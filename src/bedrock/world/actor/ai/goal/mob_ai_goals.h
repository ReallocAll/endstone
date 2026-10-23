#pragma once

#include "bedrock/bedrock.h"
#include "bedrock/world/actor/actor_unique_id.h"

struct MobDescriptor;

class SleepGoal {
public:
    ENDSTONE_HOOK bool canUse();
    ENDSTONE_HOOK void stop();
};

class NearestAttackableTargetGoal {
public:
    ENDSTONE_HOOK ActorUniqueID _findTarget(const MobDescriptor **matched_definition);
};

class RandomStrollGoal {
public:
    ENDSTONE_HOOK bool canUse();
    ENDSTONE_HOOK bool canContinue();
};

class RandomLookAroundGoal {
public:
    ENDSTONE_HOOK bool canUse();
    ENDSTONE_HOOK bool canContinue();
};
