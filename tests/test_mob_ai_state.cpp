#include <gtest/gtest.h>

#include "endstone/core/perf/mob_ai_state.h"

using endstone::core::MobAiState;

TEST(MobAiStateTest, SearchCadenceUsesActualSearchTicks)
{
    MobAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 7, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 8, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 26, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 30, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 49, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 50, 20));
}

TEST(MobAiStateTest, ActivityProtectsHalfOpenWindowAndClearsSearchDelay)
{
    MobAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 1, 200));
    state.recordActivity(12, 10);
    EXPECT_TRUE(state.recentlyActive(12, 10));
    EXPECT_TRUE(state.recentlyActive(12, 109));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 109, 200));
    EXPECT_FALSE(state.recentlyActive(12, 110));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 110, 200));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 111, 200));
}

TEST(MobAiStateTest, SearchResetsPreserveActivity)
{
    MobAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 3, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 4, std::nullopt));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 5, 20));
    state.clearSearch(12);
    EXPECT_TRUE(state.allowNewTargetSearch(12, 6, 20));
    state.recordActivity(12, 10);
    EXPECT_TRUE(state.allowNewTargetSearch(13, 10, 200));
    state.clearSearchDelays();
    EXPECT_TRUE(state.recentlyActive(12, 109));
    EXPECT_TRUE(state.allowNewTargetSearch(13, 11, 200));
}

TEST(MobAiStateTest, RemovalAndLevelResetDiscardTransientEntries)
{
    MobAiState state;
    state.recordActivity(12, 1);
    state.erase(12);
    EXPECT_FALSE(state.recentlyActive(12, 2));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 2, 20));
    state.clear();
    EXPECT_TRUE(state.allowNewTargetSearch(12, 3, 20));
}
