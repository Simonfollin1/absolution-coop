#pragma once

// Small hover-help widgets.
//
// Explanations live behind a "(?)" marker next to the control instead of as
// paragraphs under it, so a panel stays readable while still explaining every
// toggle to someone seeing it for the first time.
namespace UI
{
    // Appends a "(?)" marker on the same line as the previous item, showing
    // description on hover.
    void Help(const char* description);

    // Control plus marker in one call. Return value matches the ImGui widget.
    bool Checkbox(const char* label, bool* value, const char* description);
    bool SliderFloat(const char* label, float* value, float min, float max,
                     const char* format, const char* description);
    bool SliderInt(const char* label, int* value, int min, int max,
                   const char* description);
    bool Button(const char* label, const char* description);
}
