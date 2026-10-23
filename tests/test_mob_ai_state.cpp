#include <limits>

#include <gtest/gtest.h>

#include "endstone/core/perf/mob_ai_state.h"

using endstone::core::MobAiState;

namespace endstone::core {

struct MobAiStateTestAccess {
    static std::size_t size(const MobAiState &state) { return state.entries_.size(); }
    static std::size_t sleepFailures(const MobAiState &state) { return state.sleep_failures_.size(); }
    static bool hasSleepPeriod(const MobAiState &state) { return state.sleep_period_.has_value(); }
};

}  // namespace endstone::core

using endstone::core::MobAiStateTestAccess;

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
    EXPECT_TRUE(state.allowNewTargetSearch(12, 101, 200));
    state.recordSleepFailure(12, 13000);
    state.erase(12);
    EXPECT_EQ(MobAiStateTestAccess::size(state), 0);
    EXPECT_FALSE(state.recentlyActive(12, 102));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 102, 20));
    EXPECT_FALSE(state.canAttemptSleep(12, 14000));
}

TEST(MobAiStateTest, LevelResetDiscardsAllState)
{
    MobAiState state;
    state.recordActivity(12, 1);
    state.recordSleepFailure(12, 13000);
    state.clear();
    EXPECT_EQ(MobAiStateTestAccess::size(state), 0);
    EXPECT_EQ(MobAiStateTestAccess::sleepFailures(state), 0);
    EXPECT_FALSE(MobAiStateTestAccess::hasSleepPeriod(state));
    EXPECT_FALSE(state.recentlyActive(12, 2));
    EXPECT_TRUE(state.canAttemptSleep(12, 13000));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 2, 20));
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
    EXPECT_EQ(MobAiStateTestAccess::sleepFailures(state), 0);
}

TEST(MobAiStateTest, NitwitSleepAcrossMidnightStaysInOnePeriod)
{
    MobAiState state;
    state.recordSleepFailure(12, 14000);
    EXPECT_FALSE(state.canAttemptSleep(12, 26000));
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
    EXPECT_EQ(MobAiStateTestAccess::sleepFailures(state), 1);
}

TEST(MobAiStateTest, SleepFailureIgnoresInvalidIds)
{
    MobAiState state;
    state.recordSleepFailure(-1, 13000);
    EXPECT_TRUE(state.canAttemptSleep(-1, 13000));
    EXPECT_TRUE(state.canAttemptSleep(12, 13000));
    EXPECT_EQ(MobAiStateTestAccess::sleepFailures(state), 0);
    EXPECT_EQ(MobAiStateTestAccess::size(state), 0);
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
    const auto min = std::numeric_limits<int>::min();
    const auto max = std::numeric_limits<int>::max();
    state.recordSleepFailure(12, min);
    EXPECT_FALSE(state.canAttemptSleep(12, min + 1));
    EXPECT_TRUE(state.canAttemptSleep(12, -2147460000));
    state.recordSleepFailure(12, max);
    EXPECT_FALSE(state.canAttemptSleep(12, 2147460000));
    EXPECT_TRUE(state.canAttemptSleep(12, 2147459999));
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
    EXPECT_EQ(MobAiStateTestAccess::size(state), 1);
    EXPECT_FALSE(state.canAttemptSleep(3, 14000));
    state.prune(120);
    EXPECT_EQ(MobAiStateTestAccess::size(state), 0);
    state.prune(std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(state.canAttemptSleep(3, 25000));
    EXPECT_EQ(MobAiStateTestAccess::sleepFailures(state), 1);
}
