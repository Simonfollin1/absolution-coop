#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <iterator>

#include "Net/Session.h"

#include "Diag/Diag.h"

namespace Coop::Net
{
    namespace
    {
        // How often each player's position goes out. 20 Hz is what the
        // interpolation below is tuned for: enough that a sprinting player
        // reads as smooth, cheap enough that eight of them is a rounding error
        // on any connection that can run a game at all.
        constexpr uint64_t kStateIntervalMs = 50;

        constexpr uint64_t kPingIntervalMs      = 1000;
        constexpr uint64_t kHelloIntervalMs     = 500;
        constexpr uint64_t kHandshakeTimeoutMs  = 10000;
        constexpr uint64_t kLinkTimeoutMs       = 8000;
        constexpr uint64_t kRetransmitIntervalMs = 250;
        constexpr uint32_t kMaxRetransmits      = 24;

        // Deep enough that a burst of retransmissions cannot push a sequence
        // number out of the window before its ack arrives.
        constexpr size_t kSeenReliableCapacity = 256;

        constexpr size_t kInboundEventCapacity  = 128;
        constexpr size_t kOutboundEventCapacity = 64;

        constexpr int kReceiveWaitMs = 20;
    }

    // ---- Link -------------------------------------------------------------

    bool Session::Link::HasSeen(uint16_t seq) const
    {
        return std::find(seenReliable.begin(), seenReliable.end(), seq) != seenReliable.end();
    }

    void Session::Link::MarkSeen(uint16_t seq)
    {
        if (seenReliable.size() < kSeenReliableCapacity)
        {
            seenReliable.push_back(seq);
            return;
        }

        seenReliable[seenCursor] = seq;
        seenCursor = (seenCursor + 1) % kSeenReliableCapacity;
    }

    // ---- Lifetime ---------------------------------------------------------

    Session::~Session()
    {
        Leave();
    }

    uint64_t Session::NowMs()
    {
        return GetTickCount64();
    }

    bool Session::IsActive() const
    {
        const SessionMode mode = m_mode.load();

        return mode == SessionMode::Hosting
            || mode == SessionMode::Connecting
            || mode == SessionMode::Connected;
    }

    void Session::SetError(const std::string& message)
    {
        std::lock_guard<std::mutex> guard(m_stateMutex);

        m_lastError = message;
    }

    bool Session::Host(uint16_t port, const std::string& name, const std::string& password)
    {
        Leave();

        if (!m_winsock.Ok())
        {
            SetError("Winsock could not be initialised");
            m_mode.store(SessionMode::Failed);

            return false;
        }

        if (!m_socket.Open(port))
        {
            SetError(m_socket.LastError());
            m_mode.store(SessionMode::Failed);

            return false;
        }

        m_localName    = name.empty() ? std::string("Host") : name;
        m_passwordHash = HashPassword(password);
        m_nextPeerId   = 1;
        m_links.clear();
        m_hostEndpoint = Endpoint{};

        // Any non-zero value works; it only has to be stable for the lifetime
        // of the session and unlikely to collide with a session a peer just
        // left, so that stale packets from the old one are discarded.
        m_sessionId = static_cast<uint32_t>(NowMs()) | 1u;

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);

            m_peers.clear();
            m_lastError.clear();
            m_sessionStartMs   = NowMs();
            m_hostRoundTripMs  = 0;
            m_packetsSent      = 0;
            m_packetsReceived  = 0;
            m_packetsDropped   = 0;
        }

        m_localPeerId.store(kHostPeerId);
        m_mode.store(SessionMode::Hosting);
        m_running.store(true);
        m_thread = std::thread(&Session::ThreadMain, this);

        return true;
    }

    bool Session::Join(const std::string& hostAddress, uint16_t defaultPort,
                       const std::string& name, const std::string& password)
    {
        Leave();

        if (!m_winsock.Ok())
        {
            SetError("Winsock could not be initialised");
            m_mode.store(SessionMode::Failed);

            return false;
        }

        Endpoint endpoint;

        if (!Endpoint::Parse(hostAddress, defaultPort, endpoint))
        {
            SetError("Could not resolve \"" + hostAddress + "\"");
            m_mode.store(SessionMode::Failed);

            return false;
        }

        // Port 0: let the OS choose, so two clients on one machine do not
        // fight over a fixed local port.
        if (!m_socket.Open(0))
        {
            SetError(m_socket.LastError());
            m_mode.store(SessionMode::Failed);

            return false;
        }

        m_localName      = name.empty() ? std::string("Agent") : name;
        m_passwordHash   = HashPassword(password);
        m_hostEndpoint   = endpoint;
        m_sessionId      = 0;  // learned from the Welcome
        m_connectStartMs = NowMs();
        m_lastHelloSendMs = 0;
        m_links.clear();

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);

            m_peers.clear();
            m_lastError.clear();
            m_sessionStartMs  = 0;
            m_hostRoundTripMs = 0;
            m_packetsSent     = 0;
            m_packetsReceived = 0;
            m_packetsDropped  = 0;
        }

        m_localPeerId.store(kInvalidPeerId);
        m_mode.store(SessionMode::Connecting);
        m_running.store(true);
        m_thread = std::thread(&Session::ThreadMain, this);

        return true;
    }

    void Session::Leave()
    {
        // Join unconditionally. The network thread can retire itself - a
        // refused handshake or a lost host both clear m_running from inside it
        // - so keying the join off m_running would leave a joinable thread
        // behind, and assigning over a joinable std::thread calls
        // std::terminate. Which is to say: the game closes.
        m_running.store(false);

        if (m_thread.joinable())
        {
            m_thread.join();
        }

        m_socket.Close();
        m_links.clear();
        m_mode.store(SessionMode::Offline);
        m_localPeerId.store(kInvalidPeerId);

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            m_peers.clear();
        }

        {
            std::lock_guard<std::mutex> guard(m_outboundMutex);
            m_outboundEvents.clear();
            m_localStateValid = false;
        }
    }

    // ---- Game-thread interface --------------------------------------------

    void Session::PublishLocalState(const StateMessage& state)
    {
        std::lock_guard<std::mutex> guard(m_outboundMutex);

        m_localState      = state;
        m_localStateValid = true;
    }

    std::vector<PeerSnapshot> Session::SnapshotPeers() const
    {
        std::vector<PeerSnapshot> result;

        std::lock_guard<std::mutex> guard(m_stateMutex);

        result.reserve(m_peers.size());

        for (const auto& entry : m_peers)
        {
            result.push_back(entry.second);
        }

        std::sort(result.begin(), result.end(),
            [](const PeerSnapshot& a, const PeerSnapshot& b) { return a.peerId < b.peerId; });

        return result;
    }

    void Session::SendEvent(const EventMessage& event)
    {
        std::lock_guard<std::mutex> guard(m_outboundMutex);

        // A full queue means the link is not draining. Dropping the oldest
        // keeps the newest, which is the one worth having.
        if (m_outboundEvents.size() >= kOutboundEventCapacity)
        {
            m_outboundEvents.pop_front();
        }

        m_outboundEvents.push_back(event);
    }

    std::vector<EventMessage> Session::DrainEvents()
    {
        std::vector<EventMessage> result;

        std::lock_guard<std::mutex> guard(m_inboundMutex);

        result.assign(m_inboundEvents.begin(), m_inboundEvents.end());
        m_inboundEvents.clear();

        return result;
    }

    SessionStatus Session::Status() const
    {
        SessionStatus status;

        status.mode        = m_mode.load();
        status.localPeerId = m_localPeerId.load();
        status.boundPort   = m_socket.BoundPort();

        std::lock_guard<std::mutex> guard(m_stateMutex);

        status.peerCount       = static_cast<uint32_t>(m_peers.size());
        status.lastError       = m_lastError;
        status.sessionStartMs  = m_sessionStartMs;
        status.hostRoundTripMs = m_hostRoundTripMs;
        status.packetsSent     = m_packetsSent;
        status.packetsReceived = m_packetsReceived;
        status.packetsDropped  = m_packetsDropped;

        return status;
    }

    void Session::PushInboundEvent(const EventMessage& event)
    {
        std::lock_guard<std::mutex> guard(m_inboundMutex);

        if (m_inboundEvents.size() >= kInboundEventCapacity)
        {
            m_inboundEvents.pop_front();
        }

        m_inboundEvents.push_back(event);
    }

    // ---- Network thread ---------------------------------------------------

    void Session::ThreadMain()
    {
        Diag::NameCurrentThread("net");

        while (m_running.load())
        {
            // Sleeps until a packet arrives or the timeout expires, so the
            // loop is neither a spin nor a source of added latency.
            m_socket.WaitReadable(kReceiveWaitMs);

            const uint64_t nowMs = NowMs();

            ReceiveAll(nowMs);
            Tick(nowMs);
        }

        // Best-effort goodbye. If it is lost the peer times out instead, which
        // is why this is not retried.
        uint8_t buffer[Header::kSize]{};
        Writer  writer(buffer, sizeof(buffer));

        Header header;
        header.type        = static_cast<uint8_t>(MessageType::Bye);
        header.senderId    = m_localPeerId.load();
        header.sessionId   = m_sessionId;

        if (WriteHeader(writer, header))
        {
            if (m_mode.load() == SessionMode::Hosting)
            {
                for (const auto& entry : m_links)
                {
                    m_socket.Send(entry.second.endpoint, buffer, writer.Size());
                }
            }
            else if (m_hostEndpoint.IsValid())
            {
                m_socket.Send(m_hostEndpoint, buffer, writer.Size());
            }
        }
    }

    void Session::ReceiveAll(uint64_t nowMs)
    {
        uint8_t  buffer[kMaxPacketSize];
        Endpoint from;

        // Bounded so a flood cannot keep this loop from ever reaching Tick().
        for (int i = 0; i < 256; ++i)
        {
            const int received = m_socket.Receive(buffer, sizeof(buffer), from);

            if (received <= 0)
            {
                break;
            }

            HandlePacket(from, buffer, static_cast<size_t>(received), nowMs);
        }
    }

    void Session::HandlePacket(const Endpoint& from, const uint8_t* data, size_t size, uint64_t nowMs)
    {
        Reader reader(data, size);
        Header header;

        const auto drop = [this]()
        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            ++m_packetsDropped;
        };

        if (!ReadHeader(reader, header) || header.magic != kMagic)
        {
            drop();
            return;
        }

        if (header.protocolVersion != kProtocolVersion)
        {
            // Worth surfacing rather than dropping silently: a version
            // mismatch is the single most likely reason two people cannot see
            // each other, and it is invisible from inside the game otherwise.
            SetError("A peer is running co-op protocol v" +
                     std::to_string(header.protocolVersion) + ", this build speaks v" +
                     std::to_string(kProtocolVersion));
            drop();

            return;
        }

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            ++m_packetsReceived;
        }

        const SessionMode mode = m_mode.load();
        const auto        type = static_cast<MessageType>(header.type);

        // A client talks to exactly one address. Anything else is either a
        // stray packet or somebody probing, and neither gets parsed further.
        if (mode != SessionMode::Hosting && !(from == m_hostEndpoint))
        {
            drop();
            return;
        }

        // Hello is the one message that legitimately arrives before a session
        // id is agreed, so it is checked before the session filter.
        if (type == MessageType::Hello)
        {
            if (mode == SessionMode::Hosting)
            {
                HandleHello(from, header, reader, nowMs);
            }

            return;
        }

        if (type == MessageType::Welcome || type == MessageType::Reject)
        {
            if (mode == SessionMode::Connecting)
            {
                HandleWelcome(from, header, reader, nowMs);
            }

            return;
        }

        if (m_sessionId != 0 && header.sessionId != m_sessionId)
        {
            drop();
            return;
        }

        // Two different identities are in play and conflating them is a bug.
        //
        //   header.senderId  - which *player* the message is about.
        //   transportLink    - which *link* it physically arrived on.
        //
        // On a client those differ for everything the host relays: a state
        // update about peer 3 arrives over the link to peer 0. Acks, dedup and
        // liveness all belong to the link; only the payload belongs to the
        // player.
        Link* transportLink = nullptr;

        if (mode == SessionMode::Hosting)
        {
            for (auto& entry : m_links)
            {
                if (entry.second.endpoint == from)
                {
                    transportLink = &entry.second;
                    break;
                }
            }

            if (!transportLink)
            {
                // An unknown sender on the host is a peer whose Hello was lost
                // or one that outlived a previous session.
                drop();
                return;
            }
        }
        else
        {
            const auto hostLink = m_links.find(kHostPeerId);

            if (hostLink == m_links.end())
            {
                drop();
                return;
            }

            transportLink = &hostLink->second;
        }

        transportLink->lastHeardMs = nowMs;

        switch (type)
        {
        case MessageType::Ping:
        {
            uint64_t stamp = 0;

            if (reader.ReadU64(stamp))
            {
                uint8_t payload[sizeof(uint64_t)]{};
                Writer  payloadWriter(payload, sizeof(payload));

                payloadWriter.WriteU64(stamp);

                SendUnreliable(*transportLink, MessageType::Pong,
                               payload, payloadWriter.Size(), m_localPeerId.load());
            }

            break;
        }

        case MessageType::Pong:
        {
            uint64_t stamp = 0;

            if (reader.ReadU64(stamp) && stamp <= nowMs)
            {
                const uint32_t rtt = static_cast<uint32_t>(nowMs - stamp);

                transportLink->roundTripMs = rtt;

                std::lock_guard<std::mutex> guard(m_stateMutex);

                const auto peer = m_peers.find(header.senderId);

                if (peer != m_peers.end())
                {
                    peer->second.roundTripMs = rtt;
                }

                if (header.senderId == kHostPeerId)
                {
                    m_hostRoundTripMs = rtt;
                }
            }

            break;
        }

        case MessageType::Bye:
            DropLink(transportLink->peerId, "left the session", nowMs);
            break;

        case MessageType::Ack:
        {
            uint16_t seq = 0;

            if (reader.ReadU16(seq))
            {
                transportLink->pending.erase(seq);
            }

            break;
        }

        case MessageType::State:
            HandleState(header, reader, nowMs);
            break;

        case MessageType::Roster:
            if (mode != SessionMode::Hosting)
            {
                SendAck(from, header.reliableSeq);

                if (transportLink->HasSeen(header.reliableSeq))
                {
                    break;
                }

                transportLink->MarkSeen(header.reliableSeq);

                HandleRoster(reader);
            }

            break;

        case MessageType::Event:
        {
            SendAck(from, header.reliableSeq);

            if (transportLink->HasSeen(header.reliableSeq))
            {
                break;
            }

            transportLink->MarkSeen(header.reliableSeq);

            HandleEvent(header, reader, nowMs);
            break;
        }

        default:
            drop();
            break;
        }
    }

    void Session::HandleHello(const Endpoint& from, const Header& header, Reader& reader, uint64_t nowMs)
    {
        HelloMessage hello;

        if (!ReadHello(reader, hello))
        {
            return;
        }

        const auto reject = [&](RejectReason reason)
        {
            uint8_t buffer[Header::kSize + 1]{};
            Writer  writer(buffer, sizeof(buffer));

            Header responseHeader;
            responseHeader.type      = static_cast<uint8_t>(MessageType::Reject);
            responseHeader.senderId  = kHostPeerId;
            responseHeader.sessionId = m_sessionId;

            WriteHeader(writer, responseHeader);
            writer.WriteU8(static_cast<uint8_t>(reason));

            SendRaw(from, buffer, writer.Size());
        };

        if (hello.passwordHash != m_passwordHash)
        {
            reject(RejectReason::WrongPassword);
            return;
        }

        // A Hello from an address that is already a peer is a retransmission:
        // the previous Welcome was lost. Answer again with the same id rather
        // than handing out a second one.
        uint8_t assignedId = kInvalidPeerId;

        for (const auto& entry : m_links)
        {
            if (entry.second.endpoint == from)
            {
                assignedId = entry.first;
                break;
            }
        }

        if (assignedId == kInvalidPeerId)
        {
            if (m_links.size() + 1 >= kMaxPeers)
            {
                reject(RejectReason::SessionFull);
                return;
            }

            // Reuse the lowest free id so a session that has churned does not
            // run out of them.
            for (uint8_t candidate = 1; candidate < kMaxPeers; ++candidate)
            {
                if (!m_links.contains(candidate))
                {
                    assignedId = candidate;
                    break;
                }
            }

            if (assignedId == kInvalidPeerId)
            {
                reject(RejectReason::SessionFull);
                return;
            }

            Link link;
            link.endpoint    = from;
            link.peerId      = assignedId;
            link.name        = hello.name;
            link.lastHeardMs = nowMs;

            m_links.emplace(assignedId, std::move(link));

            {
                std::lock_guard<std::mutex> guard(m_stateMutex);

                PeerSnapshot snapshot;
                snapshot.peerId = assignedId;
                snapshot.name   = hello.name;

                m_peers[assignedId] = std::move(snapshot);
            }

            EventMessage joined;
            joined.type         = EventType::PlayerJoined;
            joined.originPeerId = assignedId;
            joined.timestampMs  = static_cast<uint32_t>(nowMs);
            joined.text         = hello.name;

            PushInboundEvent(joined);
            SendEvent(joined);

            if (hello.buildFingerprint != 0 &&
                m_buildFingerprint.load() != 0 &&
                hello.buildFingerprint != m_buildFingerprint.load())
            {
                SetError(hello.name + " is running a different game build - "
                         "positions will not line up between your worlds");
            }
        }
        else
        {
            m_links[assignedId].lastHeardMs = nowMs;
        }

        uint8_t buffer[kMaxPacketSize]{};
        Writer  writer(buffer, sizeof(buffer));

        Header responseHeader;
        responseHeader.type      = static_cast<uint8_t>(MessageType::Welcome);
        responseHeader.senderId  = kHostPeerId;
        responseHeader.sessionId = m_sessionId;

        WelcomeMessage welcome;
        welcome.assignedPeerId = assignedId;
        welcome.sessionId      = m_sessionId;
        welcome.hostNowMs      = nowMs;

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            welcome.sessionStartMs = m_sessionStartMs;
        }

        if (WriteHeader(writer, responseHeader) && WriteWelcome(writer, welcome))
        {
            SendRaw(from, buffer, writer.Size());
        }

        BroadcastRoster();
    }

    void Session::HandleWelcome(const Endpoint& from, const Header& header, Reader& reader, uint64_t nowMs)
    {
        if (static_cast<MessageType>(header.type) == MessageType::Reject)
        {
            uint8_t reason = 0;
            reader.ReadU8(reason);

            switch (static_cast<RejectReason>(reason))
            {
            case RejectReason::WrongPassword:
                SetError("Host refused the connection: wrong password");
                break;
            case RejectReason::SessionFull:
                SetError("Host refused the connection: session is full");
                break;
            case RejectReason::ProtocolMismatch:
                SetError("Host refused the connection: protocol mismatch");
                break;
            default:
                SetError("Host refused the connection");
                break;
            }

            m_mode.store(SessionMode::Failed);
            m_running.store(false);

            return;
        }

        WelcomeMessage welcome;

        if (!ReadWelcome(reader, welcome) || welcome.assignedPeerId == kInvalidPeerId)
        {
            return;
        }

        m_sessionId = welcome.sessionId;
        m_localPeerId.store(welcome.assignedPeerId);

        Link hostLink;
        hostLink.endpoint    = from;
        hostLink.peerId      = kHostPeerId;
        hostLink.name        = "Host";
        hostLink.lastHeardMs = nowMs;

        m_links.clear();
        m_links.emplace(kHostPeerId, std::move(hostLink));

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);

            PeerSnapshot snapshot;
            snapshot.peerId = kHostPeerId;
            snapshot.name   = "Host";

            m_peers[kHostPeerId] = std::move(snapshot);

            // The host's session clock, translated into our own. Half the
            // handshake round trip is the usual estimate of one-way delay and
            // is accurate enough for a shared mission timer; it is not
            // accurate enough for anything that compares positions in time,
            // which is why nothing does.
            const uint64_t oneWayMs = (nowMs - m_connectStartMs) / 2;
            const uint64_t hostAge  = welcome.hostNowMs > welcome.sessionStartMs
                ? welcome.hostNowMs - welcome.sessionStartMs
                : 0;

            m_sessionStartMs = nowMs > (hostAge + oneWayMs) ? nowMs - hostAge - oneWayMs : nowMs;
            m_lastError.clear();
        }

        m_mode.store(SessionMode::Connected);
    }

    void Session::HandleState(const Header& header, Reader& reader, uint64_t nowMs)
    {
        StateMessage state;

        if (!ReadState(reader, state))
        {
            return;
        }

        // The origin is whoever the header says, not whoever relayed it. On a
        // client every packet arrives from the host's address, so the header
        // is the only thing that identifies the actual player.
        state.peerId = header.senderId;

        if (state.peerId == m_localPeerId.load())
        {
            return;
        }

        RecordPeerState(state, nowMs);

        // Host duty: pass it on to everyone else, unchanged.
        if (m_mode.load() == SessionMode::Hosting)
        {
            uint8_t payload[kMaxPacketSize]{};
            Writer  payloadWriter(payload, sizeof(payload));

            if (!WriteState(payloadWriter, state))
            {
                return;
            }

            for (auto& entry : m_links)
            {
                if (entry.first == header.senderId)
                {
                    continue;
                }

                SendUnreliable(entry.second, MessageType::State,
                               payload, payloadWriter.Size(), header.senderId);
            }
        }
    }

    void Session::HandleRoster(Reader& reader)
    {
        RosterMessage roster;

        if (!ReadRoster(reader, roster))
        {
            return;
        }

        const uint8_t localId = m_localPeerId.load();

        std::lock_guard<std::mutex> guard(m_stateMutex);

        // The roster is the authority on who exists. Anyone in the local table
        // and not in it has left.
        for (auto iterator = m_peers.begin(); iterator != m_peers.end();)
        {
            const bool present = std::any_of(roster.entries.begin(), roster.entries.end(),
                [&](const RosterEntry& entry) { return entry.peerId == iterator->first; });

            iterator = present ? std::next(iterator) : m_peers.erase(iterator);
        }

        for (const RosterEntry& entry : roster.entries)
        {
            if (entry.peerId == localId)
            {
                continue;
            }

            PeerSnapshot& snapshot = m_peers[entry.peerId];

            snapshot.peerId = entry.peerId;
            snapshot.name   = entry.name;
        }
    }

    void Session::HandleEvent(const Header& header, Reader& reader, uint64_t nowMs)
    {
        EventMessage event;

        if (!ReadEvent(reader, event))
        {
            return;
        }

        if (event.originPeerId == kInvalidPeerId)
        {
            event.originPeerId = header.senderId;
        }

        if (event.originPeerId == m_localPeerId.load())
        {
            return;
        }

        PushInboundEvent(event);

        if (m_mode.load() == SessionMode::Hosting)
        {
            uint8_t payload[kMaxPacketSize]{};
            Writer  payloadWriter(payload, sizeof(payload));

            if (!WriteEvent(payloadWriter, event))
            {
                return;
            }

            for (auto& entry : m_links)
            {
                if (entry.first == header.senderId)
                {
                    continue;
                }

                SendReliable(entry.second, MessageType::Event,
                             payload, payloadWriter.Size(), event.originPeerId);
            }
        }
    }

    void Session::RecordPeerState(const StateMessage& state, uint64_t nowMs)
    {
        std::lock_guard<std::mutex> guard(m_stateMutex);

        PeerSnapshot& snapshot = m_peers[state.peerId];

        snapshot.peerId = state.peerId;

        // Out-of-order UDP: an older snapshot arriving after a newer one would
        // make the interpolation run backwards. Dropping it is correct.
        if (snapshot.currentReceivedMs != 0 &&
            static_cast<int32_t>(state.timestampMs - snapshot.current.timestampMs) <= 0)
        {
            return;
        }

        snapshot.previous           = snapshot.current;
        snapshot.previousReceivedMs = snapshot.currentReceivedMs;
        snapshot.current            = state;
        snapshot.currentReceivedMs  = nowMs;
        snapshot.stale              = false;
    }

    // ---- Sending ----------------------------------------------------------

    void Session::SendRaw(const Endpoint& to, const uint8_t* data, size_t size)
    {
        if (m_socket.Send(to, data, size))
        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            ++m_packetsSent;
        }
    }

    void Session::SendUnreliable(Link& link, MessageType type,
                                 const uint8_t* payload, size_t payloadSize,
                                 uint8_t originPeerId)
    {
        uint8_t buffer[kMaxPacketSize]{};
        Writer  writer(buffer, sizeof(buffer));

        Header header;
        header.type      = static_cast<uint8_t>(type);
        header.senderId  = originPeerId;
        header.sessionId = m_sessionId;

        if (!WriteHeader(writer, header))
        {
            return;
        }

        const size_t headerSize = writer.Size();

        if (headerSize + payloadSize > sizeof(buffer))
        {
            return;
        }

        if (payloadSize > 0)
        {
            std::memcpy(buffer + headerSize, payload, payloadSize);
        }

        SendRaw(link.endpoint, buffer, headerSize + payloadSize);
    }

    void Session::SendReliable(Link& link, MessageType type,
                               const uint8_t* payload, size_t payloadSize,
                               uint8_t originPeerId)
    {
        // Sequence 0 means "unreliable", so it is never handed out.
        if (link.nextReliableSeq == 0)
        {
            link.nextReliableSeq = 1;
        }

        const uint16_t seq = link.nextReliableSeq++;

        uint8_t buffer[kMaxPacketSize]{};
        Writer  writer(buffer, sizeof(buffer));

        Header header;
        header.type        = static_cast<uint8_t>(type);
        header.senderId    = originPeerId;
        header.sessionId   = m_sessionId;
        header.reliableSeq = seq;

        if (!WriteHeader(writer, header))
        {
            return;
        }

        if (writer.Size() + payloadSize > sizeof(buffer))
        {
            return;
        }

        std::memcpy(buffer + writer.Size(), payload, payloadSize);

        const size_t total = writer.Size() + payloadSize;

        PendingReliable pending;
        pending.bytes.assign(buffer, buffer + total);
        pending.lastSendMs = NowMs();
        pending.attempts   = 1;

        link.pending[seq] = std::move(pending);

        SendRaw(link.endpoint, buffer, total);
    }

    void Session::SendAck(const Endpoint& to, uint16_t seq)
    {
        uint8_t buffer[Header::kSize + sizeof(uint16_t)]{};
        Writer  writer(buffer, sizeof(buffer));

        Header header;
        header.type      = static_cast<uint8_t>(MessageType::Ack);
        header.senderId  = m_localPeerId.load();
        header.sessionId = m_sessionId;

        WriteHeader(writer, header);
        writer.WriteU16(seq);

        SendRaw(to, buffer, writer.Size());
    }

    void Session::RetransmitPending(Link& link, uint64_t nowMs)
    {
        for (auto iterator = link.pending.begin(); iterator != link.pending.end();)
        {
            PendingReliable& pending = iterator->second;

            if (nowMs - pending.lastSendMs < kRetransmitIntervalMs)
            {
                ++iterator;
                continue;
            }

            if (pending.attempts >= kMaxRetransmits)
            {
                // Six seconds of no ack. The link timeout will finish the job;
                // continuing to retransmit only wastes bandwidth.
                iterator = link.pending.erase(iterator);
                continue;
            }

            SendRaw(link.endpoint, pending.bytes.data(), pending.bytes.size());

            pending.lastSendMs = nowMs;
            ++pending.attempts;
            ++iterator;
        }
    }

    void Session::BroadcastRoster()
    {
        if (m_mode.load() != SessionMode::Hosting)
        {
            return;
        }

        RosterMessage roster;

        RosterEntry hostEntry;
        hostEntry.peerId = kHostPeerId;
        hostEntry.name   = m_localName;

        roster.entries.push_back(std::move(hostEntry));

        for (const auto& entry : m_links)
        {
            RosterEntry rosterEntry;
            rosterEntry.peerId = entry.first;
            rosterEntry.name   = entry.second.name;

            roster.entries.push_back(std::move(rosterEntry));
        }

        uint8_t payload[kMaxPacketSize]{};
        Writer  payloadWriter(payload, sizeof(payload));

        if (!WriteRoster(payloadWriter, roster))
        {
            return;
        }

        for (auto& entry : m_links)
        {
            SendReliable(entry.second, MessageType::Roster,
                         payload, payloadWriter.Size(), kHostPeerId);
        }
    }

    void Session::DropLink(uint8_t peerId, const std::string& reason, uint64_t nowMs)
    {
        const auto iterator = m_links.find(peerId);

        if (iterator == m_links.end())
        {
            return;
        }

        const std::string name = iterator->second.name;

        m_links.erase(iterator);

        {
            std::lock_guard<std::mutex> guard(m_stateMutex);
            m_peers.erase(peerId);
        }

        EventMessage left;
        left.type         = EventType::PlayerLeft;
        left.originPeerId = peerId;
        left.timestampMs  = static_cast<uint32_t>(nowMs);
        left.text         = name.empty() ? reason : name + " " + reason;

        PushInboundEvent(left);

        if (m_mode.load() == SessionMode::Hosting)
        {
            SendEvent(left);
            BroadcastRoster();
        }
        else if (peerId == kHostPeerId)
        {
            SetError("Lost the connection to the host");
            m_mode.store(SessionMode::Failed);
            m_running.store(false);
        }
    }

    // ---- Per-tick work ----------------------------------------------------

    void Session::Tick(uint64_t nowMs)
    {
        const SessionMode mode = m_mode.load();

        if (mode == SessionMode::Connecting)
        {
            if (nowMs - m_connectStartMs > kHandshakeTimeoutMs)
            {
                SetError("No answer from " + m_hostEndpoint.ToString() +
                         " - check the address, the port forward and the firewall");
                m_mode.store(SessionMode::Failed);
                m_running.store(false);

                return;
            }

            if (nowMs - m_lastHelloSendMs >= kHelloIntervalMs)
            {
                m_lastHelloSendMs = nowMs;

                uint8_t buffer[kMaxPacketSize]{};
                Writer  writer(buffer, sizeof(buffer));

                Header header;
                header.type      = static_cast<uint8_t>(MessageType::Hello);
                header.senderId  = kInvalidPeerId;
                header.sessionId = 0;

                HelloMessage hello;
                hello.passwordHash     = m_passwordHash;
                hello.buildFingerprint = m_buildFingerprint.load();
                hello.name             = m_localName;

                if (WriteHeader(writer, header) && WriteHello(writer, hello))
                {
                    SendRaw(m_hostEndpoint, buffer, writer.Size());
                }
            }

            return;
        }

        if (mode != SessionMode::Hosting && mode != SessionMode::Connected)
        {
            return;
        }

        // Local state out at a fixed rate, independent of frame rate. A player
        // at 30 fps and one at 144 should cost the network the same.
        if (nowMs - m_lastStateSendMs >= kStateIntervalMs)
        {
            m_lastStateSendMs = nowMs;

            StateMessage state;
            bool         valid = false;

            {
                std::lock_guard<std::mutex> guard(m_outboundMutex);

                state = m_localState;
                valid = m_localStateValid;
            }

            if (valid)
            {
                state.peerId      = m_localPeerId.load();
                state.timestampMs = static_cast<uint32_t>(nowMs);

                uint8_t payload[kMaxPacketSize]{};
                Writer  payloadWriter(payload, sizeof(payload));

                if (WriteState(payloadWriter, state))
                {
                    for (auto& entry : m_links)
                    {
                        SendUnreliable(entry.second, MessageType::State,
                                       payload, payloadWriter.Size(), state.peerId);
                    }
                }
            }
        }

        // Outbound events, drained one tick at a time so a burst does not
        // build one oversized packet train.
        for (;;)
        {
            EventMessage event;

            {
                std::lock_guard<std::mutex> guard(m_outboundMutex);

                if (m_outboundEvents.empty())
                {
                    break;
                }

                event = m_outboundEvents.front();
                m_outboundEvents.pop_front();
            }

            if (event.originPeerId == kInvalidPeerId)
            {
                event.originPeerId = m_localPeerId.load();
            }

            if (event.timestampMs == 0)
            {
                event.timestampMs = static_cast<uint32_t>(nowMs);
            }

            uint8_t payload[kMaxPacketSize]{};
            Writer  payloadWriter(payload, sizeof(payload));

            if (!WriteEvent(payloadWriter, event))
            {
                continue;
            }

            for (auto& entry : m_links)
            {
                SendReliable(entry.second, MessageType::Event,
                             payload, payloadWriter.Size(), event.originPeerId);
            }
        }

        // Keepalive and round-trip measurement.
        std::vector<uint8_t> timedOut;

        for (auto& entry : m_links)
        {
            Link& link = entry.second;

            if (nowMs - link.lastPingMs >= kPingIntervalMs)
            {
                link.lastPingMs = nowMs;

                uint8_t payload[sizeof(uint64_t)]{};
                Writer  payloadWriter(payload, sizeof(payload));

                payloadWriter.WriteU64(nowMs);

                SendUnreliable(link, MessageType::Ping,
                               payload, payloadWriter.Size(), m_localPeerId.load());
            }

            RetransmitPending(link, nowMs);

            if (link.lastHeardMs != 0 && nowMs - link.lastHeardMs > kLinkTimeoutMs)
            {
                timedOut.push_back(entry.first);
            }
        }

        for (const uint8_t peerId : timedOut)
        {
            DropLink(peerId, "timed out", nowMs);
        }

        // Mark peers whose last packet is old enough that drawing them where
        // they were would be a lie.
        {
            std::lock_guard<std::mutex> guard(m_stateMutex);

            for (auto& entry : m_peers)
            {
                entry.second.stale = entry.second.currentReceivedMs == 0 ||
                                     nowMs - entry.second.currentReceivedMs > 2000;
            }
        }
    }
}
