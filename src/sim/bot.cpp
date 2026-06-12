#include "sim/bot.hpp"

#include "sim/collision.hpp"
#include "sim/components.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace ds {

PlayerCmd Bot::make_cmd(World& world) {
    PlayerCmd cmd;
    if (mode_.empty()) {
        return cmd;
    }
    const auto& tr = world.reg.get<Transform>(world.player);
    const glm::vec2 dir{std::cos(yaw_), std::sin(yaw_)};

    // Turn away when a wall is within a tile of travel.
    if (grid_raycast(world.map(), tr.pos, tr.pos + dir * 1.0f)) {
        yaw_ += glm::half_pi<float>() + (world.rng.next_float01() - 0.5f);
    }

    cmd.yaw = yaw_;
    cmd.wish_dir = {std::cos(yaw_), std::sin(yaw_)};
    cmd.run = true;

    if (mode_ == "walk_attack") {
        // Swing roughly twice a second; the combat system lands in M5, until
        // then the flag simply exercises the cmd path.
        cmd.attack_primary = (ticks_ % 30) == 0;
    }
    ++ticks_;
    return cmd;
}

} // namespace ds
