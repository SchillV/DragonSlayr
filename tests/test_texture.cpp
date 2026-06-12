#include <doctest/doctest.h>

#include "render/texture_load.hpp"

// These run with CWD = repo root (set by add_test WORKING_DIRECTORY).

TEST_CASE("legacy P3 PPM textures load as 64x64 RGBA") {
    const auto img = ds::load_image("assets/textures/pebble.ppm");
    REQUIRE(img.has_value());
    CHECK(img->width == 64);
    CHECK(img->height == 64);
    CHECK(img->pixels.size() == 64u * 64u * 4u);
    CHECK(img->pixels[3] == 255); // opaque by default
}

TEST_CASE("black_to_alpha keys out the legacy transparency color") {
    const auto img = ds::load_image("assets/textures/monster.ppm", /*black_to_alpha=*/true);
    REQUIRE(img.has_value());
    // The sprite sheet background is pure black, which becomes transparent.
    CHECK(img->pixels[3] == 0);
    // ...but not every pixel is transparent.
    bool any_opaque = false;
    for (size_t i = 3; i < img->pixels.size(); i += 4) {
        if (img->pixels[i] == 255) {
            any_opaque = true;
            break;
        }
    }
    CHECK(any_opaque);
}

TEST_CASE("missing image returns nullopt") {
    CHECK_FALSE(ds::load_image("assets/textures/does_not_exist.ppm").has_value());
}
