#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Glacier/Math/SVector3.h>

#include "Net/Session.h"
#include "Threading/Lock.h"

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

    // A point somebody wanted the others to look at, or where somebody went
    // down. Fades on its own so nobody has to clean up after it.
    struct WorldMarker
    {
        SVector3    position{};
        std::string label;
        uint8_t     peerId    = Net::kInvalidPeerId;
        float       remaining = 0.f;
    };

    struct AvatarSettings
    {
        bool  drawInWorld    = true;
        bool  drawNameplates = true;
        bool  drawBeam       = true;
        bool  drawDistance   = true;
        float maxDrawDistance = 250.f;   // metres; beyond this only the panel lists them
        float beamHeight      = 2.1f;

        bool  drawMarkers     = true;
        float markerSeconds   = 20.f;

        // Instinct is the game's own see-through-walls view, and it already
        // draws points of interest in it. A teammate is one, so this puts them
        // there: brighter, and out to any distance, for as long as it is held.
        //
        // Only-in-instinct is off by default because it hides teammates the
        // rest of the time, which is a big change to make on the strength of a
        // reading nobody has confirmed yet. Turn it on and co-op stops being an
        // always-on wallhack, which is the better game.
        bool  instinctHighlight = true;
        bool  instinctOnly      = false;
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

        // Told once a frame, before drawing. Kept here rather than read here
        // because Draw3D runs on the render thread and the controller belongs
        // to the game thread.
        void SetInstinct(bool active) { m_instinctActive = active; }

        // Markers are owned here because this is what already holds the
        // renderer and the world-to-screen plumbing.
        void AddMarker(const SVector3& position, const std::string& label, uint8_t peerId);
        void TickMarkers(float deltaSeconds);

        // Copies, both of them. The game thread rebuilds these vectors every
        // frame while the render thread draws from them inside Present, and a
        // vector reallocating under an iterator is heap corruption - the same
        // shape as the crash the log lock fixed. A copy of eight avatars is
        // nothing; a dangling string is everything.
        std::vector<WorldMarker> Markers() const;
        std::vector<AvatarView>  Views() const;

        // Names arrive from the network, so they are untrusted input on their
        // way to a renderer that throws on a glyph it does not have. Anything
        // drawn goes through here first.
        static std::string SanitiseForDisplay(const std::string& text, size_t maxLength);

    private:
        void DrawMarkers(const class DirectXRenderer& renderer,
                         const std::vector<WorldMarker>& markers) const;

        AvatarSettings           m_settings;

        // Written by the game thread (Update, AddMarker, TickMarkers), read by
        // the render thread (Draw3D and the panel). Every touch on either side
        // holds this.
        mutable Threading::Lock  m_lock;
        std::vector<AvatarView>  m_views;
        std::vector<WorldMarker> m_markers;

        bool m_instinctActive = false;
    };
}
