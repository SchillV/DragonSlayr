#include "sim/stats.hpp"

#include <algorithm>
#include <array>
#include <cassert>

namespace ds {

namespace {

constexpr std::array<const char*, static_cast<size_t>(StatId::Count)> kNames = {
    "max_hp",           "move_speed_mult",  "damage_mult", "defense_pct", "fire_rate_mult",
    "melee_damage_mult", "magic_damage_mult", "str",        "dex",         "vit",
    "mag",
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
    case StatId::MeleeDamageMult: return melee_damage_mult;
    case StatId::MagicDamageMult: return magic_damage_mult;
    case StatId::Str: return str;
    case StatId::Dex: return dex;
    case StatId::Vit: return vit;
    case StatId::Mag: return mag;
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

    // Attribute derivations — the curated bridge from the classic sheet to
    // the effect stats. Kept to one felt lever (or two) per attribute:
    //   STR  +5% melee damage per point
    //   DEX  +3% attack speed, +2% move speed per point
    //   VIT  +8 max HP, +1% defense per point
    //   MAG  +5% magic damage per point
    // Applied after modifiers, before clamps, so "+2 vit" items and "+10%
    // melee" items compose predictably.
    cached.melee_damage_mult *= 1.0f + 0.05f * cached.str;
    cached.magic_damage_mult *= 1.0f + 0.05f * cached.mag;
    cached.fire_rate_mult *= 1.0f + 0.03f * cached.dex;
    cached.move_speed_mult *= 1.0f + 0.02f * cached.dex;
    cached.max_hp += 8.0f * cached.vit;
    cached.defense_pct += 0.01f * cached.vit;

    cached.defense_pct = std::clamp(cached.defense_pct, 0.0f, 0.9f);
    cached.max_hp = std::max(cached.max_hp, 1.0f);
    cached.fire_rate_mult = std::max(cached.fire_rate_mult, 0.05f);
    cached.move_speed_mult = std::max(cached.move_speed_mult, 0.05f);
    cached.damage_mult = std::max(cached.damage_mult, 0.0f);
    cached.melee_damage_mult = std::max(cached.melee_damage_mult, 0.1f);
    cached.magic_damage_mult = std::max(cached.magic_damage_mult, 0.1f);
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
