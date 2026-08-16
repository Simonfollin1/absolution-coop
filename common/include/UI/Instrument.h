#pragma once

#include <imgui.h>

namespace UI
{
    // A framed area for something drawn rather than something clicked.
    //
    // Heavy visualisations sitting loose among checkbox rows read as decoration.
    // Giving them one consistent container makes the distinction structural
    // instead of something to remember to draw the same way each time:
    //
    //   - a recessed surface, darker than the panel behind it
    //   - inset from the panel edge, where plain controls run full width, so the
    //     change of rhythm carries the change of category
    //   - a caption inside the frame at top left
    //   - the exact value at top right, in the same place every time, so an
    //     instrument is never less precise than the slider it replaced
    //
    // One rule holds the whole thing together: never put a control inside an
    // instrument. The moment a checkbox appears inside the frame it stops being
    // obvious whether the frame is a display or a group. Controls belonging to
    // an instrument go immediately below it, outside.
    //
    // Returns false when the frame is clipped away; skip the drawing then, but
    // EndInstrument must still be called - same contract as ImGui::Begin.
    bool BeginInstrument(const char* caption, float height, const char* valueText = nullptr);
    void EndInstrument();

    // Top-left corner of the usable area inside the current instrument, and its
    // size. Valid between Begin and End.
    ImVec2 InstrumentOrigin();
    ImVec2 InstrumentSize();

    // A row of controls that belongs to the instrument just ended. Tighter
    // spacing than normal, so it reads as attached rather than as the next
    // unrelated thing.
    void BeginInstrumentControls();
    void EndInstrumentControls();
}
