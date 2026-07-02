#include "render/font.hpp"

#include "core/log.hpp"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <font8x8_basic.h>

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

constexpr int kFirstChar = 32;
constexpr int kCharCount = 95; // 32..126

} // namespace

const Glyph& FontAtlas::glyph(char c) const {
    int idx = static_cast<unsigned char>(c) - kFirstChar;
    if (idx < 0 || idx >= kCharCount) {
        idx = '?' - kFirstChar;
    }
    return glyphs[static_cast<size_t>(idx)];
}

FontAtlas bake_builtin_font() {
    // 16 x 6 grid of 10px cells (8px glyph + 2px guard against bleed).
    constexpr int kCell = 10;
    constexpr int kCols = 16;
    constexpr int kRows = (kCharCount + kCols - 1) / kCols;

    FontAtlas atlas;
    atlas.px_height = 8.0f;
    atlas.line_advance = 10.0f;
    atlas.image.width = kCols * kCell;  // 160
    atlas.image.height = kRows * kCell; // 60
    atlas.image.pixels.assign(
        static_cast<size_t>(atlas.image.width) * atlas.image.height * 4, 0);

    const float aw = static_cast<float>(atlas.image.width);
    const float ah = static_cast<float>(atlas.image.height);

    for (int i = 0; i < kCharCount; ++i) {
        const int cx = (i % kCols) * kCell;
        const int cy = (i / kCols) * kCell;
        // font8x8_basic rows are LSB-first: bit x of row y = pixel (x, y).
        const unsigned char* rows = font8x8_basic[kFirstChar + i];
        bool any_pixel = false;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if ((rows[y] >> x) & 1u) {
                    any_pixel = true;
                    const size_t p =
                        (static_cast<size_t>(cy + y) * atlas.image.width + (cx + x)) * 4;
                    atlas.image.pixels[p + 0] = 255;
                    atlas.image.pixels[p + 1] = 255;
                    atlas.image.pixels[p + 2] = 255;
                    atlas.image.pixels[p + 3] = 255;
                }
            }
        }
        Glyph& g = atlas.glyphs[static_cast<size_t>(i)];
        g.uv = {static_cast<float>(cx) / aw, static_cast<float>(cy) / ah,
                static_cast<float>(cx + 8) / aw, static_cast<float>(cy + 8) / ah};
        // Empty glyphs (space) advance the pen but emit no quad.
        g.size = any_pixel ? glm::vec2{8.0f, 8.0f} : glm::vec2{0.0f, 0.0f};
        g.offset = {0.0f, 0.0f};
        g.advance = 8.0f;
    }
    return atlas;
}

FontAtlas bake_ttf_font(std::span<const uint8_t> ttf_bytes, float px_height) {
    FontAtlas atlas;
    if (ttf_bytes.empty()) {
        return atlas;
    }

    // Validate before baking — stb_truetype trusts its input, and garbage
    // bytes would walk out of bounds inside BakeFontBitmap.
    const int offset = stbtt_GetFontOffsetForIndex(ttf_bytes.data(), 0);
    if (offset < 0) {
        log_warn("not a recognizable ttf; using builtin font");
        return atlas;
    }
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_bytes.data(), offset)) {
        log_warn("ttf failed to parse; using builtin font");
        return atlas;
    }

    constexpr int kAtlasSize = 512;
    std::vector<uint8_t> coverage(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    std::array<stbtt_bakedchar, kCharCount> baked{};

    const int rows = stbtt_BakeFontBitmap(ttf_bytes.data(), offset, px_height, coverage.data(),
                                          kAtlasSize, kAtlasSize, kFirstChar, kCharCount,
                                          baked.data());
    if (rows <= 0) {
        log_warn("font bake failed (atlas too small or bad ttf); using builtin font");
        return atlas;
    }
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    const float scale = stbtt_ScaleForPixelHeight(&info, px_height);
    const float ascent_px = static_cast<float>(ascent) * scale;

    atlas.px_height = px_height;
    atlas.line_advance = static_cast<float>(ascent - descent + line_gap) * scale;
    atlas.image.width = kAtlasSize;
    atlas.image.height = kAtlasSize;
    atlas.image.pixels.resize(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        atlas.image.pixels[i * 4 + 0] = 255;
        atlas.image.pixels[i * 4 + 1] = 255;
        atlas.image.pixels[i * 4 + 2] = 255;
        atlas.image.pixels[i * 4 + 3] = coverage[i];
    }

    const float aw = static_cast<float>(kAtlasSize);
    for (int i = 0; i < kCharCount; ++i) {
        const stbtt_bakedchar& b = baked[static_cast<size_t>(i)];
        Glyph& g = atlas.glyphs[static_cast<size_t>(i)];
        g.uv = {b.x0 / aw, b.y0 / aw, b.x1 / aw, b.y1 / aw};
        g.size = {static_cast<float>(b.x1 - b.x0), static_cast<float>(b.y1 - b.y0)};
        // BakeFontBitmap offsets are baseline-relative; ours are line-top-relative.
        g.offset = {b.xoff, b.yoff + ascent_px};
        g.advance = b.xadvance;
    }
    return atlas;
}

glm::vec2 measure_text(const FontAtlas& font, std::string_view text, float scale, float tracking) {
    float widest = 0.0f;
    float x = 0.0f;
    int lines = text.empty() ? 0 : 1;
    for (const char c : text) {
        if (c == '\n') {
            widest = std::max(widest, x);
            x = 0.0f;
            ++lines;
            continue;
        }
        x += (font.glyph(c).advance + tracking) * scale;
    }
    widest = std::max(widest, x);
    return {widest, static_cast<float>(lines) * font.line_advance * scale};
}

void emit_text(std::vector<OverlayQuad>& out, const FontAtlas& font, std::string_view text,
               glm::vec2 top_left, float scale, glm::vec4 color, float tracking) {
    glm::vec2 pen = top_left;
    for (const char c : text) {
        if (c == '\n') {
            pen.x = top_left.x;
            pen.y += font.line_advance * scale;
            continue;
        }
        const Glyph& g = font.glyph(c);
        if (g.size.x > 0.0f && g.size.y > 0.0f) {
            OverlayQuad q;
            q.pos = pen + g.offset * scale;
            q.size = g.size * scale;
            q.uv_rect = g.uv;
            q.layer = 0.0f; // the font texture is a single-layer array
            q.color = color;
            out.push_back(q);
        }
        pen.x += (g.advance + tracking) * scale;
    }
}

} // namespace ds
