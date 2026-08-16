#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Glacier/Math/SVector3.h>

#include "Net/Session.h"

namespace Coop::Game
{
    // One peer, resolved to where they should be drawn this frame.
    struct AvatarView
    {
        uint8_t     peerId = Net::kInvalidPeerId;
        std::string name;
        SVector3    position{};
        float       yaw       = 0.f;
        float       distance  = 0.f;
        bool        sameArea  = false;   // same chapter and section as us
        bool        stale     = false;
        bool        loading   = false;
        bool        dead      = false;
        bool        running   = false;
        uint8_t     level     = 0xFF;
        uint8_t     section   = 0xFF;
        uint32_t    pingMs    = 0;
    };

    struct AvatarSettings
    {
        bool  drawInWorld    = true;
        bool  drawNameplates = true;
        bool  drawBeam       = true;
        bool  drawDistance   = true;
        float maxDrawDistance = 250.f;   // metres; beyond this only the panel lists them
        float beamHeight      = 2.1f;
    };

    // Turns the session's raw snapshots into something drawable, and draws it.
    //
    // Rendering a remote player is the whole of "seeing each other" at this
    // stage. A spawned character standing in the world is a much better answer
    // and is what comes next, but it needs an actor spawn and animation
    // control, and neither of those can be verified without the game running.
    // A marker can, and it is honest about being a marker.
    class PeerAvatars
    {
    public:
        void SetSettings(const AvatarSettings& settings) { m_settings = settings; }
        AvatarSettings& Settings() { return m_settings; }
        const AvatarSettings& Settings() const { return m_settings; }

        // Interpolates every peer to render time. localLevel/localSection say
        // which of them are in the same place as us.
        void Update(const std::vector<Net::PeerSnapshot>& peers,
                    const SVector3& localPosition,
                    uint8_t localLevel, uint8_t localSection);

        void Draw3D() const;

        const std::vector<AvatarView>& Views() const { return m_views; }

        // Names arrive from the network, so they are untrusted input on their
        // way to a renderer that throws on a glyph it does not have. Anything
        // drawn goes through here first.
        static std::string SanitiseForDisplay(const std::string& text, size_t maxLength);

    private:
        AvatarSettings          m_settings;
        std::vector<AvatarView> m_views;
    };
}
