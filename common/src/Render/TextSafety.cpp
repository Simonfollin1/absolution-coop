#include "Render/TextSafety.h"

namespace Render
{
    void ToFontSafeAscii(const char* input, char* output, std::size_t outputSize)
    {
        if (!output || outputSize == 0)
        {
            return;
        }

        if (!input)
        {
            output[0] = '\0';
            return;
        }

        std::size_t written = 0;

        while (input[written] != '\0' && written + 1 < outputSize)
        {
            const auto character = static_cast<unsigned char>(input[written]);

            output[written] = (character >= 0x20 && character <= 0x7E)
                ? input[written]
                : '?';

            ++written;
        }

        output[written] = '\0';
    }
}
