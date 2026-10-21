#include <limits>

#include <gtest/gtest.h>

#include "endstone/core/perf/entity_ai_state.h"

namespace endstone::core {
struct EntityAiStateTestAccess {
    static std::size_t size(const EntityAiState &state) { return state.entries_.size(); }
};
}  // namespace endstone::core

using endstone::core::EntityAiState;
using endstone::core::EntityAiStateTestAccess;

TEST(EntityAiStateTest, SearchCadenceUsesActualSearchTicks)
{
    EntityAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 7, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 8, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 26, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 30, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 49, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 50, 20));
}

TEST(EntityAiStateTest, ActivityProtectsHalfOpenWindowAndClearsSearchDelay)
{
    EntityAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 1, 200));
    state.recordActivity(12, 10);
    EXPECT_TRUE(state.recentlyActive(12, 10));
    EXPECT_TRUE(state.recentlyActive(12, 109));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 109, 200));
    EXPECT_FALSE(state.recentlyActive(12, 110));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 110, 200));
    EXPECT_FALSE(state.allowNewTargetSearch(12, 111, 200));
}

TEST(EntityAiStateTest, DisabledPolicyAndExistingTargetClearSearchDelay)
{
    EntityAiState state;
    EXPECT_TRUE(state.allowNewTargetSearch(12, 3, 20));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 4, std::nullopt));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 5, 20));
    state.clearSearch(12);
    EXPECT_TRUE(state.allowNewTargetSearch(12, 6, 20));
}

TEST(EntityAiStateTest, ChangedSnapshotClearsSearchWithoutLosingProtection)
{
    EntityAiState state;
    state.recordActivity(12, 10);
    EXPECT_TRUE(state.allowNewTargetSearch(13, 10, 200));
    state.clearSearchDelays();
    EXPECT_TRUE(state.recentlyActive(12, 109));
    EXPECT_FALSE(state.recentlyActive(12, 110));
    EXPECT_TRUE(state.allowNewTargetSearch(13, 11, 200));
}

TEST(EntityAiStateTest, PruningWaitsForBothDeadlinesAndTwentyServerTicks)
{
    EntityAiState state;
    state.recordActivity(12, 0);
    EXPECT_TRUE(state.allowNewTargetSearch(13, 0, 30));
    state.prune(20);
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 2);
    state.prune(39);
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 2);
    state.prune(40);
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 1);
    state.prune(100);
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 0);
}

TEST(EntityAiStateTest, RemovalAndLevelResetDiscardTransientEntries)
{
    EntityAiState state;
    state.recordActivity(12, 1);
    state.erase(12);
    EXPECT_FALSE(state.recentlyActive(12, 2));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 2, 20));
    state.clear();
    EXPECT_TRUE(state.allowNewTargetSearch(12, 3, 20));
}

TEST(EntityAiStateTest, DeadlinesSaturateInsteadOfWrapping)
{
    EntityAiState state;
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    state.recordActivity(12, max - 5);
    EXPECT_TRUE(state.recentlyActive(12, max - 1));
    EXPECT_TRUE(state.allowNewTargetSearch(13, max - 5, 20));
    EXPECT_FALSE(state.allowNewTargetSearch(13, max - 1, 20));
    EXPECT_FALSE(state.recentlyActive(12, max));
    EXPECT_TRUE(state.allowNewTargetSearch(13, max, 20));
}

TEST(EntityAiStateTest, InvalidIdsAreNeverRetained)
{
    EntityAiState state;
    state.recordActivity(-1, 1);
    EXPECT_FALSE(state.recentlyActive(-1, 2));
    EXPECT_TRUE(state.allowNewTargetSearch(-1, 2, 20));
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 0);
}

TEST(EntityAiStateTest, OriginalFallbackAndUnknownIdsDoNotAllocateState)
{
    EntityAiState state;
    state.clearSearch(12);
    EXPECT_FALSE(state.recentlyActive(12, 10));
    EXPECT_TRUE(state.allowNewTargetSearch(12, 10, std::nullopt));
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 0);
    EXPECT_TRUE(state.allowNewTargetSearch(12, 10, 20));
    state.clearSearch(12);
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 1);
    state.prune(11);
    EXPECT_EQ(EntityAiStateTestAccess::size(state), 0);
}
