#include "UI/DiagnosticsPanel.h"

#include "Memory/GameOffsets.h"

#include <imgui.h>

#include <Windows.h>

namespace
{
    // A 60fps frame, which is what every percentage here is a share of.
    constexpr float FRAME_BUDGET_US = 16666.f;

    // Thresholds for the colouring, chosen so the colour means something
    // rather than being decoration.
    //
    // A twentieth of a percent is roughly ten microseconds, which is the floor
    // at which a mod is doing anything measurable at all. One percent - about
    // 170 microseconds - is where it starts being worth an argument. Five is
    // where it would show up as lost frames.
    constexpr float SHARE_QUIET   = 0.05f;
    constexpr float SHARE_NOTABLE = 1.0f;
    constexpr float SHARE_LOUD    = 5.0f;

    const ImVec4 COLOR_QUIET  (0.55f, 0.78f, 0.55f, 1.f);
    const ImVec4 COLOR_NORMAL (0.85f, 0.85f, 0.86f, 1.f);
    const ImVec4 COLOR_NOTABLE(1.00f, 0.78f, 0.35f, 1.f);
    const ImVec4 COLOR_LOUD   (1.00f, 0.45f, 0.40f, 1.f);
    const ImVec4 COLOR_DIM    (0.60f, 0.60f, 0.62f, 1.f);

    ImVec4 ColorForShare(float sharePercent)
    {
        if (sharePercent >= SHARE_LOUD)    return COLOR_LOUD;
        if (sharePercent >= SHARE_NOTABLE) return COLOR_NOTABLE;
        if (sharePercent <= SHARE_QUIET)   return COLOR_QUIET;

        return COLOR_NORMAL;
    }

    long long Now()
    {
        LARGE_INTEGER value = {};
        QueryPerformanceCounter(&value);

        return value.QuadPart;
    }

    double SecondsSince(long long start)
    {
        static long long frequency = []
        {
            LARGE_INTEGER value = {};
            QueryPerformanceFrequency(&value);

            return value.QuadPart > 0 ? value.QuadPart : 1;
        }();

        const long long elapsed = Now() - start;

        return elapsed > 0 ? static_cast<double>(elapsed) / static_cast<double>(frequency) : 0.0;
    }
}

void UI::DiagnosticsPanel::Configure(const char* modName)
{
    m_modName = modName ? modName : "Mod";
}

void UI::DiagnosticsPanel::ResetRates(const Diag::FrameCost& update,
                                      const Diag::FrameCost& drawUi,
                                      const Diag::FrameCost& draw3d)
{
    m_since      = Now();
    m_baseUpdate = update.TotalFrames();
    m_baseDrawUi = drawUi.TotalFrames();
    m_baseDraw3d = draw3d.TotalFrames();
    m_baseProbes = GameOffsets::GetProbeSyscalls();
    m_baseHits   = GameOffsets::GetProbeCacheHits();
}

void UI::DiagnosticsPanel::CostRow(const Diag::FrameCost& cost)
{
    const float average = cost.AverageMicros();
    const float worst   = cost.WorstMicros();
    const float share   = average / FRAME_BUDGET_US * 100.f;

    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(cost.Label());

    ImGui::TableNextColumn();
    ImGui::Text("%.1f", average);

    ImGui::TableNextColumn();
    ImGui::Text("%.1f", worst);

    ImGui::TableNextColumn();
    ImGui::TextColored(ColorForShare(share), "%.3f %%", share);
}

void UI::DiagnosticsPanel::Stat(const char* label, int value, const char* note)
{
    ImGui::Text("%s:", label);
    ImGui::SameLine(260.f);
    ImGui::Text("%d", value);

    if (note)
    {
        ImGui::SameLine();
        ImGui::TextColored(COLOR_DIM, "%s", note);
    }
}

void UI::DiagnosticsPanel::Stat(const char* label, const char* value, const char* note)
{
    ImGui::Text("%s:", label);
    ImGui::SameLine(260.f);
    ImGui::TextUnformatted(value ? value : "-");

    if (note)
    {
        ImGui::SameLine();
        ImGui::TextColored(COLOR_DIM, "%s", note);
    }
}

void UI::DiagnosticsPanel::Render(const Diag::FrameCost& update,
                                  const Diag::FrameCost& drawUi,
                                  const Diag::FrameCost& draw3d)
{
    if (m_since == 0)
    {
        ResetRates(update, drawUi, draw3d);
    }

    ImGui::TextColored(COLOR_DIM,
        "Everything on this tab is measured, not estimated. Screenshot it.");

    ImGui::Spacing();

    // ---- What this is running on ----

    ImGui::SeparatorText("Build");

    Stat("Mod", m_modName);
    Stat("Game build", GameOffsets::GetBuildName(),
        GameOffsets::IsSupported() ? "" : "  offsets disabled");

    ImGui::Spacing();

    // ---- The costs ----

    ImGui::SeparatorText("Per-frame cost");

    ImGui::TextColored(COLOR_DIM,
        "Microseconds this mod spends in each path, and what that is as a share");
    ImGui::TextColored(COLOR_DIM,
        "of one 60fps frame. Average is smoothed; worst resets each minute.");

    ImGui::Spacing();

    if (ImGui::BeginTable("Diag_Costs", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Avg us");
        ImGui::TableSetupColumn("Worst us");
        ImGui::TableSetupColumn("Share of frame");
        ImGui::TableHeadersRow();

        CostRow(update);
        CostRow(drawUi);
        CostRow(draw3d);

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // ---- The question reading the code could not answer ----

    ImGui::SeparatorText("Thread rates");

    ImGui::TextColored(COLOR_DIM,
        "The game thread and the render thread are not synchronised. These are");
    ImGui::TextColored(COLOR_DIM,
        "their own tick rates since this counter was last reset - if they differ,");
    ImGui::TextColored(COLOR_DIM,
        "a 'per frame' figure means two different things depending on the path.");

    ImGui::Spacing();

    const double seconds = SecondsSince(m_since);

    if (seconds < 0.5)
    {
        ImGui::TextColored(COLOR_DIM, "Measuring...");
    }
    else
    {
        const double updateHz = (update.TotalFrames() - m_baseUpdate) / seconds;
        const double drawUiHz = (drawUi.TotalFrames() - m_baseDrawUi) / seconds;
        const double draw3dHz = (draw3d.TotalFrames() - m_baseDraw3d) / seconds;

        ImGui::Text("Game thread updates:");
        ImGui::SameLine(260.f);
        ImGui::Text("%.1f / s", updateHz);

        ImGui::Text("Render thread UI draws:");
        ImGui::SameLine(260.f);
        ImGui::Text("%.1f / s", drawUiHz);

        ImGui::Text("Render thread 3D draws:");
        ImGui::SameLine(260.f);
        ImGui::Text("%.1f / s", draw3dHz);

        ImGui::Spacing();

        // The ratio is the actual answer, so it is stated rather than left for
        // the reader to divide.
        if (updateHz > 1.0 && drawUiHz > 1.0)
        {
            const double ratio = drawUiHz / updateHz;

            if (ratio > 0.95 && ratio < 1.05)
            {
                ImGui::TextColored(COLOR_QUIET,
                    "In step: one draw per update (ratio %.2f).", ratio);
            }
            else
            {
                ImGui::TextColored(COLOR_NOTABLE,
                    "NOT in step: %.2f draws per update.", ratio);
            }
        }

        ImGui::Text("Measured over %.0f s", seconds);

        // ---- What the offset readers cost the kernel ----

        ImGui::Spacing();
        ImGui::SeparatorText("Memory probes");

        ImGui::TextColored(COLOR_DIM,
            "Reading the game's loading and area flags means following pointers, and");
        ImGui::TextColored(COLOR_DIM,
            "every link used to be checked with a call into the kernel. Those calls");
        ImGui::TextColored(COLOR_DIM,
            "are cached now; this is what is left of them.");

        ImGui::Spacing();

        const double probes = (GameOffsets::GetProbeSyscalls() - m_baseProbes) / seconds;
        const double hits   = (GameOffsets::GetProbeCacheHits() - m_baseHits) / seconds;

        ImGui::Text("Kernel queries:");
        ImGui::SameLine(260.f);
        ImGui::TextColored(probes > 60.0 ? COLOR_NOTABLE : COLOR_QUIET, "%.1f / s", probes);

        ImGui::Text("Answered from cache:");
        ImGui::SameLine(260.f);
        ImGui::Text("%.1f / s", hits);

        if (updateHz > 1.0)
        {
            ImGui::TextColored(COLOR_DIM, "That is %.2f kernel queries per game-thread frame.",
                probes / updateHz);
        }
    }

    ImGui::Spacing();

    if (ImGui::Button("Reset rates"))
    {
        ResetRates(update, drawUi, draw3d);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("This mod");
}
