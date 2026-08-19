#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Net/Protocol.h"
#include "Net/UdpSocket.h"

namespace Coop::Net
{
    enum class SessionMode : uint8_t
    {
        Offline    = 0,
        Hosting    = 1,
        Connecting = 2,  // client, handshake in flight
        Connected  = 3,  // client, host has answered
        Failed     = 4,
    };

    // One remote player as the game thread sees them. Two consecutive states
    // are kept so the renderer can interpolate between them instead of
    // snapping on every packet.
    struct PeerSnapshot
    {
        uint8_t      peerId = kInvalidPeerId;
        std::string  name;
        StateMessage current;
        StateMessage previous;
        uint64_t     currentReceivedMs  = 0;  // local clock
        uint64_t     previousReceivedMs = 0;
        uint32_t     roundTripMs        = 0;
        bool         stale              = false;
    };

    struct SessionStatus
    {
        SessionMode mode        = SessionMode::Offline;
        uint8_t     localPeerId = kInvalidPeerId;
        uint16_t    boundPort   = 0;
        uint32_t    peerCount   = 0;
        uint64_t    sessionStartMs = 0;  // local clock, corrected for host offset
        uint32_t    hostRoundTripMs = 0;
        std::string lastError;
        std::string endpointText;   // what a joiner should be told to type
        uint64_t    packetsSent     = 0;
        uint64_t    packetsReceived = 0;
        uint64_t    packetsDropped  = 0;  // malformed or from an unknown peer
    };

    // Owns the socket and a single worker thread. Every public method is safe
    // to call from the game thread; nothing here blocks it.
    //
    // The split is deliberate: the game thread must never wait on a socket,
    // because a stalled frame in a 32-bit DirectX 11 title is not a dropped
    // packet, it is a hitch the player sees.
    class Session
    {
    public:
        Session() = default;
        ~Session();

        Session(const Session&)            = delete;
        Session& operator=(const Session&) = delete;

        // port 0 on Join lets the OS pick the local port, which is what you
        // want unless you are debugging two clients on one machine.
        bool Host(uint16_t port, const std::string& name, const std::string& password);
        bool Join(const std::string& hostAddress, uint16_t defaultPort,
                  const std::string& name, const std::string& password);
        void Leave();

        bool IsActive() const;

        // Called once per frame from the game thread with the local player's
        // current state. Cheap: copies under a short lock and returns.
        void PublishLocalState(const StateMessage& state);

        // A consistent copy of every known peer, excluding the local player.
        std::vector<PeerSnapshot> SnapshotPeers() const;

        // Queues an event for reliable delivery. Host broadcasts it; a client
        // sends it to the host, which relays.
        void SendEvent(const EventMessage& event);

        // Everything received since the last call, oldest first.
        std::vector<EventMessage> DrainEvents();

        SessionStatus Status() const;

        // The fingerprint the handshake compares, so mismatched game builds
        // are reported rather than silently desynchronised.
        void SetBuildFingerprint(uint32_t fingerprint) { m_buildFingerprint = fingerprint; }

    private:
        // Per-link reliability. One of these exists for the host on a client,
        // and one per client on the host.
        struct PendingReliable
        {
            std::vector<uint8_t> bytes;
            uint64_t             lastSendMs = 0;
            uint32_t             attempts   = 0;
        };

        struct Link
        {
            Endpoint    endpoint;
            uint8_t     peerId = kInvalidPeerId;
            std::string name;

            uint64_t lastHeardMs = 0;
            uint64_t lastPingMs  = 0;
            uint32_t roundTripMs = 0;

            uint16_t nextReliableSeq = 1;
            std::unordered_map<uint16_t, PendingReliable> pending;

            // Ring of recently accepted reliable sequence numbers, so a
            // retransmission that crosses its own ack is not applied twice.
            std::vector<uint16_t> seenReliable;
            size_t                seenCursor = 0;

            bool HasSeen(uint16_t seq) const;
            void MarkSeen(uint16_t seq);
        };

        void ThreadMain();
        void Tick(uint64_t nowMs);
        void ReceiveAll(uint64_t nowMs);
        void HandlePacket(const Endpoint& from, const uint8_t* data, size_t size, uint64_t nowMs);

        void HandleHello(const Endpoint& from, const Header& header, Reader& reader, uint64_t nowMs);
        void HandleWelcome(const Endpoint& from, const Header& header, Reader& reader, uint64_t nowMs);
        void HandleState(const Header& header, Reader& reader, uint64_t nowMs);
        void HandleRoster(Reader& reader);
        void HandleEvent(const Header& header, Reader& reader, uint64_t nowMs);

        void SendRaw(const Endpoint& to, const uint8_t* data, size_t size);
        void SendUnreliable(Link& link, MessageType type,
                            const uint8_t* payload, size_t payloadSize,
                            uint8_t originPeerId);
        void SendReliable(Link& link, MessageType type,
                          const uint8_t* payload, size_t payloadSize,
                          uint8_t originPeerId);
        void SendAck(const Endpoint& to, uint16_t seq);
        void RetransmitPending(Link& link, uint64_t nowMs);

        void BroadcastRoster();
        void DropLink(uint8_t peerId, const std::string& reason, uint64_t nowMs);

        void RecordPeerState(const StateMessage& state, uint64_t nowMs);
        void PushInboundEvent(const EventMessage& event);
        void SetError(const std::string& message);

        static uint64_t NowMs();

        WinsockGuard m_winsock;
        UdpSocket    m_socket;

        std::thread       m_thread;
        std::atomic<bool> m_running{false};

        std::atomic<SessionMode> m_mode{SessionMode::Offline};
        std::atomic<uint8_t>     m_localPeerId{kInvalidPeerId};
        std::atomic<uint32_t>    m_buildFingerprint{0};

        // Written by the network thread, read by the game thread.
        mutable std::mutex                       m_stateMutex;
        std::unordered_map<uint8_t, PeerSnapshot> m_peers;
        std::string                               m_lastError;
        uint64_t                                  m_sessionStartMs = 0;
        uint32_t                                  m_hostRoundTripMs = 0;
        uint64_t                                  m_packetsSent = 0;
        uint64_t                                  m_packetsReceived = 0;
        uint64_t                                  m_packetsDropped = 0;

        // Written by the game thread, read by the network thread.
        mutable std::mutex       m_outboundMutex;
        StateMessage             m_localState;
        bool                     m_localStateValid = false;
        uint64_t                 m_localStatePublishedMs = 0;
        std::deque<EventMessage> m_outboundEvents;

        mutable std::mutex       m_inboundMutex;
        std::deque<EventMessage> m_inboundEvents;

        // Network-thread-only state. No lock: nothing else touches it.
        std::unordered_map<uint8_t, Link> m_links;
        Endpoint                          m_hostEndpoint;
        uint32_t                          m_sessionId       = 0;
        uint32_t                          m_passwordHash    = 0;
        std::string                       m_localName       = "47";
        uint64_t                          m_lastStateSendMs = 0;
        uint64_t                          m_lastHelloSendMs = 0;
        uint64_t                          m_connectStartMs  = 0;
        uint8_t                           m_nextPeerId      = 1;
    };
}
