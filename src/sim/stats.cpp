#include "sim/stats.hpp"

#include <algorithm>
#include <array>
#include <cassert>

namespace ds {

namespace {

constexpr std::array<const char*, static_cast<size_t>(StatId::Count)> kNames = {
    "max_hp", "move_speed_mult", "damage_mult", "defense_pct", "fire_rate_mult",
};

} // namespace

const char* stat_name(StatId id) {
    const auto i = static_cast<size_t>(id);
    return i < kNames.size() ? kNames[i] : "?";
}

bool stat_from_name(std::string_view name, StatId& out) {
    for (size_t i = 0; i < kNames.size(); ++i) {
        if (name == kNames[i]) {
            out = static_cast<StatId>(i);
            return true;
        }
    }
    return false;
}

float& Stats::operator[](StatId id) {
    switch (id) {
    case StatId::MaxHp: return max_hp;
    case StatId::MoveSpeedMult: return move_speed_mult;
    case StatId::DamageMult: return damage_mult;
    case StatId::DefensePct: return defense_pct;
    case StatId::FireRateMult: return fire_rate_mult;
    case StatId::Count: break;
    }
    assert(false && "bad StatId");
    return max_hp;
}

float Stats::operator[](StatId id) const {
    return (*const_cast<Stats*>(this))[id];
}

void StatBlock::recompute() {
    cached = base;
    // Adds first, then mults, per stat — the composition players expect and
    // the one that keeps modifier order irrelevant.
    for (const Modifier& m : mods) {
        if (m.op == Modifier::Op::Add) {
            cached[m.stat] += m.value;
        }
    }
    for (const Modifier& m : mods) {
        if (m.op == Modifier::Op::Mult) {
            cached[m.stat] *= m.value;
        }
    }
    cached.defense_pct = std::clamp(cached.defense_pct, 0.0f, 0.9f);
    cached.max_hp = std::max(cached.max_hp, 1.0f);
    cached.fire_rate_mult = std::max(cached.fire_rate_mult, 0.05f);
    cached.move_speed_mult = std::max(cached.move_speed_mult, 0.05f);
    cached.damage_mult = std::max(cached.damage_mult, 0.0f);
}

void StatBlock::add(Modifier m) {
    mods.push_back(m);
    recompute();
}

void StatBlock::remove_source(uint16_t source) {
    std::erase_if(mods, [source](const Modifier& m) { return m.source == source; });
    recompute();
}

} // namespace ds
