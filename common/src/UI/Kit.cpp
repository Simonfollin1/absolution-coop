#include "UI/Kit.h"

#include <cfloat>
#include <cstdio>

#include <SDK.h>

namespace Kit
{
    namespace
    {
        constexpr ImU32 Rgb(unsigned int hex, unsigned int alpha = 255)
        {
            // ImGui packs as ABGR.
            return (alpha << IM_COL32_A_SHIFT)
                 | ((hex & 0xFFu) << IM_COL32_B_SHIFT)
                 | (((hex >> 8) & 0xFFu) << IM_COL32_G_SHIFT)
                 | (((hex >> 16) & 0xFFu) << IM_COL32_R_SHIFT);
        }

        // Amaranth red - the accent means "press this", and because black on
        // this red is unreadable (3.4:1 at body size), everything that sits on
        // the accent is near-white (--on-accent). White on the accent is 5.3:1.
        // The greys carry a few percent of the accent's hue so they lean warm
        // rather than neutral - a red header on flat-grey rows reads as two
        // designs stapled together. The alarm (danger) is a hotter red than the
        // accent and is kept back for the one state that is an emergency.
        const Palette kPalette = {
            Rgb(0xCE1924),   // accent      - Amaranth red, "press this"
            Rgb(0xDB1E2B),   // accentHover
            Rgb(0xFAFAFA),   // onAccent    - near-white, the readable side of red

            Rgb(0x161413),   // bg0         - the card
            Rgb(0x201D1B),   // bg1         - a row on the card
            Rgb(0x2A2624),   // bg2         - a row under the cursor, and chips
            Rgb(0x36322E),   // bg3         - the inactive half of a toggle, chip end-caps

            Rgb(0xF1F0EE),   // text
            Rgb(0x9D9A96),   // textDim     - labels that are not the point
            Rgb(0x66635F),   // textFaint   - present but not worth reading

            Rgb(0xFF6B60),   // danger      - the alarm, hotter than the accent
        };

        const Metrics kMetrics = {};

        ImVec2 At(float x, float y) { return ImVec2(x, y); }

        // Draws text left-aligned and vertically centred in a box, clipped to
        // it. Long labels get cut rather than pushing the rail out of line.
        void TextIn(ImDrawList* drawList, ImFont* font, float size,
                    const ImVec2& min, const ImVec2& max, ImU32 colour,
                    const char* text)
        {
            const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);

            drawList->PushClipRect(min, max, true);
            drawList->AddText(font, size, At(min.x, min.y + (max.y - min.y - measured.y) * 0.5f),
                              colour, text);
            drawList->PopClipRect();
        }

        void TextCentred(ImDrawList* drawList, ImFont* font, float size,
                         const ImVec2& min, const ImVec2& max, ImU32 colour,
                         const char* text)
        {
            const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);

            drawList->AddText(font, size,
                              At(min.x + (max.x - min.x - measured.x) * 0.5f,
                                 min.y + (max.y - min.y - measured.y) * 0.5f),
                              colour, text);
        }

        // Where a row's control and key chip sit, measured in from the right so
        // every row lines up with every other row.
        struct Rail
        {
            float chipLeft;
            float controlLeft;
            float controlRight;
        };

        Rail RailFor(float rowRight, float chipWidth)
        {
            const Metrics& m = Size();
            const float    s = Scale();

            Rail rail{};

            rail.chipLeft     = rowRight - m.lg * s - chipWidth;
            rail.controlRight = rail.chipLeft - m.lg * s;
            rail.controlLeft  = rail.controlRight - m.controlWidth * s;

            return rail;
        }

        float ChipWidth(const char* text)
        {
            const Metrics& m = Size();
            const float    s = Scale();

            if (!text || !*text)
            {
                return 0.f;
            }

            const ImVec2 measured =
                Regular()->CalcTextSizeA(m.smallSize * s, FLT_MAX, 0.f, text);

            const float wanted = measured.x + m.md * s;

            return wanted < m.chipMinWidth * s ? m.chipMinWidth * s : wanted;
        }

        void DrawChip(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                      const char* text)
        {
            const Palette& c = Colors();
            const Metrics& m = Size();

            drawList->AddRectFilled(min, max, c.bg2, m.chipRadius * Scale());

            TextCentred(drawList, Regular(), m.smallSize * Scale(), min, max, c.textDim, text);
        }

        // The Off/On control. Two halves inside one track; the lit half moves.
        void DrawPill(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, bool on,
                      bool hovered)
        {
            const Palette& c = Colors();
            const Metrics& m = Size();
            const float    s = Scale();

            const float height   = max.y - min.y;
            const float rounding = height * 0.5f;
            const float half     = (max.x - min.x) * 0.5f;

            drawList->AddRectFilled(min, max, c.bg3, rounding);

            const ImVec2 knobMin = on ? At(min.x + half, min.y) : min;
            const ImVec2 knobMax = on ? max : At(min.x + half, max.y);

            drawList->AddRectFilled(knobMin, knobMax,
                                    on ? (hovered ? c.accentHover : c.accent)
                                       : (hovered ? c.bg2 : c.bg2),
                                    rounding);

            const float size = m.smallSize * s;

            TextCentred(drawList, Bold(), size, min, At(min.x + half, max.y),
                        on ? c.textDim : c.text, "Off");

            TextCentred(drawList, Bold(), size, At(min.x + half, min.y), max,
                        on ? c.onAccent : c.textDim, "On");
        }

        // The row background, plus the label, plus the chip. Everything every
        // row has in common, so no row can drift out of alignment with the rest.
        struct RowLayout
        {
            ImVec2 min;
            ImVec2 max;
            Rail   rail;
            bool   hovered = false;
        };

        RowLayout BeginRow(const char* id, const char* label, const char* keyHint)
        {
            const Palette& c = Colors();
            const Metrics& m = Size();
            const float    s = Scale();

            RowLayout row{};

            const float width = ImGui::GetContentRegionAvail().x;

            row.min = ImGui::GetCursorScreenPos();

            ImGui::InvisibleButton(id, At(width, m.rowHeight * s));

            row.max     = At(row.min.x + width, row.min.y + m.rowHeight * s);
            row.hovered = ImGui::IsItemHovered();

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            drawList->AddRectFilled(row.min, row.max,
                                    row.hovered ? c.bg2 : c.bg1, m.rowRadius * s);

            const float chipWidth = ChipWidth(keyHint);

            row.rail = RailFor(row.max.x, chipWidth);

            if (chipWidth > 0.f)
            {
                const float chipHeight = m.chipHeight * s;
                const float top        = row.min.y + (m.rowHeight * s - chipHeight) * 0.5f;

                DrawChip(drawList, At(row.rail.chipLeft, top),
                         At(row.rail.chipLeft + chipWidth, top + chipHeight), keyHint);
            }

            TextIn(drawList, Regular(), m.bodySize * s,
                   At(row.min.x + m.lg * s, row.min.y),
                   At(row.rail.controlLeft - m.md * s, row.max.y),
                   c.text, label);

            return row;
        }

        void AfterRow()
        {
            ImGui::Dummy(At(0.f, Size().xs * Scale()));
        }
    }

    const Palette& Colors() { return kPalette; }
    const Metrics& Size()   { return kMetrics; }

    float Scale()
    {
        // The fonts are built at 32 px and io.FontGlobalScale is the screen's
        // height over 2048, so the size ImGui reports already carries the
        // screen. Sixteen is the design size these numbers were drawn at.
        //
        // Floored at 1: the mockup is scale 1, and on a modest window the font
        // scale comes out below it, which shrank the whole card - width,
        // padding, spacing and text all - well under the design. Never smaller
        // than the mockup; larger only on a screen tall enough to earn it.
        const float size = ImGui::GetFontSize();

        return size > 16.f ? size / 16.f : 1.f;
    }

    ImFont* Regular()
    {
        ImFont* font = SDK::GetInstance().GetRegularFont();

        return font ? font : ImGui::GetFont();
    }

    ImFont* Bold()
    {
        ImFont* font = SDK::GetInstance().GetBoldFont();

        return font ? font : Regular();
    }

    // --- the card -----------------------------------------------------------

    bool BeginPanel(const char* id, const char* title, bool* open)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   m.cardRadius * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    At(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, c.bg0);

        ImGui::SetNextWindowSizeConstraints(At(520.f * s, 420.f * s), At(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(At(560.f * s, 640.f * s), ImGuiCond_FirstUseEver);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                                         | ImGuiWindowFlags_NoScrollbar
                                         | ImGuiWindowFlags_NoScrollWithMouse
                                         | ImGuiWindowFlags_NoCollapse;

        if (!ImGui::Begin(id, nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);

            return false;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImVec2 origin = ImGui::GetWindowPos();
        const float  width  = ImGui::GetWindowWidth();
        const float  height = m.headerHeight * s;

        const ImVec2 headerMin = origin;
        const ImVec2 headerMax = At(origin.x + width, origin.y + height);

        drawList->AddRectFilled(headerMin, headerMax, c.accent,
                                m.cardRadius * s, ImDrawFlags_RoundCornersTop);

        TextIn(drawList, Bold(), m.titleSize * s,
               At(headerMin.x + m.lg * s, headerMin.y),
               At(headerMax.x - m.xxl * s, headerMax.y),
               c.onAccent, title);

        // The close box: a hit area with an X drawn in it, rather than ImGui's
        // own, which would arrive with its own colours and its own hover.
        const float box = 26.f * s;

        ImGui::SetCursorScreenPos(At(headerMax.x - m.lg * s - box,
                                     headerMin.y + (height - box) * 0.5f));

        ImGui::InvisibleButton("##close", At(box, box));

        const bool closeHovered = ImGui::IsItemHovered();
        const bool closeClicked = ImGui::IsItemClicked();

        const ImVec2 boxMin = ImGui::GetItemRectMin();
        const ImVec2 boxMax = ImGui::GetItemRectMax();

        if (closeHovered)
        {
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 40), box * 0.5f);
        }

        const float inset = box * 0.3f;

        drawList->AddLine(At(boxMin.x + inset, boxMin.y + inset),
                          At(boxMax.x - inset, boxMax.y - inset), c.onAccent, 2.f * s);
        drawList->AddLine(At(boxMax.x - inset, boxMin.y + inset),
                          At(boxMin.x + inset, boxMax.y - inset), c.onAccent, 2.f * s);

        if (closeClicked && open)
        {
            *open = false;
        }

        ImGui::SetCursorScreenPos(At(origin.x, origin.y + height));

        return true;
    }

    void EndPanel()
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    void BeginNav()
    {
        const Metrics& m = Size();
        const float    s = Scale();

        ImGui::SetCursorPosX(m.lg * s);
        ImGui::Dummy(At(0.f, m.md * s));
        ImGui::SetCursorPosX(m.lg * s);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, At(m.sm * s, 0.f));
    }

    bool NavPill(const char* label, bool active)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        const float  size     = m.bodySize * s;
        const ImVec2 measured = Bold()->CalcTextSizeA(size, FLT_MAX, 0.f, label);

        const float width  = measured.x + m.lg * s * 2.f;
        const float height = m.navHeight * s - m.md * s;

        const ImVec2 min = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(label, At(width, height));

        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        const ImVec2 max = ImGui::GetItemRectMax();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (active)
        {
            drawList->AddRectFilled(min, max, c.text, height * 0.5f);
        }
        else if (hovered)
        {
            drawList->AddRectFilled(min, max, c.bg1, height * 0.5f);
        }

        TextCentred(drawList, Bold(), size, min, max,
                    active ? c.bg0 : (hovered ? c.text : c.textDim), label);

        ImGui::SameLine();

        return clicked;
    }

    void EndNav()
    {
        ImGui::PopStyleVar();
        ImGui::NewLine();
    }

    void BeginBody(float reserveBottomPx)
    {
        const Metrics& m = Size();
        const float    s = Scale();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, At(m.lg * s, m.md * s));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg0);

        // A negative height leaves that many pixels below the child for a footer.
        ImGui::BeginChild("##body", At(0.f, -reserveBottomPx * s), false);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, At(0.f, 0.f));
    }

    void EndBody()
    {
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // --- rows ---------------------------------------------------------------

    void SectionLabel(const char* text)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        // Design: 24 above a heading, 12 below it. The generous top gap is what
        // separates one group of rows from the previous group.
        ImGui::Dummy(At(0.f, m.xl * s));

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float  height = m.sectionSize * s * 1.6f;

        ImGui::Dummy(At(ImGui::GetContentRegionAvail().x, height));

        TextIn(ImGui::GetWindowDrawList(), Bold(), m.sectionSize * s,
               min, At(min.x + ImGui::GetContentRegionAvail().x, min.y + height),
               c.textDim, text);

        ImGui::Dummy(At(0.f, m.md * s));
    }

    bool ToggleRow(const char* label, bool* value, const char* keyHint)
    {
        const Metrics& m = Size();
        const float    s = Scale();

        const RowLayout row = BeginRow(label, label, keyHint);

        const float height = m.controlHeight * s;
        const float top    = row.min.y + (m.rowHeight * s - height) * 0.5f;

        const ImVec2 pillMin = At(row.rail.controlLeft, top);
        const ImVec2 pillMax = At(row.rail.controlRight, top + height);

        const ImVec2 mouse = ImGui::GetIO().MousePos;

        const bool overPill = mouse.x >= pillMin.x && mouse.x <= pillMax.x
                           && mouse.y >= pillMin.y && mouse.y <= pillMax.y;

        bool changed = false;

        if (ImGui::IsItemClicked() && overPill)
        {
            const bool wanted = mouse.x >= (pillMin.x + pillMax.x) * 0.5f;

            if (wanted != *value)
            {
                *value  = wanted;
                changed = true;
            }
        }

        DrawPill(ImGui::GetWindowDrawList(), pillMin, pillMax, *value,
                 row.hovered && overPill);

        AfterRow();

        return changed;
    }

    void InfoRow(const char* label, const char* value)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        const RowLayout row = BeginRow(label, label, nullptr);

        const ImVec2 measured = Regular()->CalcTextSizeA(m.bodySize * s, FLT_MAX, 0.f, value);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImVec2 min = At(row.max.x - m.lg * s - measured.x, row.min.y);

        TextIn(drawList, Regular(), m.bodySize * s, min,
               At(row.max.x - m.lg * s, row.max.y), c.textDim, value);

        AfterRow();
    }

    bool ActionRow(const char* label, const char* buttonLabel, const char* keyHint)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        const RowLayout row = BeginRow(label, label, keyHint);

        const float height = m.controlHeight * s;
        const float top    = row.min.y + (m.rowHeight * s - height) * 0.5f;

        const ImVec2 min = At(row.rail.controlLeft, top);
        const ImVec2 max = At(row.rail.controlRight, top + height);

        const ImVec2 mouse = ImGui::GetIO().MousePos;

        const bool over = mouse.x >= min.x && mouse.x <= max.x
                       && mouse.y >= min.y && mouse.y <= max.y;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(min, max,
                                (row.hovered && over) ? c.accentHover : c.accent,
                                height * 0.5f);

        TextCentred(drawList, Bold(), m.smallSize * s, min, max, c.onAccent, buttonLabel);

        AfterRow();

        return ImGui::IsItemClicked() && over;
    }

    // --- pieces -------------------------------------------------------------

    bool Button(const char* label, bool primary, float width)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        const float  size     = m.bodySize * s;
        const ImVec2 measured = Bold()->CalcTextSizeA(size, FLT_MAX, 0.f, label);

        const float height = m.controlHeight * s * 1.25f;
        const float wanted = width > 0.f ? width * s : measured.x + m.xl * s * 2.f;

        const ImVec2 min = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(label, At(wanted, height));

        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        const ImVec2 max = ImGui::GetItemRectMax();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (primary)
        {
            drawList->AddRectFilled(min, max, hovered ? c.accentHover : c.accent,
                                    height * 0.5f);
        }
        else
        {
            drawList->AddRectFilled(min, max, hovered ? c.bg2 : c.bg1, height * 0.5f);
        }

        TextCentred(drawList, Bold(), size, min, max, primary ? c.onAccent : c.text, label);

        return clicked;
    }

    void Chip(const char* text)
    {
        const Metrics& m = Size();
        const float    s = Scale();

        const float width  = ChipWidth(text);
        const float height = m.chipHeight * s;

        const ImVec2 min = ImGui::GetCursorScreenPos();

        ImGui::Dummy(At(width, height));

        DrawChip(ImGui::GetWindowDrawList(), min, At(min.x + width, min.y + height), text);
    }

    namespace
    {
        void Line(const char* text, ImU32 colour)
        {
            const Metrics& m = Size();
            const float    s = Scale();

            const float  size     = m.bodySize * s;
            const ImVec2 measured = Regular()->CalcTextSizeA(size, FLT_MAX, 0.f, text);

            const ImVec2 min = ImGui::GetCursorScreenPos();

            ImGui::Dummy(At(ImGui::GetContentRegionAvail().x, measured.y * 1.45f));

            TextIn(ImGui::GetWindowDrawList(), Regular(), size, min,
                   At(min.x + ImGui::GetContentRegionAvail().x, min.y + measured.y * 1.45f),
                   colour, text);
        }
    }

    void Text(const char* text)    { Line(text, Colors().text);     }
    void TextDim(const char* text) { Line(text, Colors().textDim);  }
    void Danger(const char* text)  { Line(text, Colors().danger);   }

    void TextWrapped(const char* text)
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        const float width = ImGui::GetContentRegionAvail().x;
        const float size  = m.bodySize * s;

        const ImVec2 measured = Regular()->CalcTextSizeA(size, FLT_MAX, width, text);

        const ImVec2 min = ImGui::GetCursorScreenPos();

        ImGui::Dummy(At(width, measured.y + m.xs * s));

        ImGui::GetWindowDrawList()->AddText(Regular(), size, min, c.textDim, text,
                                            nullptr, width);
    }

    void Gap(float designPixels)
    {
        ImGui::Dummy(At(0.f, designPixels * Scale()));
    }

    void Divider()
    {
        const Metrics& m = Size();
        const float    s = Scale();

        Gap(m.md);

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float  width = ImGui::GetContentRegionAvail().x;

        ImGui::Dummy(At(width, 1.f));

        ImGui::GetWindowDrawList()->AddRectFilled(min, At(min.x + width, min.y + 1.f),
                                                  Colors().bg2);

        Gap(m.md);
    }

    void PushInputStyle()
    {
        const Palette& c = Colors();
        const Metrics& m = Size();
        const float    s = Scale();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.chipRadius * s);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  At(m.md * s, m.sm * s));

        ImGui::PushStyleColor(ImGuiCol_FrameBg,        c.bg1);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, c.bg2);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  c.bg2);
        ImGui::PushStyleColor(ImGuiCol_Text,           c.text);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled,   c.textFaint);
    }

    void PopInputStyle()
    {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
    }
}
