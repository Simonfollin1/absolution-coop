#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Wire protocol for Absolution Co-op.
//
// Everything is little-endian, which is the only endianness this game ever
// runs on (32-bit x86 Windows), so no byte swapping is done anywhere.
//
// Topology is a star: clients only ever exchange packets with the host, and
// the host relays between them. That means exactly one machine needs a
// reachable UDP port, and it keeps peer discovery trivial. It also gives the
// host a natural place to be authoritative later, when there is shared world
// state worth arbitrating.
namespace Coop::Net
{
    // 'COOP' as it appears on the wire.
    inline constexpr uint32_t kMagic = 0x504F4F43u;

    // Bumped whenever the layout of any message below changes. Peers that
    // disagree are rejected at the handshake rather than left to misparse.
    inline constexpr uint16_t kProtocolVersion = 1;

    // Conservatively under the smallest MTU worth caring about (1280 for IPv6),
    // so a state or roster packet is never fragmented.
    inline constexpr size_t kMaxPacketSize = 1100;

    inline constexpr uint8_t kMaxPeers      = 8;
    inline constexpr uint8_t kHostPeerId    = 0;
    inline constexpr uint8_t kInvalidPeerId = 0xFF;

    inline constexpr size_t kMaxNameLength = 24;

    enum class MessageType : uint8_t
    {
        Hello   = 1,  // client -> host, opens the handshake
        Welcome = 2,  // host -> client, assigns a peer id
        Reject  = 3,  // host -> client, handshake refused
        Bye     = 4,  // either direction, clean disconnect
        Ping    = 5,  // either direction, carries a send timestamp
        Pong    = 6,  // echoes a Ping's timestamp back
        State   = 7,  // unreliable player snapshot, ~20 Hz
        Roster  = 8,  // host -> clients, who is in the session
        Event   = 9,  // reliable one-shot notification
        Ack     = 10, // acknowledges one reliable message
    };

    enum class RejectReason : uint8_t
    {
        ProtocolMismatch = 1,
        WrongPassword    = 2,
        SessionFull      = 3,
        Banned           = 4,
    };

    // Bits describing what a peer is doing. Only the ones this mod can actually
    // observe are ever set; the rest are reserved so adding them later does not
    // need a protocol bump.
    enum StateFlag : uint16_t
    {
        SF_HasPosition = 1 << 0,
        SF_Running     = 1 << 1,
        SF_Dead        = 1 << 2,
        SF_Loading     = 1 << 3,
        SF_InMenu      = 1 << 4,
        SF_Crouching   = 1 << 5,  // reserved: needs more reverse engineering
        SF_Aiming      = 1 << 6,  // reserved: needs more reverse engineering
        SF_Spotted     = 1 << 7,  // reserved: needs an AI-state hook
    };

    enum class EventType : uint8_t
    {
        Chat              = 1,
        Marker            = 2,  // "look here" ping, carries a world position
        ActorDied         = 3,  // an NPC died in the sender's world
        LevelChanged      = 4,
        CheckpointReached = 5,
        ObjectiveNote     = 6,  // free text from the rules layer
        PlayerJoined      = 7,
        PlayerLeft        = 8,
        SessionReset      = 9,
    };

    // ---- Message payloads -------------------------------------------------
    //
    // These are the in-memory shapes. Serialisation is explicit (see
    // Protocol.cpp) rather than a memcpy of the struct, because struct padding
    // is a compiler decision and a wire format must not be.

    struct Header
    {
        uint32_t magic           = kMagic;
        uint16_t protocolVersion = kProtocolVersion;
        uint8_t  type            = 0;
        uint8_t  senderId        = kInvalidPeerId;
        uint32_t sessionId       = 0;
        uint16_t reliableSeq     = 0;  // 0 for unreliable messages

        static constexpr size_t kSize = 14;
    };

    struct HelloMessage
    {
        uint32_t    passwordHash     = 0;
        uint32_t    buildFingerprint = 0;  // so mismatched game builds are visible
        std::string name;
    };

    struct WelcomeMessage
    {
        uint8_t  assignedPeerId = kInvalidPeerId;
        uint32_t sessionId      = 0;
        uint64_t sessionStartMs = 0;  // host clock, for the shared timer
        uint32_t ruleFlags      = 0;
        uint64_t hostNowMs      = 0;  // host clock at send, for offset estimation
    };

    // One player's replicated state. Sent unreliably and often; losing one is
    // always cheaper than waiting for it.
    struct StateMessage
    {
        uint8_t  peerId      = kInvalidPeerId;
        uint32_t timestampMs = 0;  // sender's clock
        float    x = 0.f, y = 0.f, z = 0.f;
        float    yaw = 0.f;                 // radians, world Z-up
        float    vx = 0.f, vy = 0.f, vz = 0.f;
        uint16_t flags   = 0;
        uint8_t  level   = 0xFF;  // chapter index, 0xFF when unknown
        uint8_t  section = 0xFF;
        uint16_t score   = 0;
    };

    struct RosterEntry
    {
        uint8_t     peerId = kInvalidPeerId;
        std::string name;
        uint8_t     level   = 0xFF;
        uint8_t     section = 0xFF;
        uint16_t    flags   = 0;
    };

    struct RosterMessage
    {
        std::vector<RosterEntry> entries;
    };

    struct EventMessage
    {
        EventType   type         = EventType::Chat;
        uint8_t     originPeerId = kInvalidPeerId;
        uint32_t    timestampMs  = 0;
        float       x = 0.f, y = 0.f, z = 0.f;  // meaningful for Marker/ActorDied
        std::string text;
    };

    // ---- Buffer helpers ---------------------------------------------------
    //
    // Bounds-checked both ways. A malformed or truncated packet must make
    // Read* return false, never read past the end: this data arrives from the
    // network and some of it will be hostile or corrupt.

    class Writer
    {
    public:
        explicit Writer(uint8_t* buffer, size_t capacity)
            : m_buffer(buffer), m_capacity(capacity)
        {
        }

        bool WriteU8(uint8_t value);
        bool WriteU16(uint16_t value);
        bool WriteU32(uint32_t value);
        bool WriteU64(uint64_t value);
        bool WriteFloat(float value);
        bool WriteString(const std::string& value, size_t maxLength);

        size_t Size() const { return m_position; }
        bool   Failed() const { return m_failed; }

    private:
        bool WriteRaw(const void* data, size_t size);

        uint8_t* m_buffer   = nullptr;
        size_t   m_capacity = 0;
        size_t   m_position = 0;
        bool     m_failed   = false;
    };

    class Reader
    {
    public:
        explicit Reader(const uint8_t* buffer, size_t size)
            : m_buffer(buffer), m_size(size)
        {
        }

        bool ReadU8(uint8_t& value);
        bool ReadU16(uint16_t& value);
        bool ReadU32(uint32_t& value);
        bool ReadU64(uint64_t& value);
        bool ReadFloat(float& value);
        bool ReadString(std::string& value, size_t maxLength);

        size_t Remaining() const { return m_size - m_position; }
        bool   Failed() const { return m_failed; }

    private:
        bool ReadRaw(void* data, size_t size);

        const uint8_t* m_buffer   = nullptr;
        size_t         m_size     = 0;
        size_t         m_position = 0;
        bool           m_failed   = false;
    };

    // ---- Encode / decode --------------------------------------------------

    bool WriteHeader(Writer& writer, const Header& header);
    bool ReadHeader(Reader& reader, Header& header);

    bool WriteHello(Writer& writer, const HelloMessage& message);
    bool ReadHello(Reader& reader, HelloMessage& message);

    bool WriteWelcome(Writer& writer, const WelcomeMessage& message);
    bool ReadWelcome(Reader& reader, WelcomeMessage& message);

    bool WriteState(Writer& writer, const StateMessage& message);
    bool ReadState(Reader& reader, StateMessage& message);

    bool WriteRoster(Writer& writer, const RosterMessage& message);
    bool ReadRoster(Reader& reader, RosterMessage& message);

    bool WriteEvent(Writer& writer, const EventMessage& message);
    bool ReadEvent(Reader& reader, EventMessage& message);

    // Not a security measure and not presented as one: it keeps a stray
    // stranger who guessed the port out of the session, nothing more. The
    // whole protocol is plaintext UDP.
    uint32_t HashPassword(const std::string& password);
}
