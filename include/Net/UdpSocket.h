#pragma once

#include <cstdint>
#include <string>

// Forward-declared so <winsock2.h> stays out of every translation unit that
// only wants to name an address. The game's own headers drag in <Windows.h>
// in places, and including winsock2 after windows.h is the classic way to get
// several hundred redefinition errors.
struct sockaddr_in;

namespace Coop::Net
{
    // A UDP endpoint, stored as host-order values so it can be compared,
    // hashed and printed without touching Winsock.
    struct Endpoint
    {
        uint32_t address = 0;  // IPv4, host byte order
        uint16_t port    = 0;

        bool operator==(const Endpoint& other) const
        {
            return address == other.address && port == other.port;
        }

        bool IsValid() const { return address != 0 && port != 0; }

        std::string ToString() const;

        // Accepts "host:port" and "host" (with a default port). Host may be a
        // dotted quad or a name; names are resolved, which blocks, so only call
        // this off the game thread.
        static bool Parse(const std::string& text, uint16_t defaultPort, Endpoint& out);
    };

    // Non-blocking IPv4 UDP socket. Deliberately thin: it does one thing, and
    // everything above it (sequencing, reliability, sessions) is in Session.
    class UdpSocket
    {
    public:
        UdpSocket() = default;
        ~UdpSocket();

        UdpSocket(const UdpSocket&)            = delete;
        UdpSocket& operator=(const UdpSocket&) = delete;

        // port 0 lets the OS choose, which is what a joining client wants.
        bool Open(uint16_t port);
        void Close();
        bool IsOpen() const { return m_handle != InvalidHandle(); }

        // The port actually bound, which is only interesting after Open(0).
        uint16_t BoundPort() const { return m_boundPort; }

        bool Send(const Endpoint& to, const void* data, size_t size);

        // Returns the number of bytes received, 0 when nothing was waiting,
        // and -1 on a real error. Never blocks.
        int Receive(void* buffer, size_t capacity, Endpoint& from);

        // Blocks until the socket is readable or the timeout expires. Used to
        // keep the network thread off a busy loop without adding latency.
        bool WaitReadable(int timeoutMilliseconds);

        const std::string& LastError() const { return m_lastError; }

    private:
        static uintptr_t InvalidHandle();

        uintptr_t   m_handle    = 0;
        uint16_t    m_boundPort = 0;
        std::string m_lastError;
    };

    // Winsock needs WSAStartup before anything and WSACleanup after
    // everything. Both are refcounted per process, and the game has already
    // called WSAStartup for its own online code, but relying on that would
    // make this mod depend on the game's networking still being alive.
    class WinsockGuard
    {
    public:
        WinsockGuard();
        ~WinsockGuard();

        bool Ok() const { return m_ok; }

    private:
        bool m_ok = false;
    };
}
