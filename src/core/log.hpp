#pragma once

#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace ds {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void log_message(LogLevel level, std::string_view msg);
void log_set_min_level(LogLevel level);
// Mirrors every emitted message (e.g. into the in-game console). One sink is enough here.
void log_set_sink(std::function<void(LogLevel, std::string_view)> sink);

template <typename... Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void log_info(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace ds
