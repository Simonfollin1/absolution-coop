#pragma once

#include <cstddef>

namespace Render
{
    // Rewrites text so it is safe to hand to the SDK's DrawText2D.
    //
    // That goes through DirectXTK's SpriteFont, which throws
    // std::runtime_error("Character not in font sheet") for any glyph the font
    // does not contain, because the SDK never sets a default character. The
    // bundled Roboto sprite font covers printable ASCII, and the throw happens
    // inside the present hook, where an exception is a crash rather than an
    // error. Engine-supplied strings - actor names above all - are not ours to
    // trust on that point.
    //
    // Anything outside 0x20..0x7E becomes '?'. Always null-terminates.
    void ToFontSafeAscii(const char* input, char* output, std::size_t outputSize);
}
