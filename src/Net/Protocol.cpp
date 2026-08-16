#include "Net/Protocol.h"

namespace Coop::Net
{
    // ---- Writer -----------------------------------------------------------

    bool Writer::WriteRaw(const void* data, size_t size)
    {
        if (m_failed || m_position + size > m_capacity)
        {
            m_failed = true;
            return false;
        }

        std::memcpy(m_buffer + m_position, data, size);
        m_position += size;

        return true;
    }

    bool Writer::WriteU8(uint8_t value)   { return WriteRaw(&value, sizeof(value)); }
    bool Writer::WriteU16(uint16_t value) { return WriteRaw(&value, sizeof(value)); }
    bool Writer::WriteU32(uint32_t value) { return WriteRaw(&value, sizeof(value)); }
    bool Writer::WriteU64(uint64_t value) { return WriteRaw(&value, sizeof(value)); }
    bool Writer::WriteFloat(float value)  { return WriteRaw(&value, sizeof(value)); }

    bool Writer::WriteString(const std::string& value, size_t maxLength)
    {
        const size_t length = value.size() < maxLength ? value.size() : maxLength;

        if (!WriteU8(static_cast<uint8_t>(length)))
        {
            return false;
        }

        return length == 0 || WriteRaw(value.data(), length);
    }

    // ---- Reader -----------------------------------------------------------

    bool Reader::ReadRaw(void* data, size_t size)
    {
        if (m_failed || m_position + size > m_size)
        {
            m_failed = true;
            return false;
        }

        std::memcpy(data, m_buffer + m_position, size);
        m_position += size;

        return true;
    }

    bool Reader::ReadU8(uint8_t& value)   { return ReadRaw(&value, sizeof(value)); }
    bool Reader::ReadU16(uint16_t& value) { return ReadRaw(&value, sizeof(value)); }
    bool Reader::ReadU32(uint32_t& value) { return ReadRaw(&value, sizeof(value)); }
    bool Reader::ReadU64(uint64_t& value) { return ReadRaw(&value, sizeof(value)); }
    bool Reader::ReadFloat(float& value)  { return ReadRaw(&value, sizeof(value)); }

    bool Reader::ReadString(std::string& value, size_t maxLength)
    {
        uint8_t length = 0;

        if (!ReadU8(length))
        {
            return false;
        }

        // A length longer than the caller allows is a malformed packet, not
        // something to truncate and carry on with.
        if (length > maxLength)
        {
            m_failed = true;
            return false;
        }

        value.assign(length, '\0');

        return length == 0 || ReadRaw(value.data(), length);
    }

    // ---- Header -----------------------------------------------------------

    bool WriteHeader(Writer& writer, const Header& header)
    {
        writer.WriteU32(header.magic);
        writer.WriteU16(header.protocolVersion);
        writer.WriteU8(header.type);
        writer.WriteU8(header.senderId);
        writer.WriteU32(header.sessionId);
        writer.WriteU16(header.reliableSeq);

        return !writer.Failed();
    }

    bool ReadHeader(Reader& reader, Header& header)
    {
        reader.ReadU32(header.magic);
        reader.ReadU16(header.protocolVersion);
        reader.ReadU8(header.type);
        reader.ReadU8(header.senderId);
        reader.ReadU32(header.sessionId);
        reader.ReadU16(header.reliableSeq);

        return !reader.Failed();
    }

    // ---- Hello / Welcome --------------------------------------------------

    bool WriteHello(Writer& writer, const HelloMessage& message)
    {
        writer.WriteU32(message.passwordHash);
        writer.WriteU32(message.buildFingerprint);
        writer.WriteString(message.name, kMaxNameLength);

        return !writer.Failed();
    }

    bool ReadHello(Reader& reader, HelloMessage& message)
    {
        reader.ReadU32(message.passwordHash);
        reader.ReadU32(message.buildFingerprint);
        reader.ReadString(message.name, kMaxNameLength);

        return !reader.Failed();
    }

    bool WriteWelcome(Writer& writer, const WelcomeMessage& message)
    {
        writer.WriteU8(message.assignedPeerId);
        writer.WriteU32(message.sessionId);
        writer.WriteU64(message.sessionStartMs);
        writer.WriteU32(message.ruleFlags);
        writer.WriteU64(message.hostNowMs);

        return !writer.Failed();
    }

    bool ReadWelcome(Reader& reader, WelcomeMessage& message)
    {
        reader.ReadU8(message.assignedPeerId);
        reader.ReadU32(message.sessionId);
        reader.ReadU64(message.sessionStartMs);
        reader.ReadU32(message.ruleFlags);
        reader.ReadU64(message.hostNowMs);

        return !reader.Failed();
    }

    // ---- State ------------------------------------------------------------

    bool WriteState(Writer& writer, const StateMessage& message)
    {
        writer.WriteU8(message.peerId);
        writer.WriteU32(message.timestampMs);
        writer.WriteFloat(message.x);
        writer.WriteFloat(message.y);
        writer.WriteFloat(message.z);
        writer.WriteFloat(message.yaw);
        writer.WriteFloat(message.vx);
        writer.WriteFloat(message.vy);
        writer.WriteFloat(message.vz);
        writer.WriteU16(message.flags);
        writer.WriteU8(message.level);
        writer.WriteU8(message.section);
        writer.WriteU16(message.score);

        return !writer.Failed();
    }

    bool ReadState(Reader& reader, StateMessage& message)
    {
        reader.ReadU8(message.peerId);
        reader.ReadU32(message.timestampMs);
        reader.ReadFloat(message.x);
        reader.ReadFloat(message.y);
        reader.ReadFloat(message.z);
        reader.ReadFloat(message.yaw);
        reader.ReadFloat(message.vx);
        reader.ReadFloat(message.vy);
        reader.ReadFloat(message.vz);
        reader.ReadU16(message.flags);
        reader.ReadU8(message.level);
        reader.ReadU8(message.section);
        reader.ReadU16(message.score);

        return !reader.Failed();
    }

    // ---- Roster -----------------------------------------------------------

    bool WriteRoster(Writer& writer, const RosterMessage& message)
    {
        const size_t count = message.entries.size() < kMaxPeers
            ? message.entries.size()
            : kMaxPeers;

        writer.WriteU8(static_cast<uint8_t>(count));

        for (size_t i = 0; i < count; ++i)
        {
            const RosterEntry& entry = message.entries[i];

            writer.WriteU8(entry.peerId);
            writer.WriteString(entry.name, kMaxNameLength);
            writer.WriteU8(entry.level);
            writer.WriteU8(entry.section);
            writer.WriteU16(entry.flags);
        }

        return !writer.Failed();
    }

    bool ReadRoster(Reader& reader, RosterMessage& message)
    {
        uint8_t count = 0;

        if (!reader.ReadU8(count) || count > kMaxPeers)
        {
            return false;
        }

        message.entries.clear();
        message.entries.reserve(count);

        for (uint8_t i = 0; i < count; ++i)
        {
            RosterEntry entry;

            reader.ReadU8(entry.peerId);
            reader.ReadString(entry.name, kMaxNameLength);
            reader.ReadU8(entry.level);
            reader.ReadU8(entry.section);
            reader.ReadU16(entry.flags);

            if (reader.Failed())
            {
                return false;
            }

            message.entries.push_back(std::move(entry));
        }

        return true;
    }

    // ---- Event ------------------------------------------------------------

    // Long enough for a chat line, short enough that an Event still fits in one
    // packet with room to spare.
    inline constexpr size_t kMaxEventTextLength = 160;

    bool WriteEvent(Writer& writer, const EventMessage& message)
    {
        writer.WriteU8(static_cast<uint8_t>(message.type));
        writer.WriteU8(message.originPeerId);
        writer.WriteU32(message.timestampMs);
        writer.WriteFloat(message.x);
        writer.WriteFloat(message.y);
        writer.WriteFloat(message.z);
        writer.WriteString(message.text, kMaxEventTextLength);

        return !writer.Failed();
    }

    bool ReadEvent(Reader& reader, EventMessage& message)
    {
        uint8_t type = 0;

        reader.ReadU8(type);
        reader.ReadU8(message.originPeerId);
        reader.ReadU32(message.timestampMs);
        reader.ReadFloat(message.x);
        reader.ReadFloat(message.y);
        reader.ReadFloat(message.z);
        reader.ReadString(message.text, kMaxEventTextLength);

        if (reader.Failed())
        {
            return false;
        }

        // Anything outside the enum is a peer speaking a dialect we do not
        // know. Dropping it is correct; guessing is not.
        if (type < static_cast<uint8_t>(EventType::Chat) ||
            type > static_cast<uint8_t>(EventType::SessionReset))
        {
            return false;
        }

        message.type = static_cast<EventType>(type);

        return true;
    }

    // ---- Password ---------------------------------------------------------

    uint32_t HashPassword(const std::string& password)
    {
        // FNV-1a. Chosen for being three lines and dependency-free; see the
        // header for what this is and is not for.
        uint32_t hash = 2166136261u;

        for (const char character : password)
        {
            hash ^= static_cast<uint8_t>(character);
            hash *= 16777619u;
        }

        return hash;
    }
}
