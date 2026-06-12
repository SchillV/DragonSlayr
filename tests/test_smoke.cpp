#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/log.hpp"

#include <string>

TEST_CASE("log sink mirrors formatted messages") {
    std::string captured;
    ds::log_set_sink([&](ds::LogLevel, std::string_view msg) { captured = std::string(msg); });

    ds::log_info("hello {}", 42);
    CHECK(captured == "hello 42");

    ds::log_set_sink(nullptr);
}

TEST_CASE("log min level filters below threshold") {
    int calls = 0;
    ds::log_set_sink([&](ds::LogLevel, std::string_view) { ++calls; });

    ds::log_set_min_level(ds::LogLevel::Error);
    ds::log_info("filtered");
    CHECK(calls == 0);

    ds::log_error("passes");
    CHECK(calls == 1);

    ds::log_set_min_level(ds::LogLevel::Debug);
    ds::log_set_sink(nullptr);
}
