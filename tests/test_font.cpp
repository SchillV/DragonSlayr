#include <doctest/doctest.h>

#include "render/font.hpp"

using namespace ds;

TEST_CASE("builtin font bakes a valid atlas") {
    const FontAtlas font = bake_builtin_font();
    REQUIRE(font.valid());
    CHECK(font.image.width > 0);
    CHECK(font.image.height > 0);
    CHECK(font.px_height == 8.0f);
    CHECK(font.line_advance > font.px_height);

    // Every printable glyph has sane metrics and uvs inside the atlas.
    for (char c = 32; c < 127; ++c) {
        const Glyph& g = font.glyph(c);
        CHECK(g.advance > 0.0f);
        CHECK(g.uv.x >= 0.0f);
        CHECK(g.uv.y >= 0.0f);
        CHECK(g.uv.z <= 1.0f);
        CHECK(g.uv.w <= 1.0f);
        CHECK(g.uv.x < g.uv.z);
        CHECK(g.uv.y < g.uv.w);
    }
}

TEST_CASE("visible glyphs have coverage, space does not") {
    const FontAtlas font = bake_builtin_font();
    auto coverage_of = [&](char c) {
        const Glyph& g = font.glyph(c);
        const int x0 = static_cast<int>(g.uv.x * static_cast<float>(font.image.width));
        const int y0 = static_cast<int>(g.uv.y * static_cast<float>(font.image.height));
        int hits = 0;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const size_t p =
                    (static_cast<size_t>(y0 + y) * font.image.width + (x0 + x)) * 4;
                hits += font.image.pixels[p + 3] > 0 ? 1 : 0;
            }
        }
        return hits;
    };
    CHECK(coverage_of('A') > 8);
    CHECK(coverage_of('#') > 8);
    CHECK(coverage_of('0') > 8);
    CHECK(coverage_of(' ') == 0);
}

TEST_CASE("out-of-range characters map to '?'") {
    const FontAtlas font = bake_builtin_font();
    CHECK(&font.glyph('\t') == &font.glyph('?'));
    CHECK(&font.glyph(static_cast<char>(200)) == &font.glyph('?'));
}

TEST_CASE("measure_text grows with content and handles newlines") {
    const FontAtlas font = bake_builtin_font();
    const glm::vec2 one = measure_text(font, "A");
    const glm::vec2 three = measure_text(font, "AAA");
    CHECK(three.x > one.x * 2.5f);
    CHECK(one.y == doctest::Approx(font.line_advance));

    const glm::vec2 two_lines = measure_text(font, "AA\nAAAA");
    CHECK(two_lines.y == doctest::Approx(2.0f * font.line_advance));
    CHECK(two_lines.x == doctest::Approx(measure_text(font, "AAAA").x));

    // Scale and tracking widen the result.
    CHECK(measure_text(font, "AAA", 2.0f).x == doctest::Approx(three.x * 2.0f));
    CHECK(measure_text(font, "AAA", 1.0f, 2.0f).x > three.x);
}

TEST_CASE("emit_text produces one quad per visible glyph") {
    const FontAtlas font = bake_builtin_font();
    std::vector<OverlayQuad> quads;
    emit_text(quads, font, "HP 42", {10.0f, 10.0f}, 1.0f, glm::vec4{1.0f});
    CHECK(quads.size() == 4); // 'H','P','4','2' — space emits nothing
    for (const OverlayQuad& q : quads) {
        CHECK(q.size.x > 0.0f);
        CHECK(q.pos.x >= 10.0f);
        CHECK(q.rot == 0.0f);
    }
    // Left-to-right pen advance.
    CHECK(quads[1].pos.x > quads[0].pos.x);
    CHECK(quads[3].pos.x > quads[2].pos.x);
}

TEST_CASE("ttf baking rejects garbage and empty input") {
    const std::vector<uint8_t> garbage(64, 0xAB);
    CHECK_FALSE(bake_ttf_font(garbage, 24.0f).valid());
    CHECK_FALSE(bake_ttf_font({}, 24.0f).valid());
}
