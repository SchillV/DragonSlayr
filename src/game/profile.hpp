#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ds {

struct ContentDB;
struct World;

struct ProfileRecords {
    int runs = 0;
    int best_floor = 0;
    int best_score = 0;
    int kills = 0;
};

// One save slot: everything that outlives a run. IO stays in the game layer
// (the sim never reads disk); meta effects re-enter the sim as ordinary
// kMetaSource modifiers at run start.
struct Profile {
    int slot = 1; // 1..kProfileSlots
    std::string class_id;
    bool intro_done = false;
    int embers = 0;                      // meta currency, earned per run
    std::map<std::string, int> upgrades; // hub upgrade id -> owned ranks
    ProfileRecords records;
    std::string brain; // wyrm-brain state (opaque JSON blob, H5)

    bool loaded = false; // false = boot state, no profile active
};

constexpr int kProfileSlots = 3;

std::filesystem::path profile_path(const std::filesystem::path& dir, int slot);
bool save_profile(const std::filesystem::path& dir, const Profile& profile,
                  std::string* error = nullptr);
bool load_profile(const std::filesystem::path& dir, int slot, Profile& out,
                  std::string* error = nullptr);
bool delete_profile(const std::filesystem::path& dir, int slot);

// One line per slot for the menus ("KNIGHT · 12 RUNS · FLOOR 7").
struct ProfileSummary {
    int slot = 1;
    bool exists = false;
    std::string line;
};
std::vector<ProfileSummary> list_profiles(const std::filesystem::path& dir);

// Embers earned by a finished run (design lever, unit-tested).
int embers_for_run(int score, int floor_reached);

// Pushes the profile's owned upgrade ranks into the player's StatBlock as
// kMetaSource modifiers (rank times each) and refreshes. Call at run start.
void apply_meta_upgrades(World& world, const Profile& profile);

} // namespace ds
