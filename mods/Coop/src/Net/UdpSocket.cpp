// Winsock2 must come before anything that might pull in <windows.h>.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mstcpip.h>

// SIO_UDP_CONNRESET is documented as living in mstcpip.h, and including that
// header did not define it on the SDK this builds against. Rather than guess
// at which header or version guard is responsible, define it here: the value
// is a stable, documented ioctl code, and _WSAIOW and IOC_VENDOR both come
// from winsock2.h above.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include <cstdio>
#include <cstdlib>

#include "Net/UdpSocket.h"

#pragma comment(lib, "ws2_32.lib")

namespace Coop::Net
{
    // ---- Endpoint ---------------------------------------------------------

    std::string Endpoint::ToString() const
    {
        char buffer[32]{};

        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u:%u",
            (address >> 24) & 0xFF,
            (address >> 16) & 0xFF,
            (address >> 8) & 0xFF,
            address & 0xFF,
            static_cast<unsigned>(port));

        return std::string(buffer);
    }

    bool Endpoint::Parse(const std::string& text, uint16_t defaultPort, Endpoint& out)
    {
        if (text.empty())
        {
            return false;
        }

        std::string host = text;
        uint16_t    port = defaultPort;

        const size_t colon = text.rfind(':');

        if (colon != std::string::npos)
        {
            host = text.substr(0, colon);

            const std::string portText = text.substr(colon + 1);
            const long        parsed   = std::strtol(portText.c_str(), nullptr, 10);

            if (parsed <= 0 || parsed > 65535)
            {
                return false;
            }

            port = static_cast<uint16_t>(parsed);
        }

        if (host.empty())
        {
            return false;
        }

        addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        addrinfo* results = nullptr;

        if (getaddrinfo(host.c_str(), nullptr, &hints, &results) != 0 || !results)
        {
            return false;
        }

        const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(results->ai_addr);

        out.address = ntohl(address->sin_addr.s_addr);
        out.port    = port;

        freeaddrinfo(results);

        return out.IsValid();
    }

    // ---- WinsockGuard -----------------------------------------------------

    WinsockGuard::WinsockGuard()
    {
        WSADATA data{};

        m_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    WinsockGuard::~WinsockGuard()
    {
        if (m_ok)
        {
            WSACleanup();
        }
    }

    // ---- UdpSocket --------------------------------------------------------

    uintptr_t UdpSocket::InvalidHandle()
    {
        return static_cast<uintptr_t>(INVALID_SOCKET);
    }

    UdpSocket::~UdpSocket()
    {
        Close();
    }

    bool UdpSocket::Open(uint16_t port)
    {
        Close();

        const SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (handle == INVALID_SOCKET)
        {
            m_lastError = "socket() failed";
            return false;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port        = htons(port);

        if (bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            m_lastError = "bind() failed - port already in use?";
            closesocket(handle);

            return false;
        }

        u_long nonBlocking = 1;

        if (ioctlsocket(handle, FIONBIO, &nonBlocking) == SOCKET_ERROR)
        {
            m_lastError = "ioctlsocket(FIONBIO) failed";
            closesocket(handle);

            return false;
        }

        // A peer that has gone away makes Windows surface ICMP port-unreachable
        // as WSAECONNRESET on the *next* recvfrom, on a connectionless socket.
        // Left alone that turns one dead peer into a receive loop that reports
        // errors forever. SIO_UDP_CONNRESET off is the documented fix.
        BOOL  behaviour = FALSE;
        DWORD returned  = 0;

        WSAIoctl(handle, SIO_UDP_CONNRESET, &behaviour, sizeof(behaviour),
                 nullptr, 0, &returned, nullptr, nullptr);

        // Ask what we actually got, which matters after Open(0).
        sockaddr_in bound{};
        int         boundSize = sizeof(bound);

        if (getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &boundSize) == 0)
        {
            m_boundPort = ntohs(bound.sin_port);
        }
        else
        {
            m_boundPort = port;
        }

        m_handle = static_cast<uintptr_t>(handle);
        m_lastError.clear();

        return true;
    }

    void UdpSocket::Close()
    {
        if (IsOpen())
        {
            closesocket(static_cast<SOCKET>(m_handle));
        }

        m_handle    = InvalidHandle();
        m_boundPort = 0;
    }

    bool UdpSocket::Send(const Endpoint& to, const void* data, size_t size)
    {
        if (!IsOpen() || !to.IsValid() || size == 0)
        {
            return false;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(to.address);
        address.sin_port        = htons(to.port);

        const int sent = sendto(static_cast<SOCKET>(m_handle),
                                static_cast<const char*>(data),
                                static_cast<int>(size), 0,
                                reinterpret_cast<sockaddr*>(&address),
                                sizeof(address));

        return sent == static_cast<int>(size);
    }

    int UdpSocket::Receive(void* buffer, size_t capacity, Endpoint& from)
    {
        if (!IsOpen())
        {
            return -1;
        }

        sockaddr_in address{};
        int         addressSize = sizeof(address);

        const int received = recvfrom(static_cast<SOCKET>(m_handle),
                                      static_cast<char*>(buffer),
                                      static_cast<int>(capacity), 0,
                                      reinterpret_cast<sockaddr*>(&address),
                                      &addressSize);

        if (received == SOCKET_ERROR)
        {
            const int error = WSAGetLastError();

            // Nothing waiting is the normal case on a non-blocking socket.
            // A reset is one dead peer, not a broken socket - see the
            // SIO_UDP_CONNRESET note in Open(). Neither is worth reporting.
            if (error == WSAEWOULDBLOCK || error == WSAECONNRESET)
            {
                return 0;
            }

            return -1;
        }

        from.address = ntohl(address.sin_addr.s_addr);
        from.port    = ntohs(address.sin_port);

        return received;
    }

    bool UdpSocket::WaitReadable(int timeoutMilliseconds)
    {
        if (!IsOpen())
        {
            return false;
        }

        fd_set readable{};
        FD_ZERO(&readable);
        FD_SET(static_cast<SOCKET>(m_handle), &readable);

        timeval timeout{};
        timeout.tv_sec  = timeoutMilliseconds / 1000;
        timeout.tv_usec = (timeoutMilliseconds % 1000) * 1000;

        return select(0, &readable, nullptr, nullptr, &timeout) > 0;
    }
}
