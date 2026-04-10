#include "engine/ui/Measure.h"

#include <cstdint>

#include "engine/ui/DefaultFont.h"
#include "engine/ui/GlyphMetrics.h"
#include "engine/ui/IFont.h"

namespace engine::ui
{

namespace
{

// Decode a single UTF-8 codepoint at `*p`, advancing `*p` past the
// sequence. Returns 0 on malformed input. Handles 1..4 byte sequences.
std::uint32_t utf8Next(const char** p)
{
    const unsigned char* s = reinterpret_cast<const unsigned char*>(*p);
    if (!*s)
        return 0;
    std::uint32_t cp = 0;
    int extra = 0;
    if (*s < 0x80)
    {
        cp = *s;
        extra = 0;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
        cp = *s & 0x1F;
        extra = 1;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
        cp = *s & 0x0F;
        extra = 2;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
        cp = *s & 0x07;
        extra = 3;
    }
    else
    {
        ++(*p);
        return 0;
    }
    ++s;
    for (int i = 0; i < extra; ++i)
    {
        if ((*s & 0xC0) != 0x80)
        {
            *p = reinterpret_cast<const char*>(s);
            return 0;
        }
        cp = (cp << 6) | (*s & 0x3F);
        ++s;
    }
    *p = reinterpret_cast<const char*>(s);
    return cp;
}

}  // namespace

math::Vec2 measureText(const IFont* font, float fontSize, const char* text)
{
    if (!font)
        font = defaultFont();
    if (!font || !text)
        return {0.f, 0.f};

    const float scale = fontSize / font->nominalSize();
    float width = 0.f;
    std::uint32_t prev = 0;

    const char* p = text;
    while (*p)
    {
        const std::uint32_t cp = utf8Next(&p);
        if (cp == 0)
            continue;
        if (cp == '\n')
        {
            prev = 0;
            continue;
        }
        const GlyphMetrics* g = font->getGlyph(cp);
        if (!g)
            continue;
        if (prev)
            width += font->getKerning(prev, cp) * scale;
        width += g->advance * scale;
        prev = cp;
    }

    const float height = font->lineHeight() * scale;
    return {width, height};
}

}  // namespace engine::ui
