// Golden packets for the bot's self test.
//
// Compiles against the mod's real codec - mods/Coop/src/Net/Protocol.cpp, the
// same file the game links - and prints one sample of every message as
// name=hex. coopbot.py --selftest --golden <file> then has to produce the
// identical bytes and read them back to the identical values. That is the
// whole contract: the Python in the bot is a copy of the C++, and this is the
// machine that catches the copy drifting.
//
// Every constant here matches GOLDEN_SAMPLES in coopbot.py. Change one side
// and the build fails until the other follows.
//
// Built by the protocol job in the workflow:
//   g++ -std=c++20 -I mods/Coop/include tools/coopbot/goldengen.cpp \
//       mods/Coop/src/Net/Protocol.cpp -o goldengen

#include <cstdio>

#include "Net/Protocol.h"

using namespace Coop::Net;

namespace
{
    void Print(const char* name, const uint8_t* data, size_t size)
    {
        std::printf("%s=", name);

        for (size_t i = 0; i < size; ++i)
        {
            std::printf("%02x", data[i]);
        }

        std::printf("\n");
    }
}

int main()
{
    uint8_t buffer[kMaxPacketSize];

    {
        Writer writer(buffer, sizeof(buffer));

        Header header;
        header.type        = static_cast<uint8_t>(MessageType::Event);
        header.senderId    = 3;
        header.sessionId   = 0xDEADBEEF;
        header.reliableSeq = 0x1234;

        WriteHeader(writer, header);
        Print("header", buffer, writer.Size());
    }

    {
        Writer writer(buffer, sizeof(buffer));

        HelloMessage hello;
        hello.passwordHash     = HashPassword("hemligt");
        hello.buildFingerprint = 0x11223344;
        hello.name             = "Agent 47";

        WriteHello(writer, hello);
        Print("hello", buffer, writer.Size());
    }

    {
        Writer writer(buffer, sizeof(buffer));

        WelcomeMessage welcome;
        welcome.assignedPeerId = 2;
        welcome.sessionId      = 0xCAFEBABE;
        welcome.sessionStartMs = 123456789;
        welcome.ruleFlags      = 7;
        welcome.hostNowMs      = 987654321;

        WriteWelcome(writer, welcome);
        Print("welcome", buffer, writer.Size());
    }

    {
        Writer writer(buffer, sizeof(buffer));

        StateMessage state;
        state.peerId      = 1;
        state.timestampMs = 100000;
        state.x           = 1.5f;
        state.y           = -2.25f;
        state.z           = 3.75f;
        state.yaw         = 0.5f;
        state.vx          = 0.125f;
        state.vy          = 0.25f;
        state.vz          = -0.375f;
        state.flags       = 3;
        state.level       = 4;
        state.section     = 2;
        state.score       = 41;

        WriteState(writer, state);
        Print("state", buffer, writer.Size());
    }

    {
        Writer writer(buffer, sizeof(buffer));

        RosterMessage roster;

        RosterEntry host;
        host.peerId = 0;
        host.name   = "Host";

        RosterEntry agent;
        agent.peerId  = 5;
        agent.name    = "Agent 47";
        agent.level   = 4;
        agent.section = 2;
        agent.flags   = 3;

        roster.entries.push_back(host);
        roster.entries.push_back(agent);

        WriteRoster(writer, roster);
        Print("roster", buffer, writer.Size());
    }

    {
        Writer writer(buffer, sizeof(buffer));

        EventMessage event;
        event.type         = EventType::LevelChanged;
        event.originPeerId = 1;
        event.timestampMs  = 4242;
        event.x            = 13.f;
        event.text         = "assembly:/scenes/m04/scene_rfyl.entity";

        WriteEvent(writer, event);
        Print("event", buffer, writer.Size());
    }

    return 0;
}
