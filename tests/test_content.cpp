#include <doctest/doctest.h>

#include "sim/content.hpp"

using namespace ds;

namespace {

constexpr const char* kValidRoster = R"json({
  "walker": {
    "name": "Dungeon Walker",
    "hp": 12,
    "speed": 3.0,
    "radius": 0.35,
    "aggro_radius": 10.0,
    "sprite": "monster",
    "sprite_size": [0.9, 0.9],
    "score": 50,
    "attack": { "damage": 6, "range": 0.9, "windup_s": 0.35, "cooldown_s": 0.8 }
  },
  "brute": {
    "hp": 30,
    "sprite": "monster"
  }
})json";

} // namespace

TEST_CASE("a valid roster parses with explicit values and defaults") {
    ContentDB db;
    std::string error;
    REQUIRE(db.load_enemies_from_string(kValidRoster, &error));
    REQUIRE(db.enemies.size() == 2);

    const int walker = db.find_enemy("walker");
    REQUIRE(walker >= 0);
    const EnemyDef& w = db.enemies[static_cast<size_t>(walker)];
    CHECK(w.name == "Dungeon Walker");
    CHECK(w.hp == 12.0f);
    CHECK(w.attack.damage == 6.0f);
    CHECK(w.sprite_size.x == doctest::Approx(0.9f));

    const int brute = db.find_enemy("brute");
    REQUIRE(brute >= 0);
    const EnemyDef& b = db.enemies[static_cast<size_t>(brute)];
    CHECK(b.name == "brute");               // defaults to the id
    CHECK(b.speed == 3.0f);                 // documented default
    CHECK(b.aggro_radius == 10.0f);         // documented default
    CHECK(b.attack.windup_s == 0.35f);      // attack block omitted entirely

    CHECK(db.find_enemy("dragon") == -1);
}

TEST_CASE("missing required fields name the key path") {
    ContentDB db;
    std::string error;
    CHECK_FALSE(db.load_enemies_from_string(R"({"bad": {"sprite": "x"}})", &error));
    CHECK(error.find("enemies.bad.hp") != std::string::npos);
}

TEST_CASE("wrong types name the key path") {
    ContentDB db;
    std::string error;
    CHECK_FALSE(db.load_enemies_from_string(R"({"bad": {"hp": "lots", "sprite": "x"}})", &error));
    CHECK(error.find("enemies.bad.hp") != std::string::npos);

    error.clear();
    CHECK_FALSE(
        db.load_enemies_from_string(R"({"bad": {"hp": 5, "sprite": "x", "attack": 7}})", &error));
    CHECK(error.find("enemies.bad.attack") != std::string::npos);
}

TEST_CASE("a failed load leaves the db unchanged") {
    ContentDB db;
    REQUIRE(db.load_enemies_from_string(kValidRoster));
    REQUIRE(db.enemies.size() == 2);

    std::string error;
    CHECK_FALSE(db.load_enemies_from_string("not json at all", &error));
    CHECK(db.enemies.size() == 2);
    CHECK(db.find_enemy("walker") >= 0);
}

TEST_CASE("missing file reports its path") {
    ContentDB db;
    std::string error;
    CHECK_FALSE(db.load_enemies("/nonexistent/enemies.json", &error));
    CHECK(error.find("/nonexistent/enemies.json") != std::string::npos);
}
