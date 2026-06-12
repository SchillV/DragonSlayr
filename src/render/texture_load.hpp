#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ds {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8, row-major, top-left origin
};

// PNG/JPG/TGA via stb_image; PPM (P3 ascii / P6 binary) via a tiny built-in
// parser because the legacy textures are PPMs. black_to_alpha applies the
// legacy sprite convention: pure black (0,0,0) is the transparency key.
std::optional<Image> load_image(const std::filesystem::path& path, bool black_to_alpha = false);

// Nearest-neighbor rescale — texture arrays need uniform layer dimensions and
// retro art wants hard pixels anyway.
Image scale_nearest(const Image& src, int w, int h);

} // namespace ds
