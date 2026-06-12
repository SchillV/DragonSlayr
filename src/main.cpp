#include "core/log.hpp"
#include "game/app.hpp"

#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace {

void print_usage() {
    ds::log_info("usage: dragonslayr [--headless] [--ticks N] [--seed N] [--bot NAME] "
                 "[--telemetry-dir PATH] [--print-map]");
}

template <typename T>
std::optional<T> parse_number(std::string_view text) {
    T value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

const char* next_value(int argc, char** argv, int& i, std::string_view flag) {
    if (i + 1 >= argc) {
        ds::log_error("missing value for {}", flag);
        return nullptr;
    }
    return argv[++i];
}

} // namespace

int main(int argc, char* argv[]) {
    ds::AppConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--print-map") {
            cfg.print_map = true;
        } else if (arg == "--ticks") {
            const char* value = next_value(argc, argv, i, arg);
            if (!value) return 2;
            const auto parsed = parse_number<int64_t>(value);
            if (!parsed) {
                ds::log_error("invalid --ticks value: {}", value);
                return 2;
            }
            cfg.max_ticks = *parsed;
        } else if (arg == "--seed") {
            const char* value = next_value(argc, argv, i, arg);
            if (!value) return 2;
            const auto parsed = parse_number<uint64_t>(value);
            if (!parsed) {
                ds::log_error("invalid --seed value: {}", value);
                return 2;
            }
            cfg.seed = *parsed;
        } else if (arg == "--bot") {
            const char* value = next_value(argc, argv, i, arg);
            if (!value) return 2;
            cfg.bot = value;
        } else if (arg == "--telemetry-dir") {
            const char* value = next_value(argc, argv, i, arg);
            if (!value) return 2;
            cfg.telemetry_dir = value;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            ds::log_error("unknown argument: {}", arg);
            print_usage();
            return 2;
        }
    }

    ds::App app(std::move(cfg));
    return app.run();
}
