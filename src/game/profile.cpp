#include "game/profile.hpp"

#include "core/log.hpp"
#include "sim/components.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>

namespace ds {

namespace {

using nlohmann::json;

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace

std::filesystem::path profile_path(const std::filesystem::path& dir, int slot) {
    return dir / std::format("slot_{}.json", slot);
}

bool save_profile(const std::filesystem::path& dir, const Profile& profile, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    json doc;
    doc["version"] = 1;
    doc["class"] = profile.class_id;
    doc["intro_done"] = profile.intro_done;
    doc["embers"] = profile.embers;
    doc["upgrades"] = profile.upgrades;
    doc["records"] = {{"runs", profile.records.runs},
                      {"best_floor", profile.records.best_floor},
                      {"best_score", profile.records.best_score},
                      {"kills", profile.records.kills}};
    if (!profile.brain.empty()) {
        // Stored as parsed JSON so the file stays human-readable.
        const json brain = json::parse(profile.brain, nullptr, /*allow_exceptions=*/false);
        doc["brain"] = brain.is_discarded() ? json(profile.brain) : brain;
    }

    std::ofstream f(profile_path(dir, profile.slot));
    if (!f) {
        if (error) {
            *error = std::format("cannot write {}", profile_path(dir, profile.slot).string());
        }
        return false;
    }
    f << doc.dump(2) << '\n';
    return true;
}

bool load_profile(const std::filesystem::path& dir, int slot, Profile& out, std::string* error) {
    std::ifstream f(profile_path(dir, slot));
    if (!f) {
        if (error) {
            *error = std::format("no profile in slot {}", slot);
        }
        return false;
    }
    const json doc = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        if (error) {
            *error = std::format("slot {} is not valid JSON", slot);
        }
        return false;
    }
    Profile p;
    p.slot = slot;
    p.class_id = doc.value("class", "");
    p.intro_done = doc.value("intro_done", false);
    p.embers = doc.value("embers", 0);
    if (doc.contains("upgrades") && doc["upgrades"].is_object()) {
        for (const auto& [id, rank] : doc["upgrades"].items()) {
            if (rank.is_number_integer()) {
                p.upgrades[id] = rank.get<int>();
            }
        }
    }
    if (doc.contains("records") && doc["records"].is_object()) {
        const json& r = doc["records"];
        p.records.runs = r.value("runs", 0);
        p.records.best_floor = r.value("best_floor", 0);
        p.records.best_score = r.value("best_score", 0);
        p.records.kills = r.value("kills", 0);
    }
    if (doc.contains("brain")) {
        p.brain = doc["brain"].is_string() ? doc["brain"].get<std::string>() : doc["brain"].dump();
    }
    p.loaded = true;
    out = std::move(p);
    return true;
}

bool delete_profile(const std::filesystem::path& dir, int slot) {
    std::error_code ec;
    return std::filesystem::remove(profile_path(dir, slot), ec);
}

std::vector<ProfileSummary> list_profiles(const std::filesystem::path& dir) {
    std::vector<ProfileSummary> out;
    for (int slot = 1; slot <= kProfileSlots; ++slot) {
        ProfileSummary s;
        s.slot = slot;
        Profile p;
        if (load_profile(dir, slot, p)) {
            s.exists = true;
            s.line = std::format("{} · {} RUNS · FLOOR {} · {}",
                                 p.class_id.empty() ? "UNBOUND" : upper(p.class_id),
                                 p.records.runs, p.records.best_floor, p.records.best_score);
        }
        out.push_back(std::move(s));
    }
    return out;
}

int embers_for_run(int score, int floor_reached) {
    return std::max(0, score / 10 + 25 * std::max(floor_reached - 1, 0));
}

void apply_meta_upgrades(World& world, const Profile& profile) {
    auto& stats = world.reg.get<StatBlock>(world.player);
    stats.remove_source(kMetaSource); // idempotent re-application
    for (const auto& [id, ranks] : profile.upgrades) {
        const int idx = world.content.find_upgrade(id);
        if (idx < 0) {
            log_warn("profile upgrade '{}' no longer exists; ignored", id);
            continue;
        }
        const UpgradeDef& def = world.content.upgrades[static_cast<size_t>(idx)];
        const int capped = std::min(ranks, def.max_ranks);
        for (int r = 0; r < capped; ++r) {
            for (const ItemModifierDef& m : def.modifiers) {
                stats.mods.push_back({m.stat, m.op, m.value, kMetaSource});
            }
        }
    }
    world.refresh_player_stats();
    // Meta power arrives before the first swing: start whole.
    auto& hp = world.reg.get<Health>(world.player);
    hp.hp = hp.max_hp;
}

} // namespace ds
