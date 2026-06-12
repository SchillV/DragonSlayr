#include "core/cvar.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <format>
#include <memory>
#include <vector>

namespace ds {

namespace {

struct ConCommand {
    std::string name;
    std::string help;
    ConCommandFn fn;
};

std::vector<std::unique_ptr<CVar>>& cvar_registry() {
    static std::vector<std::unique_ptr<CVar>> registry;
    return registry;
}

std::vector<CVar*>& cvar_pointers() {
    static std::vector<CVar*> pointers;
    return pointers;
}

std::vector<ConCommand>& command_registry() {
    static std::vector<ConCommand> registry;
    return registry;
}

bool g_cheat_touched = false;

std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ') ++i;
        if (i > start) {
            tokens.push_back(line.substr(start, i - start));
        }
    }
    return tokens;
}

} // namespace

CVar& cvar_register(const char* name, float default_value, const char* help, uint32_t flags) {
    assert(cvar_find(name) == nullptr && "duplicate cvar name");
    auto& registry = cvar_registry();
    registry.push_back(std::make_unique<CVar>(CVar{name, default_value, default_value, help, flags}));
    cvar_pointers().push_back(registry.back().get());
    return *registry.back();
}

CVar* cvar_find(std::string_view name) {
    for (CVar* cvar : cvar_pointers()) {
        if (name == cvar->name) {
            return cvar;
        }
    }
    return nullptr;
}

std::span<CVar* const> cvar_all() {
    return cvar_pointers();
}

void cvar_set(CVar& cvar, float value) {
    cvar.value = value;
    if (cvar.flags & CVAR_CHEAT) {
        g_cheat_touched = true;
    }
}

bool cvar_any_cheat_touched() {
    return g_cheat_touched;
}

void cvar_reset_cheat_touched() {
    g_cheat_touched = false;
}

void con_register(const char* name, const char* help, ConCommandFn fn) {
    command_registry().push_back({name, help, std::move(fn)});
}

void con_unregister_all() {
    command_registry().clear();
}

bool con_execute(std::string_view line, std::string& feedback) {
    const auto tokens = tokenize(line);
    if (tokens.empty()) {
        return true;
    }
    const std::string_view head = tokens[0];

    if (CVar* cvar = cvar_find(head)) {
        if (tokens.size() == 1) {
            feedback = std::format("{} = {} (default {}) — {}", cvar->name, cvar->value,
                                   cvar->default_value, cvar->help);
            return true;
        }
        float value = 0.0f;
        const std::string_view arg = tokens[1];
        const auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
        if (ec != std::errc{} || ptr != arg.data() + arg.size()) {
            feedback = std::format("not a number: '{}'", arg);
            return false;
        }
        cvar_set(*cvar, value);
        feedback = std::format("{} = {}{}", cvar->name, value,
                               (cvar->flags & CVAR_CHEAT) ? "  [CHEAT]" : "");
        return true;
    }

    for (ConCommand& cmd : command_registry()) {
        if (head == cmd.name) {
            cmd.fn(std::span<const std::string_view>(tokens).subspan(1), feedback);
            return true;
        }
    }
    if (head == "help") {
        feedback = con_help();
        return true;
    }
    feedback = std::format("unknown cvar or command: '{}'", head);
    return false;
}

std::string con_help() {
    std::string out = "commands:\n";
    for (const ConCommand& cmd : command_registry()) {
        out += std::format("  {:<18} {}\n", cmd.name, cmd.help);
    }
    out += "cvars:\n";
    for (const CVar* cvar : cvar_pointers()) {
        out += std::format("  {:<18} {} (default {}){}\n", cvar->name, cvar->value,
                           cvar->default_value, (cvar->flags & CVAR_CHEAT) ? " [CHEAT]" : "");
    }
    return out;
}

} // namespace ds
