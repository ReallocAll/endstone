#pragma once

#ifdef __linux__

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "bedrock/symbol.h"

namespace endstone::runtime {

struct MobAiGoalLayout {
    std::size_t vtable;
    std::size_t owner_offset;
};

inline constexpr MobAiGoalLayout sleep_goal_layout{0xEEFC570, 0xB8};
inline constexpr MobAiGoalLayout nearest_attackable_target_goal_layout{0xF242670, 0x28};
inline constexpr MobAiGoalLayout random_stroll_goal_layout{0xF242178, 0x28};
inline constexpr MobAiGoalLayout random_look_around_goal_layout{0xEEE7378, 0x40};

struct MobAiHook {
    consteval MobAiHook(std::string_view name, const MobAiGoalLayout &layout, std::size_t slot)
        : name(name), function(get_symbol(name)), layout(layout), slot(slot)
    {
    }

    std::string_view name;
    std::size_t function;
    const MobAiGoalLayout &layout;
    std::size_t slot;
};

inline constexpr std::array mob_ai_hooks = {
    MobAiHook{"_ZN9SleepGoal6canUseEv", sleep_goal_layout, 2},
    MobAiHook{"_ZN9SleepGoal11canContinueEv", sleep_goal_layout, 3},
    MobAiHook{"_ZN27NearestAttackableTargetGoal11_findTargetEPPK13MobDescriptor", nearest_attackable_target_goal_layout,
              13},
    MobAiHook{"_ZN16RandomStrollGoal6canUseEv", random_stroll_goal_layout, 2},
    MobAiHook{"_ZN16RandomStrollGoal11canContinueEv", random_stroll_goal_layout, 3},
    MobAiHook{"_ZN20RandomLookAroundGoal6canUseEv", random_look_around_goal_layout, 2},
    MobAiHook{"_ZN20RandomLookAroundGoal11canContinueEv", random_look_around_goal_layout, 3},
};

bool mob_ai_preflight(std::string &reason);
bool is_mob_ai_hook(std::string_view name);

}  // namespace endstone::runtime

#endif
