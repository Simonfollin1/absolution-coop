#pragma once

#include <cstdint>
#include <string>
#include <thread>

#include "Threading/Lock.h"

namespace Coop::Net
{
    // Asks the router to open the host's port, so nobody has to.
    //
    // Hosting needs one reachable UDP port and nothing else, and until now that
    // meant the host going into their router's admin pages, finding the port
    // forwarding tab, and getting the protocol right - the field defaults to
    // TCP on most of them, and every byte this mod sends is UDP. That is a lot
    // of ways to fail before the game has even started.
    //
    // Routers have offered to do it themselves for twenty years. This asks.
    //
    // Deliberately not Windows' IUPnPNAT: that COM interface depends on the
    // "Internet Gateway Device Discovery and Control Client" service, which is
    // disabled by default on current Windows and cannot be turned on from
    // inside a game. The protocol underneath is simple enough to speak
    // directly - a multicast search, one HTTP GET, one SOAP POST - and doing so
    // needs nothing but the socket library the mod already links.
    //
    // Every part of it is allowed to fail. A router that does not answer, or
    // answers and refuses, costs a few seconds on a worker thread and changes
    // nothing else: hosting still works on a LAN or over a VPN, which is what
    // most people will use anyway.
    class PortMapper
    {
    public:
        enum class State : uint8_t
        {
            Idle,       // nothing asked for
            Working,    // searching or negotiating
            Mapped,     // the router opened it
            Failed,     // it did not, and Note() says why
        };

        ~PortMapper();

        PortMapper() = default;
        PortMapper(const PortMapper&) = delete;
        PortMapper& operator=(const PortMapper&) = delete;

        // Starts the work on its own thread and returns immediately. Discovery
        // waits seconds for replies, which is not something to do on the thread
        // the game is drawing from.
        void RequestMap(uint16_t port);

        // Removes the mapping if one was made, and waits for the worker.
        // Leaving a hole open in somebody's router after the game has closed is
        // not ours to do.
        void Release();

        State       GetState() const;
        std::string Note() const;

        // "81.2.3.4:47474" once the router has told us, which is the thing the
        // host actually needs to hand to the other player. Empty until then.
        std::string ExternalAddress() const;

    private:
        void Work(uint16_t port);

        mutable Threading::Lock m_lock;

        std::thread m_worker;

        State       m_state = State::Idle;
        std::string m_note;
        std::string m_external;

        // What to undo, filled in once the router has agreed.
        std::string m_controlUrl;
        std::string m_serviceType;
        uint16_t    m_mappedPort = 0;
    };
}
