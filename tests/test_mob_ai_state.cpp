#include <limits>

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

TEST(MobAiStateTest, DisabledIntervalsAndExplicitResetsClearSearchDelay)
{
    MobAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 3, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 4, std::nullopt));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 5, 20));
    state.clearSearch(12);
    EXPECT_TRUE(state.allowNewTargetSearch(12, 6, 20));
}

TEST(MobAiStateTest, RemovalDiscardsActivityAndSearchButPreservesSleepFailure)
{
    MobAiState state;
    state.recordActivity(12, 1);
    EXPECT_TRUE(state.recentlyActive(12, 1));
    EXPECT_TRUE(state.allowNewTargetSearch(13, 1, 200));
    EXPECT_FALSE(state.allowNewTargetSearch(13, 2, 200));
    state.recordSleepFailure(12, 13000);
    state.erase(12);
    state.erase(13);
    EXPECT_FALSE(state.recentlyActive(12, 1));
    EXPECT_TRUE(state.allowNewTargetSearch(13, 2, 200));
    EXPECT_FALSE(state.canAttemptSleep(12, 14000));
}

TEST(MobAiStateTest, LevelResetDiscardsAllState)
{
    MobAiState state;
    state.recordActivity(12, 1);
    EXPECT_TRUE(state.allowNewTargetSearch(13, 1, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(13, 2, 20));
    state.recordSleepFailure(12, 13000);
    state.clear();
    EXPECT_FALSE(state.recentlyActive(12, 2));
    EXPECT_TRUE(state.canAttemptSleep(12, 13000));
    EXPECT_TRUE(state.allowNewTargetSearch(13, 2, 20));
}

TEST(MobAiStateTest, SleepFailureLocksThroughDawnUntilNextEvening)
{
    MobAiState state;
    EXPECT_TRUE(state.canAttemptSleep(12, 13000));
    state.recordSleepFailure(12, 13000);
    for (const auto world_time : {13000, 23000, 25000, 35999}) {
        EXPECT_FALSE(state.canAttemptSleep(12, world_time));
        EXPECT_TRUE(state.canAttemptSleep(13, world_time));
    }
    EXPECT_TRUE(state.canAttemptSleep(12, 36000));
}

TEST(MobAiStateTest, BackwardTimeKeepsOnlyTheObservedPeriod)
{
    MobAiState state;
    state.recordSleepFailure(12, 27000);
    EXPECT_FALSE(state.canAttemptSleep(12, 13000));
    EXPECT_FALSE(state.canAttemptSleep(12, 12000));
    EXPECT_TRUE(state.canAttemptSleep(12, 11999));
    state.recordSleepFailure(13, 11999);
    EXPECT_TRUE(state.canAttemptSleep(12, 13000));
    EXPECT_TRUE(state.canAttemptSleep(13, 11999));
}

TEST(MobAiStateTest, RecordingFailureInNewPeriodDiscardsPreviousFailures)
{
    MobAiState state;
    state.recordSleepFailure(12, 13000);
    state.recordSleepFailure(13, 36000);
    EXPECT_TRUE(state.canAttemptSleep(12, 36001));
    EXPECT_FALSE(state.canAttemptSleep(13, 36001));
}

TEST(MobAiStateTest, SleepFailureIgnoresInvalidIds)
{
    MobAiState state;
    state.recordSleepFailure(-1, 13000);
    EXPECT_TRUE(state.canAttemptSleep(-1, 13000));
    EXPECT_TRUE(state.canAttemptSleep(12, 13000));
    state.recordSleepFailure(12, 13000);
    EXPECT_TRUE(state.canAttemptSleep(-1, 14000));
    EXPECT_FALSE(state.canAttemptSleep(12, 14000));
}

TEST(MobAiStateTest, SleepPeriodsUseFloorDivisionForNegativeTime)
{
    MobAiState state;
    state.recordSleepFailure(12, -12001);
    EXPECT_FALSE(state.canAttemptSleep(12, -12001));
    EXPECT_TRUE(state.canAttemptSleep(12, -12000));
    state.recordSleepFailure(12, -12000);
    EXPECT_FALSE(state.canAttemptSleep(12, 0));
    EXPECT_FALSE(state.canAttemptSleep(12, 11999));
    EXPECT_TRUE(state.canAttemptSleep(12, 12000));
}

TEST(MobAiStateTest, SleepPeriodsHandleExtremeWorldTimes)
{
    MobAiState state;
    state.recordSleepFailure(12, std::numeric_limits<int>::min());
    EXPECT_FALSE(state.canAttemptSleep(12, std::numeric_limits<int>::min() + 1));
    EXPECT_TRUE(state.canAttemptSleep(12, std::numeric_limits<int>::max()));
}

TEST(MobAiStateTest, SleepFailurePreservesAndSurvivesActivityAndSearch)
{
    MobAiState state;
    state.recordActivity(12, 1);
    EXPECT_TRUE(state.allowNewTargetSearch(12, 101, 200));
    state.recordSleepFailure(12, 13000);
    EXPECT_TRUE(state.recentlyActive(12, 100));
    EXPECT_FALSE(state.recentlyActive(12, 101));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 111, 200));
    state.recordActivity(12, 200);
    EXPECT_TRUE(state.recentlyActive(12, 299));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 300, 200));
    state.clearSearch(12);
    EXPECT_FALSE(state.canAttemptSleep(12, 14000));
}

TEST(MobAiStateTest, PruningRetainsUnexpiredActivityAndSearchWithoutClearingSleepFailures)
{
    MobAiState state;
    state.recordActivity(1, 0);
    EXPECT_TRUE(state.allowNewTargetSearch(2, 0, 120));
    state.recordSleepFailure(3, 13000);
    state.prune(100);
    EXPECT_FALSE(state.allowNewTargetSearch(2, 100, 120));
    EXPECT_FALSE(state.canAttemptSleep(3, 14000));
    state.prune(120);
    EXPECT_TRUE(state.allowNewTargetSearch(2, 120, 120));
    EXPECT_FALSE(state.canAttemptSleep(3, 25000));
}
