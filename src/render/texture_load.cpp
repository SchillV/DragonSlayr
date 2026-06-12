#include "render/texture_load.hpp"

#include "core/log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

#include <fstream>
#include <iterator>

namespace ds {

namespace {

std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::nullopt;
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

bool is_ppm_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Minimal PPM reader: P3 (ascii) and P6 (binary), maxval <= 255 — exactly what
// the legacy textures use.
std::optional<Image> parse_ppm(const std::vector<uint8_t>& data) {
    const bool binary = data[1] == '6';
    size_t pos = 2;

    auto skip_space_and_comments = [&] {
        while (pos < data.size()) {
            if (is_ppm_space(data[pos])) {
                ++pos;
            } else if (data[pos] == '#') {
                while (pos < data.size() && data[pos] != '\n') ++pos;
            } else {
                break;
            }
        }
    };
    auto next_int = [&]() -> int {
        skip_space_and_comments();
        int value = 0;
        bool any = false;
        while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9') {
            value = value * 10 + (data[pos] - '0');
            ++pos;
            any = true;
        }
        return any ? value : -1;
    };

    const int w = next_int();
    const int h = next_int();
    const int maxval = next_int();
    if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 255) {
        return std::nullopt;
    }

    Image img;
    img.width = w;
    img.height = h;
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    img.pixels.resize(count * 4);

    if (binary) {
        ++pos; // exactly one whitespace byte separates the header from the raster
        if (data.size() < pos + count * 3) {
            return std::nullopt;
        }
        for (size_t i = 0; i < count; ++i) {
            img.pixels[i * 4 + 0] = data[pos + i * 3 + 0];
            img.pixels[i * 4 + 1] = data[pos + i * 3 + 1];
            img.pixels[i * 4 + 2] = data[pos + i * 3 + 2];
            img.pixels[i * 4 + 3] = 255;
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            const int r = next_int();
            const int g = next_int();
            const int b = next_int();
            if (r < 0 || g < 0 || b < 0) {
                return std::nullopt;
            }
            img.pixels[i * 4 + 0] = static_cast<uint8_t>(r);
            img.pixels[i * 4 + 1] = static_cast<uint8_t>(g);
            img.pixels[i * 4 + 2] = static_cast<uint8_t>(b);
            img.pixels[i * 4 + 3] = 255;
        }
    }
    return img;
}

} // namespace

std::optional<Image> load_image(const std::filesystem::path& path, bool black_to_alpha) {
    const auto data = read_file(path);
    if (!data || data->size() < 2) {
        log_error("failed to read image: {}", path.string());
        return std::nullopt;
    }

    std::optional<Image> img;
    if ((*data)[0] == 'P' && ((*data)[1] == '3' || (*data)[1] == '6')) {
        img = parse_ppm(*data);
    } else {
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = stbi_load_from_memory(data->data(), static_cast<int>(data->size()), &w, &h, &comp, 4);
        if (px) {
            img = Image{w, h, std::vector<uint8_t>(px, px + static_cast<size_t>(w) * h * 4)};
            stbi_image_free(px);
        }
    }
    if (!img) {
        log_error("failed to decode image: {}", path.string());
        return std::nullopt;
    }

    if (black_to_alpha) {
        for (size_t i = 0; i + 3 < img->pixels.size(); i += 4) {
            if (img->pixels[i] == 0 && img->pixels[i + 1] == 0 && img->pixels[i + 2] == 0) {
                img->pixels[i + 3] = 0;
            }
        }
    }
    return img;
}

} // namespace ds
