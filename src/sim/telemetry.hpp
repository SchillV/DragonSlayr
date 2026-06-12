#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace ds {

struct ContentDB;

// The boss-learning gimmick's input: every combat-relevant action, recorded
// per run and persisted as versioned JSON. Events always carry def indices
// (resolved to id strings on write) and positions, so future aggregation
// (melee ratio, engagement distance, strafe dominance, dodge-after-windup)
// needs no schema change.
enum class EvType : uint8_t {
    RunStart,
    RunEnd,
    PlayerAttack,     // a = damage, def = target enemy (or 0xffff), flags bit0 = hit
    ProjectileFired,  // def = weapon
    ProjectileHit,    // a = damage, def = target enemy (0xffff = wall)
    PlayerDamaged,    // a = damage, b = hp after, def = source enemy
    PlayerDash,       // a, b = dash direction
    PlayerMoveSample, // a, b = velocity (4 Hz downsample)
    EnemyKilled,      // def = enemy, a = weapon def index, b = seconds alive
};

struct TelemetryEvent {
    uint32_t tick = 0;
    EvType type = EvType::RunStart;
    uint8_t flags = 0;
    uint16_t def = 0xffff;
    float x = 0.0f, y = 0.0f; // world position context
    float yaw = 0.0f;
    float a = 0.0f, b = 0.0f; // event-specific payload
};

class TelemetryRecorder {
public:
    void begin_run(uint64_t seed);
    void record(TelemetryEvent ev);

    // Total events ever recorded this run (monotonic, ignores ring eviction).
    uint64_t total() const { return total_; }
    // Events with absolute index in [cursor, total) that are still in the
    // ring; advances cursor. Drives audio/vfx without a second event system.
    void drain_since(uint64_t& cursor, std::vector<TelemetryEvent>& out) const;

    size_t size() const { return ring_.size(); }
    std::span<const TelemetryEvent> events() const { return ring_; }

    // Writes <dir>/run_<utc>_<id>.json; def indices resolve to id strings via
    // the content db. Returns the written path or empty on failure.
    std::filesystem::path write_json(const std::filesystem::path& dir, const ContentDB& content,
                                     std::string_view outcome, bool cheats_used) const;

    static constexpr size_t kCapacity = 65536;

private:
    std::vector<TelemetryEvent> ring_; // newest kept; oldest evicted
    uint64_t total_ = 0;
    uint64_t seed_ = 0;
    std::string run_id_;
    std::string started_utc_;
};

} // namespace ds
