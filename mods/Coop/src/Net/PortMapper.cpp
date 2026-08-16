#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <vector>

#include "Net/PortMapper.h"
#include "Diag/Diag.h"

namespace Coop::Net
{
    namespace
    {
        // The two service types a home router presents. Cable and fibre boxes
        // are almost always the first; anything speaking PPPoE is the second.
        constexpr const char* kWanIp  = "urn:schemas-upnp-org:service:WANIPConnection:1";
        constexpr const char* kWanPpp = "urn:schemas-upnp-org:service:WANPPPConnection:1";

        constexpr const char* kSsdpAddress = "239.255.255.250";
        constexpr uint16_t    kSsdpPort    = 1900;

        // Long enough for a router to answer a multicast, short enough that a
        // silent network does not hold anything up for long.
        constexpr int kDiscoverySeconds = 3;

        std::string Lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return text;
        }

        // The text between <tag> and </tag>, searching from `from`. Crude on
        // purpose: a full XML parser to read three elements out of a device
        // description would be a dependency to maintain forever.
        std::string Element(const std::string& xml, const std::string& tag, size_t from = 0)
        {
            const std::string open  = "<" + tag + ">";
            const std::string close = "</" + tag + ">";

            const size_t start = xml.find(open, from);

            if (start == std::string::npos)
            {
                return {};
            }

            const size_t valueStart = start + open.size();
            const size_t valueEnd   = xml.find(close, valueStart);

            if (valueEnd == std::string::npos)
            {
                return {};
            }

            return xml.substr(valueStart, valueEnd - valueStart);
        }

        struct Url
        {
            std::string host;
            uint16_t    port = 80;
            std::string path = "/";

            bool ok = false;
        };

        Url ParseUrl(const std::string& text)
        {
            Url url;

            const std::string prefix = "http://";

            if (text.rfind(prefix, 0) != 0)
            {
                return url;
            }

            const size_t hostStart = prefix.size();
            const size_t pathStart = text.find('/', hostStart);

            std::string authority = pathStart == std::string::npos
                ? text.substr(hostStart)
                : text.substr(hostStart, pathStart - hostStart);

            if (pathStart != std::string::npos)
            {
                url.path = text.substr(pathStart);
            }

            const size_t colon = authority.find(':');

            if (colon == std::string::npos)
            {
                url.host = authority;
            }
            else
            {
                url.host = authority.substr(0, colon);

                const int parsed = std::atoi(authority.c_str() + colon + 1);

                if (parsed <= 0 || parsed > 65535)
                {
                    return url;
                }

                url.port = static_cast<uint16_t>(parsed);
            }

            url.ok = !url.host.empty();

            return url;
        }

        // One request, one response, connection closed. Routers are HTTP/1.0
        // servers in all but name and this is what they expect.
        std::string HttpExchange(const Url& url, const std::string& request)
        {
            if (!url.ok)
            {
                return {};
            }

            addrinfo hints{};
            hints.ai_family   = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            addrinfo* results = nullptr;

            const std::string portText = std::to_string(url.port);

            if (getaddrinfo(url.host.c_str(), portText.c_str(), &hints, &results) != 0 || !results)
            {
                return {};
            }

            const SOCKET handle = socket(results->ai_family, results->ai_socktype,
                                         results->ai_protocol);

            if (handle == INVALID_SOCKET)
            {
                freeaddrinfo(results);
                return {};
            }

            // A router that is not going to answer should not cost more than a
            // couple of seconds.
            DWORD timeout = 4000;
            setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));

            if (connect(handle, results->ai_addr, static_cast<int>(results->ai_addrlen)) != 0)
            {
                closesocket(handle);
                freeaddrinfo(results);

                return {};
            }

            freeaddrinfo(results);

            if (send(handle, request.c_str(), static_cast<int>(request.size()), 0) < 0)
            {
                closesocket(handle);
                return {};
            }

            std::string response;

            char   buffer[2048];
            int    received = 0;

            while ((received = recv(handle, buffer, sizeof(buffer), 0)) > 0)
            {
                response.append(buffer, static_cast<size_t>(received));

                // Device descriptions are a few kilobytes; anything much larger
                // is not something we are going to understand anyway.
                if (response.size() > 256 * 1024)
                {
                    break;
                }
            }

            closesocket(handle);

            return response;
        }

        std::string HttpBody(const std::string& response)
        {
            const size_t split = response.find("\r\n\r\n");

            return split == std::string::npos ? std::string() : response.substr(split + 4);
        }

        // Our own address on the network the router is on. Asking the socket
        // rather than enumerating adapters: a machine with a VPN, a virtual
        // switch and a physical card has several answers, and the only correct
        // one is whichever the kernel would use to reach this router.
        std::string LocalAddressFor(const std::string& routerHost)
        {
            const SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

            if (handle == INVALID_SOCKET)
            {
                return {};
            }

            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port   = htons(9);

            if (inet_pton(AF_INET, routerHost.c_str(), &target.sin_addr) != 1)
            {
                closesocket(handle);
                return {};
            }

            // No packet is sent for a connected UDP socket; this only fixes the
            // route, which is all we are after.
            if (connect(handle, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0)
            {
                closesocket(handle);
                return {};
            }

            sockaddr_in local{};
            int         length = sizeof(local);

            if (getsockname(handle, reinterpret_cast<sockaddr*>(&local), &length) != 0)
            {
                closesocket(handle);
                return {};
            }

            closesocket(handle);

            char text[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text));

            return text;
        }

        // Every LOCATION a gateway offered, in the order they answered.
        std::vector<std::string> Discover()
        {
            std::vector<std::string> locations;

            const SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

            if (handle == INVALID_SOCKET)
            {
                return locations;
            }

            DWORD timeout = 700;
            setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));

            sockaddr_in destination{};
            destination.sin_family = AF_INET;
            destination.sin_port   = htons(kSsdpPort);
            inet_pton(AF_INET, kSsdpAddress, &destination.sin_addr);

            // Three search targets, because routers are inconsistent about
            // which they answer: the gateway device, the service itself, and
            // the catch-all.
            const char* const targets[] = {
                "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                "urn:schemas-upnp-org:service:WANIPConnection:1",
                "upnp:rootdevice",
            };

            for (const char* target : targets)
            {
                const std::string search = std::format(
                    "M-SEARCH * HTTP/1.1\r\n"
                    "HOST: {}:{}\r\n"
                    "MAN: \"ssdp:discover\"\r\n"
                    "MX: 2\r\n"
                    "ST: {}\r\n"
                    "\r\n",
                    kSsdpAddress, kSsdpPort, target);

                sendto(handle, search.c_str(), static_cast<int>(search.size()), 0,
                       reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
            }

            const DWORD deadline = GetTickCount() + kDiscoverySeconds * 1000;

            while (GetTickCount() < deadline)
            {
                char buffer[2048] = {};

                const int received = recv(handle, buffer, sizeof(buffer) - 1, 0);

                if (received <= 0)
                {
                    continue;
                }

                const std::string reply(buffer, static_cast<size_t>(received));
                const std::string lowered = Lowered(reply);

                const size_t at = lowered.find("location:");

                if (at == std::string::npos)
                {
                    continue;
                }

                size_t start = at + 9;

                while (start < reply.size() && (reply[start] == ' ' || reply[start] == '\t'))
                {
                    ++start;
                }

                const size_t end = reply.find_first_of("\r\n", start);

                if (end == std::string::npos)
                {
                    continue;
                }

                std::string location = reply.substr(start, end - start);

                if (std::find(locations.begin(), locations.end(), location) == locations.end())
                {
                    locations.push_back(std::move(location));
                }
            }

            closesocket(handle);

            return locations;
        }

        std::string SoapRequest(const Url& control, const std::string& serviceType,
                                const std::string& action, const std::string& arguments)
        {
            const std::string body = std::format(
                "<?xml version=\"1.0\"?>"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                "<s:Body><u:{} xmlns:u=\"{}\">{}</u:{}></s:Body></s:Envelope>",
                action, serviceType, arguments, action);

            return std::format(
                "POST {} HTTP/1.1\r\n"
                "HOST: {}:{}\r\n"
                "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
                "SOAPACTION: \"{}#{}\"\r\n"
                "CONTENT-LENGTH: {}\r\n"
                "CONNECTION: close\r\n"
                "\r\n{}",
                control.path, control.host, control.port,
                serviceType, action, body.size(), body);
        }

        bool Succeeded(const std::string& response)
        {
            return response.find("200 OK") != std::string::npos
                || response.find("200 Ok") != std::string::npos;
        }
    }

    PortMapper::~PortMapper()
    {
        Release();
    }

    PortMapper::State PortMapper::GetState() const
    {
        const Threading::ReadGuard guard(m_lock);

        return m_state;
    }

    std::string PortMapper::Note() const
    {
        const Threading::ReadGuard guard(m_lock);

        return m_note;
    }

    std::string PortMapper::ExternalAddress() const
    {
        const Threading::ReadGuard guard(m_lock);

        return m_external;
    }

    void PortMapper::RequestMap(uint16_t port)
    {
        Release();

        {
            const Threading::WriteGuard guard(m_lock);

            m_state    = State::Working;
            m_note     = "asking the router to open the port";
            m_external.clear();
        }

        m_worker = std::thread(&PortMapper::Work, this, port);
    }

    void PortMapper::Work(uint16_t port)
    {
        Diag::Log("upnp: searching for a gateway");

        const std::vector<std::string> locations = Discover();

        if (locations.empty())
        {
            const Threading::WriteGuard guard(m_lock);

            m_state = State::Failed;
            m_note  = "no router answered - forward UDP " + std::to_string(port)
                    + " by hand, or use a VPN";

            Diag::Log("upnp: %s", m_note.c_str());

            return;
        }

        Diag::Log("upnp: %zu gateway(s) answered", locations.size());

        for (const std::string& location : locations)
        {
            const Url description = ParseUrl(location);

            if (!description.ok)
            {
                continue;
            }

            const std::string request = std::format(
                "GET {} HTTP/1.1\r\nHOST: {}:{}\r\nCONNECTION: close\r\n\r\n",
                description.path, description.host, description.port);

            const std::string xml = HttpBody(HttpExchange(description, request));

            if (xml.empty())
            {
                continue;
            }

            // Which of the two connection services this router offers, and
            // where its control endpoint is.
            std::string serviceType;
            size_t      at = xml.find(kWanIp);

            if (at != std::string::npos)
            {
                serviceType = kWanIp;
            }
            else
            {
                at = xml.find(kWanPpp);

                if (at == std::string::npos)
                {
                    continue;
                }

                serviceType = kWanPpp;
            }

            const std::string controlPath = Element(xml, "controlURL", at);

            if (controlPath.empty())
            {
                continue;
            }

            Url control = description;

            control.path = controlPath.front() == '/' ? controlPath : "/" + controlPath;

            const std::string localAddress = LocalAddressFor(description.host);

            if (localAddress.empty())
            {
                continue;
            }

            Diag::Log("upnp: gateway at %s, control %s, we are %s",
                      description.host.c_str(), control.path.c_str(), localAddress.c_str());

            const std::string arguments = std::format(
                "<NewRemoteHost></NewRemoteHost>"
                "<NewExternalPort>{}</NewExternalPort>"
                "<NewProtocol>UDP</NewProtocol>"
                "<NewInternalPort>{}</NewInternalPort>"
                "<NewInternalClient>{}</NewInternalClient>"
                "<NewEnabled>1</NewEnabled>"
                "<NewPortMappingDescription>Absolution Co-op</NewPortMappingDescription>"
                "<NewLeaseDuration>0</NewLeaseDuration>",
                port, port, localAddress);

            const std::string reply = HttpExchange(
                control, SoapRequest(control, serviceType, "AddPortMapping", arguments));

            if (!Succeeded(reply))
            {
                Diag::Log("upnp: the gateway refused the mapping");
                continue;
            }

            // While we are talking to it: the address the other player has to
            // type. Nice to have rather than required, so a failure here does
            // not undo the mapping.
            std::string external;

            const std::string addressReply = HttpExchange(
                control, SoapRequest(control, serviceType, "GetExternalIPAddress", ""));

            if (Succeeded(addressReply))
            {
                const std::string address = Element(HttpBody(addressReply), "NewExternalIPAddress");

                if (!address.empty())
                {
                    external = std::format("{}:{}", address, port);
                }
            }

            const Threading::WriteGuard guard(m_lock);

            m_state       = State::Mapped;
            m_controlUrl  = std::format("http://{}:{}{}", control.host, control.port, control.path);
            m_serviceType = serviceType;
            m_mappedPort  = port;
            m_external    = external;

            m_note = external.empty()
                ? std::format("the router opened UDP {}", port)
                : std::format("the router opened UDP {} - others connect to {}", port, external);

            Diag::Log("upnp: %s", m_note.c_str());

            return;
        }

        const Threading::WriteGuard guard(m_lock);

        m_state = State::Failed;
        m_note  = "a router answered but would not open the port - forward UDP "
                + std::to_string(port) + " by hand, or use a VPN";

        Diag::Log("upnp: %s", m_note.c_str());
    }

    void PortMapper::Release()
    {
        if (m_worker.joinable())
        {
            m_worker.join();
        }

        std::string controlUrl;
        std::string serviceType;
        uint16_t    port = 0;

        {
            const Threading::WriteGuard guard(m_lock);

            controlUrl  = m_controlUrl;
            serviceType = m_serviceType;
            port        = m_mappedPort;

            m_controlUrl.clear();
            m_serviceType.clear();
            m_mappedPort = 0;
            m_state      = State::Idle;
            m_note.clear();
            m_external.clear();
        }

        if (controlUrl.empty() || port == 0)
        {
            return;
        }

        // Closing it again. A hole left open in somebody's router after the
        // game has gone is not ours to leave behind.
        const Url control = ParseUrl(controlUrl);

        const std::string arguments = std::format(
            "<NewRemoteHost></NewRemoteHost>"
            "<NewExternalPort>{}</NewExternalPort>"
            "<NewProtocol>UDP</NewProtocol>",
            port);

        HttpExchange(control, SoapRequest(control, serviceType, "DeletePortMapping", arguments));

        Diag::Log("upnp: mapping for UDP %u removed", static_cast<unsigned>(port));
    }
}
