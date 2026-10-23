#pragma once

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

#ifdef _WIN32
inline constexpr MobAiGoalLayout sleep_goal_layout{0xA994440, 0xC0};
inline constexpr MobAiGoalLayout nearest_attackable_target_goal_layout{0xA961300, 0x30};
inline constexpr MobAiGoalLayout random_stroll_goal_layout{0xA960EC0, 0x30};
inline constexpr MobAiGoalLayout random_look_around_goal_layout{0xA98F7F0, 0x48};
#else
inline constexpr MobAiGoalLayout sleep_goal_layout{0xEEFC570, 0xB8};
inline constexpr MobAiGoalLayout nearest_attackable_target_goal_layout{0xF242670, 0x28};
inline constexpr MobAiGoalLayout random_stroll_goal_layout{0xF242178, 0x28};
inline constexpr MobAiGoalLayout random_look_around_goal_layout{0xEEE7378, 0x40};
#endif

struct MobAiHook {
    consteval MobAiHook(std::string_view name, const MobAiGoalLayout &layout, std::size_t slot)
        : name(name), function(get_symbol(name)), layout(&layout), slot(slot)
    {
    }

    consteval explicit MobAiHook(std::string_view name)
        : name(name), function(get_symbol(name)), layout(nullptr), slot(0)
    {
    }

    std::string_view name;
    std::size_t function;
    const MobAiGoalLayout *layout;
    std::size_t slot;
};

inline constexpr std::array mob_ai_hooks = {
#ifdef _WIN32
    MobAiHook{"?canUse@SleepGoal@@QEAA_NXZ", sleep_goal_layout, 1},
    MobAiHook{"?stop@SleepGoal@@QEAAXXZ", sleep_goal_layout, 5},
    MobAiHook{"?_findTarget@NearestAttackableTargetGoal@@QEAA?AUActorUniqueID@@PEAPEBUMobDescriptor@@@Z",
              nearest_attackable_target_goal_layout, 12},
    MobAiHook{"?canUse@RandomStrollGoal@@QEAA_NXZ", random_stroll_goal_layout, 1},
    MobAiHook{"?canContinue@RandomStrollGoal@@QEAA_NXZ", random_stroll_goal_layout, 2},
    MobAiHook{"?canUse@RandomLookAroundGoal@@QEAA_NXZ", random_look_around_goal_layout, 1},
    MobAiHook{"?canContinue@RandomLookAroundGoal@@QEAA_NXZ", random_look_around_goal_layout, 2},
    MobAiHook{"?findPath@Level@@UEAA?AV?$unique_ptr@VPath@@U?$default_delete@VPath@@@std@@@std@@AEAVActor@@"
              "HHHAEAVNavigationComponent@@@Z"},
#else
    MobAiHook{"_ZN9SleepGoal6canUseEv", sleep_goal_layout, 2},
    MobAiHook{"_ZN9SleepGoal4stopEv", sleep_goal_layout, 6},
    MobAiHook{"_ZN27NearestAttackableTargetGoal11_findTargetEPPK13MobDescriptor", nearest_attackable_target_goal_layout,
              13},
    MobAiHook{"_ZN16RandomStrollGoal6canUseEv", random_stroll_goal_layout, 2},
    MobAiHook{"_ZN16RandomStrollGoal11canContinueEv", random_stroll_goal_layout, 3},
    MobAiHook{"_ZN20RandomLookAroundGoal6canUseEv", random_look_around_goal_layout, 2},
    MobAiHook{"_ZN20RandomLookAroundGoal11canContinueEv", random_look_around_goal_layout, 3},
    MobAiHook{"_ZN10PathFinder8findPathER5Actoriiif"},
#endif
};

bool mob_ai_preflight(std::string &reason);
bool is_mob_ai_hook(std::string_view name);

}  // namespace endstone::runtime
