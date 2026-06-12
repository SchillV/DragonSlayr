#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace ds {

enum CvarFlags : uint32_t {
    CVAR_NONE = 0,
    CVAR_CHEAT = 1, // touching one of these poisons the run for telemetry/leaderboard
};

// Float-only on purpose (bools are 0/1): one storage type keeps the whole
// module trivial. Register at file scope:
//   static ds::CVar& sv_speed = ds::cvar_register("sv.run_speed", 8.0f, "tiles/s");
struct CVar {
    const char* name;
    float value;
    float default_value;
    const char* help;
    uint32_t flags;

    bool as_bool() const { return value != 0.0f; }
};

CVar& cvar_register(const char* name, float default_value, const char* help,
                    uint32_t flags = CVAR_NONE);
CVar* cvar_find(std::string_view name);
std::span<CVar* const> cvar_all();
void cvar_set(CVar& cvar, float value); // tracks cheat usage

bool cvar_any_cheat_touched();
void cvar_reset_cheat_touched(); // call on run start

using ConCommandFn = std::function<void(std::span<const std::string_view>, std::string& feedback)>;
void con_register(const char* name, const char* help, ConCommandFn fn);
void con_unregister_all(); // session teardown (commands capture session state)

// "sv.run_speed"     -> feedback shows the value
// "sv.run_speed 9.5" -> sets it
// "regen 1234"       -> runs the command with args
// Returns false for unknown names.
bool con_execute(std::string_view line, std::string& feedback);

// One help text covering every cvar and command.
std::string con_help();

} // namespace ds
