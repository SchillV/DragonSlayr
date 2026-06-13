#include "sim/telemetry.hpp"

#include "core/log.hpp"
#include "sim/content.hpp"

#include <nlohmann/json.hpp>

#include <ctime>
#include <format>
#include <fstream>
#include <random>

namespace ds {

namespace {

const char* type_name(EvType t) {
    switch (t) {
    case EvType::RunStart: return "run_start";
    case EvType::RunEnd: return "run_end";
    case EvType::PlayerAttack: return "player_attack";
    case EvType::ProjectileFired: return "projectile_fired";
    case EvType::ProjectileHit: return "projectile_hit";
    case EvType::PlayerDamaged: return "player_damaged";
    case EvType::PlayerDash: return "player_dash";
    case EvType::PlayerMoveSample: return "player_move_sample";
    case EvType::EnemyKilled: return "enemy_killed";
    }
    return "unknown";
}

std::string utc_now_compact() {
    const std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
    // std::gmtime is not thread-safe and trips MSVC's deprecation warning under
    // /W4; use each platform's reentrant variant instead.
#if defined(_WIN32)
    if (gmtime_s(&tm_buf, &t) != 0) {
        return {};
    }
#else
    if (gmtime_r(&t, &tm_buf) == nullptr) {
        return {};
    }
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm_buf);
    return buf;
}

} // namespace

void TelemetryRecorder::begin_run(uint64_t seed) {
    ring_.clear();
    ring_.reserve(1024);
    total_ = 0;
    seed_ = seed;
    started_utc_ = utc_now_compact();
    // Uniqueness, not security: wall clock + hardware entropy.
    std::random_device rd;
    run_id_ = std::format("{:08x}{:08x}", rd(), static_cast<uint32_t>(seed));
}

void TelemetryRecorder::record(TelemetryEvent ev) {
    if (ring_.size() >= kCapacity) {
        ring_.erase(ring_.begin()); // rare in practice; keep newest
    }
    ring_.push_back(ev);
    ++total_;
}

void TelemetryRecorder::drain_since(uint64_t& cursor, std::vector<TelemetryEvent>& out) const {
    out.clear();
    if (cursor >= total_) {
        return;
    }
    const uint64_t evicted = total_ - ring_.size();
    const uint64_t first = cursor > evicted ? cursor : evicted;
    for (uint64_t i = first; i < total_; ++i) {
        out.push_back(ring_[static_cast<size_t>(i - evicted)]);
    }
    cursor = total_;
}

std::filesystem::path TelemetryRecorder::write_json(const std::filesystem::path& dir,
                                                    const ContentDB& content,
                                                    std::string_view outcome,
                                                    bool cheats_used) const {
    nlohmann::json doc;
    doc["version"] = 1;
    doc["run_id"] = run_id_;
    doc["seed"] = seed_;
    doc["started_utc"] = started_utc_;
    doc["outcome"] = std::string(outcome);
    doc["cheats_used"] = cheats_used;
    doc["events_total"] = total_;
    doc["events_kept"] = ring_.size();

    auto def_name = [&](uint16_t def) -> std::string {
        if (def == 0xffff) {
            return "";
        }
        if (def < content.enemies.size()) {
            return content.enemies[def].id;
        }
        return std::format("#{}", def);
    };
    auto weapon_name = [&](int idx) -> std::string {
        if (idx >= 0 && static_cast<size_t>(idx) < content.weapons.size()) {
            return content.weapons[static_cast<size_t>(idx)].id;
        }
        return "";
    };

    nlohmann::json events = nlohmann::json::array();
    for (const TelemetryEvent& ev : ring_) {
        nlohmann::json e;
        e["t"] = ev.tick;
        e["type"] = type_name(ev.type);
        switch (ev.type) {
        case EvType::PlayerAttack:
            e["weapon"] = weapon_name(static_cast<int>(ev.flags >> 1));
            e["hit"] = (ev.flags & 1) != 0;
            e["target"] = def_name(ev.def);
            e["dmg"] = ev.a;
            e["pos"] = {ev.x, ev.y};
            e["yaw"] = ev.yaw;
            break;
        case EvType::ProjectileFired:
            e["weapon"] = weapon_name(ev.def);
            e["pos"] = {ev.x, ev.y};
            e["yaw"] = ev.yaw;
            break;
        case EvType::ProjectileHit:
            e["target"] = def_name(ev.def);
            e["dmg"] = ev.a;
            e["pos"] = {ev.x, ev.y};
            break;
        case EvType::PlayerDamaged:
            e["src"] = def_name(ev.def);
            e["dmg"] = ev.a;
            e["hp_after"] = ev.b;
            e["pos"] = {ev.x, ev.y};
            break;
        case EvType::PlayerDash:
            e["dir"] = {ev.a, ev.b};
            e["pos"] = {ev.x, ev.y};
            break;
        case EvType::PlayerMoveSample:
            e["pos"] = {ev.x, ev.y};
            e["vel"] = {ev.a, ev.b};
            e["yaw"] = ev.yaw;
            break;
        case EvType::EnemyKilled:
            e["def"] = def_name(ev.def);
            e["weapon"] = weapon_name(static_cast<int>(ev.a));
            e["alive_s"] = ev.b;
            e["pos"] = {ev.x, ev.y};
            break;
        case EvType::RunStart:
        case EvType::RunEnd:
            break;
        }
        events.push_back(std::move(e));
    }
    doc["events"] = std::move(events);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / std::format("run_{}_{}.json", started_utc_, run_id_);
    std::ofstream f(path);
    if (!f) {
        log_error("cannot write telemetry to {}", path.string());
        return {};
    }
    f << doc.dump(1, '\t') << '\n';
    log_info("telemetry written: {} ({} events)", path.string(), ring_.size());
    return path;
}

} // namespace ds
