#include <doctest/doctest.h>

#include "core/cvar.hpp"

using namespace ds;

// The registry is global; every name here is unique to this file.

TEST_CASE("cvar registration, lookup and console set/get") {
    CVar& cv = cvar_register("test.speed", 5.0f, "test cvar");
    CHECK(cvar_find("test.speed") == &cv);
    CHECK(cv.value == 5.0f);

    std::string fb;
    CHECK(con_execute("test.speed 7.5", fb));
    CHECK(cv.value == 7.5f);

    fb.clear();
    CHECK(con_execute("test.speed", fb));
    CHECK(fb.find("7.5") != std::string::npos);
}

TEST_CASE("bad input is rejected") {
    cvar_register("test.numeric", 1.0f, "test cvar");
    std::string fb;
    CHECK_FALSE(con_execute("test.numeric banana", fb));
    CHECK_FALSE(con_execute("test.does_not_exist 1", fb));
    CHECK(fb.find("unknown") != std::string::npos);
}

TEST_CASE("cheat cvars poison the run flag") {
    CVar& cheat = cvar_register("test.cheat", 0.0f, "test cheat", CVAR_CHEAT);
    cvar_reset_cheat_touched();
    CHECK_FALSE(cvar_any_cheat_touched());

    std::string fb;
    CHECK(con_execute("test.cheat 1", fb));
    CHECK(cvar_any_cheat_touched());
    CHECK(cheat.as_bool());

    cvar_reset_cheat_touched();
    CHECK_FALSE(cvar_any_cheat_touched());
}

TEST_CASE("commands receive their arguments") {
    std::string seen;
    con_register("test.echo", "echo args", [&](std::span<const std::string_view> args, std::string& fb) {
        for (const auto a : args) {
            seen += std::string(a) + ";";
        }
        fb = "ok";
    });
    std::string fb;
    CHECK(con_execute("test.echo alpha beta", fb));
    CHECK(seen == "alpha;beta;");
    CHECK(fb == "ok");
    con_unregister_all();
}

TEST_CASE("help is built in") {
    std::string fb;
    CHECK(con_execute("help", fb));
    CHECK(fb.find("cvars:") != std::string::npos);
}
