#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>

#include <SDK.h>
#include <Renderer/DirectXRenderer.h>
#include <Glacier/Math/SVector2.h>
#include <Glacier/Math/SVector4.h>
#include <Glacier/ZString.h>

#include "Game/PeerAvatars.h"

namespace Coop::Game
{
    namespace
    {
        // Render peers this far in the past, so there is always a pair of
        // snapshots to interpolate between rather than an extrapolation past
        // the newest one. One and a bit send intervals: enough to absorb a
        // single lost packet, little enough that nobody feels dragged.
        constexpr float kInterpolationDelayMs = 120.f;

        // Past this the last snapshot is too old to interpolate from and the
        // avatar holds still instead of drifting somewhere it never was.
        constexpr float kMaxExtrapolationMs = 250.f;

        SVector4 ColourForPeer(uint8_t peerId, bool stale)
        {
            // Distinct, readable against Absolution's palette, and stable per
            // peer so a player keeps their colour for the session.
            static const SVector4 palette[] = {
                { 1.00f, 0.79f, 0.20f, 1.f },  // amber
                { 0.35f, 0.78f, 1.00f, 1.f },  // ice
                { 0.55f, 0.92f, 0.45f, 1.f },  // green
                { 1.00f, 0.45f, 0.60f, 1.f },  // rose
                { 0.75f, 0.60f, 1.00f, 1.f },  // violet
                { 1.00f, 0.62f, 0.35f, 1.f },  // orange
                { 0.55f, 0.95f, 0.90f, 1.f },  // teal
                { 0.90f, 0.90f, 0.90f, 1.f },  // bone
            };

            SVector4 colour = palette[peerId % (sizeof(palette) / sizeof(palette[0]))];

            if (stale)
            {
                colour.w = 0.35f;
            }

            return colour;
        }

        float LerpAngle(float from, float to, float t)
        {
            constexpr float kTwoPi = 6.28318530718f;

            float difference = std::fmod(to - from + kTwoPi + 3.14159265f, kTwoPi) - 3.14159265f;

            return from + difference * t;
        }
    }

    std::string PeerAvatars::SanitiseForDisplay(const std::string& text, size_t maxLength)
    {
        // The SDK's text renderer throws std::runtime_error for a glyph that is
        // not in its font sheet, and it does that inside the Present hook,
        // where there is nothing to catch it. Peer names come off the wire.
        // Printable ASCII only, therefore, and never mind that it is blunt.
        std::string result;
        result.reserve(std::min(text.size(), maxLength));

        for (const char character : text)
        {
            if (result.size() >= maxLength)
            {
                break;
            }

            const auto byte = static_cast<unsigned char>(character);

            if (byte >= 0x20 && byte <= 0x7E)
            {
                result.push_back(character);
            }
        }

        if (result.empty())
        {
            result = "player";
        }

        return result;
    }

    void PeerAvatars::Update(const std::vector<Net::PeerSnapshot>& peers,
                             const SVector3& localPosition,
                             uint8_t localLevel, uint8_t localSection)
    {
        m_views.clear();
        m_views.reserve(peers.size());

        const uint64_t nowMs = GetTickCount64();

        for (const Net::PeerSnapshot& peer : peers)
        {
            AvatarView view;

            view.peerId  = peer.peerId;
            view.name    = SanitiseForDisplay(peer.name, Net::kMaxNameLength);
            view.stale   = peer.stale;
            view.pingMs  = peer.roundTripMs;
            view.level   = peer.current.level;
            view.section = peer.current.section;
            view.loading = (peer.current.flags & Net::SF_Loading) != 0;
            view.dead    = (peer.current.flags & Net::SF_Dead) != 0;
            view.running = (peer.current.flags & Net::SF_Running) != 0;
            view.yaw     = peer.current.yaw;

            view.sameArea = view.level != 0xFF
                         && view.level == localLevel
                         && view.section == localSection;

            const bool hasPosition = (peer.current.flags & Net::SF_HasPosition) != 0;

            if (!hasPosition || peer.currentReceivedMs == 0)
            {
                m_views.push_back(std::move(view));
                continue;
            }

            // Where we want to show them: slightly behind the newest packet.
            const float renderAgeMs =
                static_cast<float>(nowMs - peer.currentReceivedMs) - kInterpolationDelayMs;

            SVector3 position(peer.current.x, peer.current.y, peer.current.z);
            float    yaw = peer.current.yaw;

            if (renderAgeMs < 0.f && peer.previousReceivedMs != 0)
            {
                // Between the two snapshots we hold. This is the normal path.
                const float span = static_cast<float>(
                    peer.currentReceivedMs - peer.previousReceivedMs);

                if (span > 0.f)
                {
                    const float t = std::clamp(1.f + renderAgeMs / span, 0.f, 1.f);

                    position.x = peer.previous.x + (peer.current.x - peer.previous.x) * t;
                    position.y = peer.previous.y + (peer.current.y - peer.previous.y) * t;
                    position.z = peer.previous.z + (peer.current.z - peer.previous.z) * t;

                    yaw = LerpAngle(peer.previous.yaw, peer.current.yaw, t);
                }
            }
            else if (renderAgeMs > 0.f && renderAgeMs < kMaxExtrapolationMs)
            {
                // Ahead of the newest packet by less than a couple of frames:
                // carry the last known velocity. Any further than that and
                // guessing does more harm than standing still.
                const float seconds = renderAgeMs * 0.001f;

                position.x += peer.current.vx * seconds;
                position.y += peer.current.vy * seconds;
                position.z += peer.current.vz * seconds;
            }

            view.position = position;
            view.yaw      = yaw;

            const float dx = position.x - localPosition.x;
            const float dy = position.y - localPosition.y;
            const float dz = position.z - localPosition.z;

            view.distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            m_views.push_back(std::move(view));
        }

        std::sort(m_views.begin(), m_views.end(),
            [](const AvatarView& a, const AvatarView& b) { return a.peerId < b.peerId; });
    }

    void PeerAvatars::Draw3D() const
    {
        if (!m_settings.drawInWorld)
        {
            return;
        }

        const std::shared_ptr<DirectXRenderer> renderer = SDK::GetInstance().GetDirectXRenderer();

        if (!renderer)
        {
            return;
        }

        for (const AvatarView& view : m_views)
        {
            // Somebody in another chapter has a position, but it is a position
            // in a level we are not in. Drawing it would be worse than useless.
            if (!view.sameArea || view.loading)
            {
                continue;
            }

            if (view.distance > m_settings.maxDrawDistance)
            {
                continue;
            }

            const SVector4 colour = ColourForPeer(view.peerId, view.stale);

            const SVector3 feet(view.position.x, view.position.y, view.position.z);
            const SVector3 head(view.position.x, view.position.y,
                                view.position.z + m_settings.beamHeight);

            if (m_settings.drawBeam)
            {
                renderer->DrawLine3D(feet, head, colour, colour);

                // A short spur in the direction they are facing, so you can
                // tell at a glance which way a teammate is looking.
                const SVector3 facing(
                    view.position.x + std::cos(view.yaw) * 0.8f,
                    view.position.y + std::sin(view.yaw) * 0.8f,
                    view.position.z + 0.1f);

                renderer->DrawLine3D(feet, facing, colour, colour);
            }

            if (!m_settings.drawNameplates)
            {
                continue;
            }

            SVector2 screen{};

            const SVector3 label(view.position.x, view.position.y,
                                 view.position.z + m_settings.beamHeight + 0.25f);

            if (!renderer->WorldToScreen(label, screen))
            {
                continue;
            }

            std::string text = view.name;

            if (view.dead)
            {
                text += " (down)";
            }

            if (m_settings.drawDistance)
            {
                text += std::format(" {:.0f}m", view.distance);
            }

            // Already ASCII: name went through SanitiseForDisplay and the rest
            // is generated here.
            renderer->DrawText2D(ZString(text.c_str()), screen, colour, 0.f, 0.5f);
        }
    }
}
