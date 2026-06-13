#include <doctest/doctest.h>

#include "core/rng.hpp"
#include "sim/content.hpp"
#include "sim/world.hpp"

using namespace ds;

namespace {

ContentDB roster() {
    ContentDB db;
    // walker everywhere; brute only from floor 3.
    REQUIRE(db.load_enemies_from_string(R"({
        "walker": {"hp":12,"sprite":"m","spawn_weight":1.0,"min_floor":1},
        "brute":  {"hp":30,"sprite":"m","spawn_weight":1.0,"min_floor":3}
    })"));
    return db;
}

} // namespace

TEST_CASE("min_floor gates eligibility") {
    const ContentDB db = roster();
    Rng rng(123);
    for (int i = 0; i < 32; ++i) {
        const int idx = pick_enemy_for_floor(db, 1, rng);
        REQUIRE(idx >= 0);
        CHECK(db.enemies[static_cast<size_t>(idx)].id == "walker"); // brute not yet eligible
    }
}

TEST_CASE("both enemies appear once eligible") {
    const ContentDB db = roster();
    Rng rng(123);
    bool saw_walker = false, saw_brute = false;
    for (int i = 0; i < 200; ++i) {
        const std::string& id = db.enemies[static_cast<size_t>(pick_enemy_for_floor(db, 3, rng))].id;
        saw_walker |= id == "walker";
        saw_brute |= id == "brute";
    }
    CHECK(saw_walker);
    CHECK(saw_brute);
}

TEST_CASE("selection is deterministic for a seed") {
    const ContentDB db = roster();
    Rng a(7), b(7);
    for (int i = 0; i < 64; ++i) {
        CHECK(pick_enemy_for_floor(db, 5, a) == pick_enemy_for_floor(db, 5, b));
    }
}

TEST_CASE("weighting is roughly proportional") {
    ContentDB db;
    REQUIRE(db.load_enemies_from_string(R"({
        "common": {"hp":1,"sprite":"m","spawn_weight":3.0},
        "rare":   {"hp":1,"sprite":"m","spawn_weight":1.0}
    })"));
    Rng rng(99);
    int common = 0;
    const int n = 4000;
    for (int i = 0; i < n; ++i) {
        if (db.enemies[static_cast<size_t>(pick_enemy_for_floor(db, 1, rng))].id == "common") {
            ++common;
        }
    }
    // Expected ~75% common; allow a generous band.
    CHECK(common > n * 0.68);
    CHECK(common < n * 0.82);
}

TEST_CASE("zero weight and empty rosters never spawn") {
    ContentDB db;
    REQUIRE(db.load_enemies_from_string(R"({"ghost":{"hp":1,"sprite":"m","spawn_weight":0.0}})"));
    Rng rng(1);
    CHECK(pick_enemy_for_floor(db, 1, rng) == -1);

    ContentDB empty;
    Rng r2(1);
    CHECK(pick_enemy_for_floor(empty, 1, r2) == -1);
}

TEST_CASE("spawn_weight and min_floor default sensibly") {
    ContentDB db;
    REQUIRE(db.load_enemies_from_string(R"({"x":{"hp":5,"sprite":"m"}})"));
    CHECK(db.enemies[0].spawn_weight == doctest::Approx(1.0f));
    CHECK(db.enemies[0].min_floor == 1);
}
