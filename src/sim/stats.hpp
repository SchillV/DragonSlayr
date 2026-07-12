#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace ds {

// The curated stat set (hybrid philosophy: stat NAMES are C++, values and
// combinations are data). Extending it is one enum value + one name-table
// entry + one field; every consumer reads through StatBlock::cached.
//
// Two layers share the block: effect stats (what the sim consumes) and the
// classic attribute sheet (what the player reasons about). Attributes derive
// effect stats inside recompute() — see the derivation table there — so
// items/feats/classes can speak either language ("+2 vit" or "+10% melee").
enum class StatId : uint8_t {
    // effect stats
    MaxHp,           // absolute hit points
    MoveSpeedMult,   // multiplies player move/dash speed
    DamageMult,      // multiplies ALL damage the player deals
    DefensePct,      // fraction of incoming damage prevented (capped at 0.9)
    FireRateMult,    // divides weapon cooldowns
    MeleeDamageMult, // multiplies melee (sword) damage only
    MagicDamageMult, // multiplies magic (bolt/spell) damage only
    // attributes (0 = baseline human; negative is allowed)
    Str, // melee power
    Dex, // attack + move speed
    Vit, // toughness
    Mag, // magic power
    Count,
};

const char* stat_name(StatId id);
bool stat_from_name(std::string_view name, StatId& out);

struct Stats {
    float max_hp = 100.0f;
    float move_speed_mult = 1.0f;
    float damage_mult = 1.0f;
    float defense_pct = 0.0f;
    float fire_rate_mult = 1.0f;
    float melee_damage_mult = 1.0f;
    float magic_damage_mult = 1.0f;
    float str = 0.0f;
    float dex = 0.0f;
    float vit = 0.0f;
    float mag = 0.0f;

    float& operator[](StatId id);
    float operator[](StatId id) const;
};

// One granted bonus. `source` identifies who granted it (an item def index)
// so removal is by source, not by fragile iterator bookkeeping.
struct Modifier {
    StatId stat = StatId::MaxHp;
    enum class Op : uint8_t { Add, Mult } op = Op::Add;
    float value = 0.0f;
    uint16_t source = 0xffff;
};

// Modifier `source` id spaces. Item modifiers use the item's def index
// directly; everything else gets a reserved range far above any plausible
// roster size so the spaces can never collide and code can strip a whole
// class of modifiers by range (e.g. temp buffs expiring at the stairs).
constexpr uint16_t kClassSource = 0xd000;    // the run's class attribute package
constexpr uint16_t kSkillSource = 0xd800;    // skill-tree attribute purchases
constexpr uint16_t kFeatSourceBase = 0xe000; // + feat def index
constexpr uint16_t kTempSourceBase = 0xf000; // timed buffs (unique token each)
constexpr uint16_t kTempSourceSpan = 0x0e00; // wraps before the 0xfffe/0xffff sentinels

// base + modifiers -> cached. Per stat, all Adds apply before all Mults, so
// the result is order-independent (insertion order never matters).
struct StatBlock {
    Stats base;
    std::vector<Modifier> mods;
    Stats cached;

    StatBlock() { recompute(); }

    void recompute();
    void add(Modifier m);
    void remove_source(uint16_t source);
};

} // namespace ds
