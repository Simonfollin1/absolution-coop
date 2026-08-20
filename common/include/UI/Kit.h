#pragma once

#include <imgui.h>

// The panel's look, in one place.
//
// Every SDK mod looks identical because every SDK mod is ImGui with the default
// style, and the default style is a 13-pixel bitmap font with square corners.
// Nothing layered on top of that reads as designed.
//
// Two things fix it. The SDK already loads Roboto Regular and Roboto Bold at
// 32 px and hands them out, so real type is free. And everything else - cards,
// rows, pill toggles, key chips - is drawn straight into the draw list, so none
// of ImGui's own widget chrome is on screen at all.
//
// The numbers below are design pixels at 1080p and every one of them is a
// multiple of four. Kit::Scale() converts to whatever the screen actually is,
// so the panel keeps its proportions from 1080p to 4K rather than shrinking
// into a corner.
namespace Kit
{
    struct Palette
    {
        ImU32 accent;       // the one bright colour, used for one thing at a time
        ImU32 accentHover;
        ImU32 onAccent;     // text on top of it

        ImU32 bg0;          // the card
        ImU32 bg1;          // a row on the card
        ImU32 bg2;          // a row under the cursor, and chips
        ImU32 bg3;          // the inactive half of a toggle

        ImU32 text;
        ImU32 textDim;      // labels that are not the point
        ImU32 textFaint;    // present but not worth reading

        ImU32 danger;
    };

    struct Metrics
    {
        // Spacing is 4, 8, 12, 16, 24, 32 and nothing else. Anything that needs
        // a gap uses one of those, which is the whole reason the result looks
        // deliberate rather than assembled.
        float xs   =  4.f;
        float sm   =  8.f;
        float md   = 12.f;
        float lg   = 16.f;
        float xl   = 24.f;
        float xxl  = 32.f;

        float cardRadius = 14.f;
        float rowRadius  = 10.f;
        float chipRadius =  7.f;

        float headerHeight =  52.f;
        float navHeight    =  44.f;
        float rowHeight    =  52.f;   // design --h-row

        // The right-hand rail. Every row puts its control and its key chip at
        // the same two x positions regardless of how long the label is, which
        // is what stops a list of settings looking ragged.
        float controlWidth  = 84.f;
        float controlHeight = 28.f;
        float chipMinWidth  = 40.f;
        float chipHeight    = 26.f;

        float titleSize   = 19.f;
        float sectionSize = 12.f;
        float bodySize    = 14.f;
        float smallSize   = 12.f;
    };

    const Palette& Colors();
    const Metrics& Size();

    // Design pixels to screen pixels. The SDK sets io.FontGlobalScale from the
    // window height, so the font it hands back already carries the screen's
    // scale and everything else can be derived from it.
    float Scale();

    ImFont* Regular();
    ImFont* Bold();

    // --- the card -----------------------------------------------------------

    // A rounded card with a coloured header, a title and a close box. Returns
    // false when it is not visible; do not draw and do not call EndPanel.
    bool BeginPanel(const char* id, const char* title, bool* open);
    void EndPanel();

    // The row of tabs under the header. Pills, not ImGui tabs.
    void BeginNav();
    bool NavPill(const char* label, bool active);
    void EndNav();

    // The scrolling area under the nav, with the card's padding applied.
    // reserveBottomPx (design pixels) leaves a strip below the scrolling body
    // for a pinned footer; 0 fills the card.
    void BeginBody(float reserveBottomPx = 0.f);
    void EndBody();

    // --- rows ---------------------------------------------------------------

    // A quiet heading over a group of rows.
    void SectionLabel(const char* text);

    // label .................... [ Off | On ]   [F6]
    //
    // The pill is the only part that reacts: clicking the left half turns it
    // off, the right half on. Clicking the row anywhere else does nothing,
    // because a settings list where a stray click flips something is worse than
    // one that needs aiming.
    bool ToggleRow(const char* label, bool* value, const char* keyHint = nullptr);

    // label .................... value
    void InfoRow(const char* label, const char* value);

    // label .................... [ button ]     [F7]
    bool ActionRow(const char* label, const char* buttonLabel, const char* keyHint = nullptr);

    // --- pieces -------------------------------------------------------------

    // Filled in the accent when primary, outlined when not.
    bool Button(const char* label, bool primary = false, float width = 0.f);

    void Chip(const char* text);

    void Text(const char* text);
    void TextDim(const char* text);
    void TextWrapped(const char* text);
    void Danger(const char* text);

    void Gap(float designPixels);
    void Divider();

    // ImGui's own input widgets, dressed to match. Push before, pop after.
    void PushInputStyle();
    void PopInputStyle();
}
