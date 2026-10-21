#pragma once

#ifdef __linux__

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace endstone::runtime {

struct EntityAiGoalLayout {
    std::size_t vtable;
    std::size_t owner_offset;
};

inline constexpr EntityAiGoalLayout sleep_goal_layout{0xEEFC570, 0xB8};
inline constexpr EntityAiGoalLayout nearest_attackable_target_goal_layout{0xF242670, 0x28};
inline constexpr EntityAiGoalLayout random_stroll_goal_layout{0xF242178, 0x28};
inline constexpr EntityAiGoalLayout random_look_around_goal_layout{0xEEE7378, 0x40};

struct EntityAiHook {
    std::string_view name;
    std::size_t function;
    const EntityAiGoalLayout &layout;
    std::size_t slot;
};

inline constexpr std::array entity_ai_hooks = {
    EntityAiHook{"_ZN9SleepGoal6canUseEv", 0x592D200, sleep_goal_layout, 2},
    EntityAiHook{"_ZN9SleepGoal11canContinueEv", 0x592DB20, sleep_goal_layout, 3},
    EntityAiHook{"endstone_perf_find_nearest_target", 0xBDD7710, nearest_attackable_target_goal_layout, 13},
    EntityAiHook{"_ZN16RandomStrollGoal6canUseEv", 0xBDC8D30, random_stroll_goal_layout, 2},
    EntityAiHook{"_ZN16RandomStrollGoal11canContinueEv", 0xBDC8E30, random_stroll_goal_layout, 3},
    EntityAiHook{"_ZN20RandomLookAroundGoal6canUseEv", 0x5761110, random_look_around_goal_layout, 2},
    EntityAiHook{"_ZN20RandomLookAroundGoal11canContinueEv", 0x5761160, random_look_around_goal_layout, 3},
};

bool entity_ai_preflight(std::string &reason);
bool is_entity_ai_hook(std::string_view name);

}  // namespace endstone::runtime

#endif
