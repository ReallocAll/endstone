#pragma once

#ifdef __linux__

#include "bedrock/bedrock.h"

class SleepGoal {
public:
    ENDSTONE_HOOK_LINUX bool canUse();
    ENDSTONE_HOOK_LINUX bool canContinue();
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
