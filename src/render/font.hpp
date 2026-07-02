#pragma once

#include "render/frame_view.hpp"
#include "render/texture_load.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ds {

struct Glyph {
    glm::vec4 uv{0.0f};     // u0 v0 u1 v1 in the atlas
    glm::vec2 size{0.0f};   // pixels at base scale
    glm::vec2 offset{0.0f}; // pen-relative draw offset (x bearing, y from the line top)
    float advance = 0.0f;
};

// White-on-transparent glyph atlas + metrics for ASCII 32..126. Pure CPU —
// baking and layout are unit-testable; the GPU only ever sees the Image.
struct FontAtlas {
    Image image;
    float px_height = 8.0f;    // size the glyphs were baked at
    float line_advance = 10.0f;
    std::array<Glyph, 95> glyphs{};

    bool valid() const { return image.width > 0; }
    const Glyph& glyph(char c) const; // out-of-range maps to '?'
};

// Embedded public-domain 8x8 pixel font (third_party/font8x8) — always works,
// fits the retro look, scales cleanly at integer multiples.
FontAtlas bake_builtin_font();

// Bakes a TTF via stb_truetype. Returns an invalid() atlas on failure; callers
// fall back to the builtin font.
FontAtlas bake_ttf_font(std::span<const uint8_t> ttf_bytes, float px_height);

// `tracking` adds extra pixels between characters at scale 1 (the menu design
// uses widely spaced capitals). '\n' starts a new line.
glm::vec2 measure_text(const FontAtlas& font, std::string_view text, float scale = 1.0f,
                       float tracking = 0.0f);
void emit_text(std::vector<OverlayQuad>& out, const FontAtlas& font, std::string_view text,
               glm::vec2 top_left, float scale, glm::vec4 color, float tracking = 0.0f);

} // namespace ds
