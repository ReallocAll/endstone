#pragma once

#ifdef __linux__

#include "bedrock/bedrock.h"
#include "bedrock/world/actor/actor_unique_id.h"

struct MobDescriptor;

class SleepGoal {
public:
    ENDSTONE_HOOK_LINUX bool canUse();
    ENDSTONE_HOOK_LINUX bool canContinue();
};

class NearestAttackableTargetGoal {
public:
    ENDSTONE_HOOK_LINUX ActorUniqueID _findTarget(const MobDescriptor **matched_definition);
};

class RandomStrollGoal {
public:
    ENDSTONE_HOOK_LINUX bool canUse();
    ENDSTONE_HOOK_LINUX bool canContinue();
};

class RandomLookAroundGoal {
public:
    ENDSTONE_HOOK_LINUX bool canUse();
    ENDSTONE_HOOK_LINUX bool canContinue();
};

#endif
