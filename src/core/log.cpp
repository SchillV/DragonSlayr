#include "core/log.hpp"

#include <cstdio>
#include <mutex>

namespace ds {

namespace {

LogLevel g_min_level = LogLevel::Debug;
std::function<void(LogLevel, std::string_view)> g_sink;
std::mutex g_mutex;

const char* prefix(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "[dbg]";
    case LogLevel::Info: return "[inf]";
    case LogLevel::Warn: return "[WRN]";
    case LogLevel::Error: return "[ERR]";
    }
    return "[???]";
}

} // namespace

void log_message(LogLevel level, std::string_view msg) {
    std::lock_guard lock(g_mutex);
    if (level < g_min_level) {
        return;
    }
    std::FILE* out = level >= LogLevel::Warn ? stderr : stdout;
    std::fprintf(out, "%s %.*s\n", prefix(level), static_cast<int>(msg.size()), msg.data());
    std::fflush(out);
    if (g_sink) {
        g_sink(level, msg);
    }
}

void log_set_min_level(LogLevel level) {
    std::lock_guard lock(g_mutex);
    g_min_level = level;
}

void log_set_sink(std::function<void(LogLevel, std::string_view)> sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

} // namespace ds
