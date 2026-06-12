#pragma once

#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

namespace ds {

template <typename T>
class Grid2D {
public:
    Grid2D() = default;
    Grid2D(int w, int h, T fill = {})
        : w_(w), h_(h), cells_(static_cast<size_t>(w) * static_cast<size_t>(h), fill) {}

    int width() const { return w_; }
    int height() const { return h_; }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }

    T& at(int x, int y) {
        assert(in_bounds(x, y));
        return cells_[static_cast<size_t>(y) * w_ + x];
    }
    const T& at(int x, int y) const {
        assert(in_bounds(x, y));
        return cells_[static_cast<size_t>(y) * w_ + x];
    }

    T get_or(int x, int y, T fallback) const { return in_bounds(x, y) ? at(x, y) : fallback; }

    std::span<const T> cells() const { return cells_; }
    std::span<T> cells() { return cells_; }

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<T> cells_;
};

} // namespace ds
