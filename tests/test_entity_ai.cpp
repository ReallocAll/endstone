#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "endstone/core/perf/entity_ai.h"
#include "endstone/core/perf/entity_ai_capabilities.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace endstone::core {

struct EntityAiPoliciesTestAccess {
    static auto &fileOperations(EntityAiPolicies &policies) { return policies.file_operations_; }

    static EntityAiDecision parseAndResolve(const std::optional<std::string> &policies, std::string_view type)
    {
        EntityAiPolicies subject;
        subject.snapshot_ = EntityAiPolicies::parse(policies);
        return subject.resolve(type);
    }
};

namespace {

TEST(EntityAiCapabilitiesTest, IdleRequiresMinecraftMobAndExcludesExactlyProtectedTypes)
{
    for (const std::string_view type : {"minecraft:cow", "minecraft:pig", "minecraft:sheep", "minecraft:chicken",
                                        "minecraft:spider", "minecraft:cave_spider", "minecraft:unknown_mob"}) {
        SCOPED_TRACE(type);
        EXPECT_TRUE(supportsEntityAiIdle(type, ActorType::Mob));
        EXPECT_FALSE(supportsEntityAiIdle(type, ActorType::Undefined));
        EXPECT_FALSE(supportsEntityAiIdle(type, ActorType::Player));
    }
    for (const std::string_view type : {"minecraft:player",
                                        "minecraft:armor_stand",
                                        "minecraft:tripod_camera",
                                        "minecraft:npc",
                                        "minecraft:agent",
                                        "minecraft:villager",
                                        "minecraft:villager_v2",
                                        "minecraft:wandering_trader",
                                        "minecraft:trader_llama",
                                        "minecraft:ender_dragon",
                                        "minecraft:wither",
                                        "minecraft:warden",
                                        "minecraft:creaking",
                                        "minecraft:allay",
                                        "minecraft:copper_golem",
                                        "minecraft:iron_golem",
                                        "minecraft:snow_golem",
                                        "minecraft:guardian",
                                        "minecraft:elder_guardian",
                                        "minecraft:elder_guardian_ghost",
                                        "minecraft:shulker",
                                        "custom:cow",
                                        "cow"}) {
        SCOPED_TRACE(type);
        EXPECT_FALSE(supportsEntityAiIdle(type, ActorType::Mob));
    }
}

TEST(EntityAiCapabilitiesTest, SearchRequiresOwnRangeMonsterAndExactAllowlist)
{
    EntityAiDecision decision;
    decision.animal = EntityAiAnimalPolicy{32, true, true};
    decision.target_search_interval_ticks = 20;
    decision.target_search_player_range_blocks = 32;
    for (const std::string_view type :
         {"minecraft:zombie", "minecraft:husk", "minecraft:drowned", "minecraft:zombie_villager",
          "minecraft:zombie_villager_v2", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton",
          "minecraft:bogged", "minecraft:parched", "minecraft:creeper", "minecraft:witch"}) {
        SCOPED_TRACE(type);
        EXPECT_TRUE(supportsEntityAiSearch(type, ActorType::Monster, decision));
        EXPECT_FALSE(supportsEntityAiSearch(type, ActorType::Mob, decision));
    }
    for (const std::string_view type :
         {"minecraft:spider", "minecraft:cave_spider", "minecraft:enderman", "minecraft:zombie_pigman",
          "minecraft:piglin", "minecraft:piglin_brute", "minecraft:wolf", "minecraft:guardian",
          "minecraft:unknown_monster", "custom:zombie"}) {
        SCOPED_TRACE(type);
        EXPECT_FALSE(supportsEntityAiSearch(type, ActorType::Monster, decision));
    }
    decision.animal.reset();
    EXPECT_TRUE(supportsEntityAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    EXPECT_TRUE(supportsEntityAiSearch("minecraft:skeleton", ActorType::Skeleton, decision));
    decision.target_search_player_range_blocks.reset();
    EXPECT_FALSE(supportsEntityAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    decision.animal = EntityAiAnimalPolicy{32, true, true};
    EXPECT_FALSE(supportsEntityAiSearch("minecraft:skeleton", ActorType::Skeleton, decision));
    for (const auto range : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        decision.target_search_player_range_blocks = range;
        EXPECT_FALSE(supportsEntityAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    }
    decision.target_search_player_range_blocks = 32;
    decision.target_search_interval_ticks.reset();
    EXPECT_FALSE(supportsEntityAiSearch("minecraft:zombie", ActorType::Zombie, decision));
}

TEST(EntityAiCapabilitiesTest, ActivityRequiresResolvedSupportedIdleOrSearchCapability)
{
    EntityAiDecision decision;
    EXPECT_FALSE(hasEntityAiActivityCapability("minecraft:cow", ActorType::Cow, decision));
    decision.prevent_sleep = true;
    EXPECT_FALSE(hasEntityAiActivityCapability("minecraft:villager_v2", ActorType::Villager, decision));
    decision.prevent_sleep = false;
    decision.animal = EntityAiAnimalPolicy{16, true, false};
    EXPECT_TRUE(hasEntityAiActivityCapability("minecraft:pig", ActorType::Pig, decision));
    EXPECT_FALSE(hasEntityAiActivityCapability("minecraft:allay", ActorType::Mob, decision));
    EXPECT_FALSE(hasEntityAiActivityCapability("custom:pig", ActorType::Pig, decision));
    decision.animal = EntityAiAnimalPolicy{16, false, false};
    EXPECT_FALSE(hasEntityAiActivityCapability("minecraft:pig", ActorType::Pig, decision));
    decision.animal.reset();
    decision.target_search_interval_ticks = 40;
    decision.target_search_player_range_blocks = 32;
    EXPECT_TRUE(hasEntityAiActivityCapability("minecraft:zombie", ActorType::Zombie, decision));
    EXPECT_FALSE(hasEntityAiActivityCapability("minecraft:spider", ActorType::Spider, decision));
    decision.animal = EntityAiAnimalPolicy{32, false, true};
    EXPECT_TRUE(hasEntityAiActivityCapability("minecraft:skeleton", ActorType::Skeleton, decision));
}

TEST(EntityAiCapabilitiesTest, FeetDistanceIsThreeDimensionalInclusiveAndRejectsNonfiniteInput)
{
    EXPECT_EQ(entityAiWithinRange({0, 0, 0}, {3, 4, 0}, 5), true);
    EXPECT_EQ(entityAiWithinRange({0, 0, 0}, {3, 4, 0}, 4.99), false);
    EXPECT_EQ(entityAiWithinRange({0, 0, 0}, {0, 0, 6}, 5), false);
    EXPECT_EQ(entityAiWithinRange({0, 0, 0}, {0, 5, 0}, 5), true);
    EXPECT_EQ(entityAiWithinRange({-3e38, 0, 0}, {3e38, 0, 0}, 7e38), true);
    EXPECT_EQ(entityAiWithinRange({-3e38, 0, 0}, {3e38, 0, 0}, 5e38), false);
    EXPECT_FALSE(entityAiWithinRange({0, 0, 0}, {0, 0, 0}, 0).has_value());
    EXPECT_FALSE(entityAiWithinRange({0, 0, 0}, {0, 0, 0}, -1).has_value());
    for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        EXPECT_FALSE(entityAiWithinRange({invalid, 0, 0}, {0, 0, 0}, 32).has_value());
        EXPECT_FALSE(entityAiWithinRange({0, invalid, 0}, {0, 0, 0}, 32).has_value());
        EXPECT_FALSE(entityAiWithinRange({0, 0, 0}, {0, 0, invalid}, 32).has_value());
        EXPECT_FALSE(entityAiWithinRange({0, 0, 0}, {0, 0, 0}, invalid).has_value());
    }
}

class EntityAiTest : public testing::Test {
protected:
    void SetUp() override
    {
        directory =
            std::filesystem::temp_directory_path() / boost::uuids::to_string(boost::uuids::random_generator()());
        std::filesystem::create_directory(directory);
        policies = directory / "endstone.toml";
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    void write(const std::filesystem::path &path, const std::string &content)
    {
        std::ofstream output(path, std::ios::binary);
        output << content;
        ASSERT_TRUE(output.good());
    }

    std::string read(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    const std::string sleep = R"([mob-ai]
enabled = true
[mob-ai.villagers]
prevent-sleep = true
)";
    std::filesystem::path directory;
    std::filesystem::path policies;
    EntityAiPolicies subject;
};

TEST_F(EntityAiTest, StartsInactive)
{
    const auto decision = subject.resolve("minecraft:villager_v2");
    EXPECT_FALSE(decision.prevent_sleep);
    EXPECT_FALSE(decision.animal);
    EXPECT_FALSE(decision.target_search_interval_ticks);
    EXPECT_FALSE(decision.target_search_player_range_blocks);
    EXPECT_EQ(decision.revision, 0);
}

TEST_F(EntityAiTest, MaximumIntegerPlayerRangesDoNotDereferenceEmptyConversion)
{
    const auto decision = EntityAiPoliciesTestAccess::parseAndResolve(R"([mob-ai]
enabled = true
[mob-ai.idle]
player-range-blocks = 9223372036854775807
[mob-ai.target-search]
player-range-blocks = 9223372036854775807
)", "minecraft:sheep");
    ASSERT_TRUE(decision.animal);
    EXPECT_DOUBLE_EQ(decision.animal->activation_range_blocks, 9223372036854775808.0);
    ASSERT_TRUE(decision.target_search_player_range_blocks);
    EXPECT_DOUBLE_EQ(*decision.target_search_player_range_blocks, 9223372036854775808.0);
}

TEST_F(EntityAiTest, DuplicateTomlFieldsAreRejectedBeforeOverwriting)
{
    for (const auto &config : std::vector<std::string>{
             "[mob-ai]\nenabled=true\nenabled=false\n",
             "[mob-ai.idle]\nenabled=true\nenabled=false\n",
             "[mob-ai.target-search]\ninterval-ticks=20\ninterval-ticks=40\n",
             "[mob-ai.villagers]\nprevent-sleep=true\nprevent-sleep=false\n",
             "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\ntypes=['minecraft:pig']\n",
             "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n"
             "[mob-ai.overrides.idle]\nplayer-range-blocks=16\nplayer-range-blocks=32\n"}) {
        SCOPED_TRACE(config);
        EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:cow"), std::exception);
    }
}

TEST_F(EntityAiTest, MainConfigurationIgnoresUnrelatedAndLegacySectionsAndMissingAiStaysInactive)
{
    for (const auto &config : std::vector<std::optional<std::string>>{
             std::nullopt, "[server]\nname='example'\n[policies.old]\nunused=true\n",
             "entity-ai=false\n", "[entity-ai]\nenabled=true\nconfig-version=99\n"}) {
        const auto missing = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:sheep");
        EXPECT_FALSE(missing.animal);
        EXPECT_FALSE(missing.target_search_interval_ticks);
        EXPECT_FALSE(missing.prevent_sleep);
    }
    const auto decision = EntityAiPoliciesTestAccess::parseAndResolve(
        "[server]\nname='example'\n[entity-ai]\nenabled=true\n" + sleep, "minecraft:villager_v2");
    EXPECT_TRUE(decision.prevent_sleep);
}

TEST_F(EntityAiTest, MasterEnabledDefaultsFalseAndDisablesAllFeatures)
{
    for (const std::string enabled : {"", "enabled=false\n"}) {
        const auto config = "[mob-ai]\n" + enabled +
                            "[mob-ai.villagers]\nprevent-sleep=true\n"
                            "[[mob-ai.overrides]]\ntypes=['minecraft:villager_v2']\n"
                            "[mob-ai.overrides.idle]\nenabled=true\n";
        const auto decision = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:villager_v2");
        EXPECT_FALSE(decision.prevent_sleep);
        EXPECT_FALSE(decision.animal);
        EXPECT_FALSE(decision.target_search_interval_ticks);
        EXPECT_FALSE(decision.target_search_player_range_blocks);
    }
}

TEST_F(EntityAiTest, GlobalDefaultsAndExactOverridesInheritOtherFields)
{
    const std::string config = R"([mob-ai]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:skeleton"]
[mob-ai.overrides.idle]
player-range-blocks = 16
disable-random-stroll = false
[mob-ai.overrides.target-search]
player-range-blocks = 8
interval-ticks = 40
)";
    const auto cow = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:cow");
    ASSERT_TRUE(cow.animal);
    EXPECT_EQ(cow.animal->activation_range_blocks, 32);
    EXPECT_TRUE(cow.animal->random_stroll);
    EXPECT_TRUE(cow.animal->random_look_around);
    EXPECT_EQ(cow.target_search_interval_ticks, 20);
    EXPECT_EQ(cow.target_search_player_range_blocks, 32);
    EXPECT_FALSE(cow.prevent_sleep);
    const auto skeleton = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:skeleton");
    ASSERT_TRUE(skeleton.animal);
    EXPECT_EQ(skeleton.animal->activation_range_blocks, 16);
    EXPECT_FALSE(skeleton.animal->random_stroll);
    EXPECT_TRUE(skeleton.animal->random_look_around);
    EXPECT_EQ(skeleton.target_search_interval_ticks, 40);
    EXPECT_EQ(skeleton.target_search_player_range_blocks, 8);
    const auto custom = EntityAiPoliciesTestAccess::parseAndResolve(config, "custom:cow");
    EXPECT_FALSE(hasEntityAiActivityCapability("custom:cow", ActorType::Cow, custom));
}

TEST_F(EntityAiTest, ExactSpiderOverrideIsValidWithoutGrantingNativeSearch)
{
    const auto decision = EntityAiPoliciesTestAccess::parseAndResolve(R"([mob-ai]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:spider"]
[mob-ai.overrides.target-search]
interval-ticks = 40
)", "minecraft:spider");
    EXPECT_TRUE(decision.animal);
    EXPECT_EQ(decision.target_search_interval_ticks, 40);
    EXPECT_EQ(decision.target_search_player_range_blocks, 32);
    EXPECT_TRUE(supportsEntityAiIdle("minecraft:spider", ActorType::Spider));
    EXPECT_FALSE(supportsEntityAiSearch("minecraft:spider", ActorType::Spider, decision));
}

TEST_F(EntityAiTest, SearchWithoutIdleUsesIndependentPlayerRange)
{
    const auto config = R"([mob-ai]
enabled = true
[mob-ai.idle]
enabled = false
player-range-blocks = 64
[mob-ai.target-search]
player-range-blocks = 12.5
interval-ticks = 40
)";
    for (const auto type : {"minecraft:zombie", "minecraft:skeleton", "minecraft:witch"}) {
        const auto decision = EntityAiPoliciesTestAccess::parseAndResolve(config, type);
        EXPECT_FALSE(decision.animal);
        EXPECT_EQ(decision.target_search_interval_ticks, 40);
        EXPECT_EQ(decision.target_search_player_range_blocks, 12.5);
        EXPECT_TRUE(supportsEntityAiSearch(type, ActorType::Monster, decision));
        EXPECT_TRUE(hasEntityAiActivityCapability(type, ActorType::Monster, decision));
    }
}

TEST_F(EntityAiTest, DisabledCandidatesStillValidateStrictFeatureSchema)
{
    for (const auto &config : std::vector<std::string>{
             "mob-ai=false", "[mob-ai]\nenabled=1", "[mob-ai]\nenabled=false\nunknown=true",
             "[mob-ai]\nenabled=false\nidle=[]", "[mob-ai]\nenabled=false\ntarget-search=1",
             "[mob-ai]\nenabled=false\nvillagers=false", "[mob-ai]\nenabled=false\noverrides={}",
             "[mob-ai.idle]\nenabled=false\nunknown=true",
             "[mob-ai.target-search]\nenabled=false\nunknown=true",
             "[mob-ai.villagers]\nprevent-sleep=false\nunknown=true"}) {
        SCOPED_TRACE(config);
        EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:cow"), std::exception);
    }
}

TEST_F(EntityAiTest, UnsupportedTypeSelectorsAreRejected)
{
    const std::string prefix = "[mob-ai]\nenabled=false\n[[mob-ai.overrides]]\ntypes=";
    for (const std::string types : {"['minecraft:skele*']", "['*']", "['minecraft:*']", "['other:*']",
                                    "['minecraft:c?w']", "['minecraft:[cow]']", "['cow']", "[':cow']",
                                    "['minecraft:']", "['minecraft:cow:extra']", "['Minecraft:cow']",
                                    "['minecraft:Cow']", "['minecraft:cow cow']", "['minecraft:cow\\\\cow']",
                                    "['minecraft:cow<event>']", "['minecraft:牛']"}) {
        SCOPED_TRACE(types);
        EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(prefix + types, "minecraft:cow"), std::exception);
    }
}

TEST_F(EntityAiTest, ExactOverridesDoNotAffectOtherTypes)
{
    const auto config = R"([mob-ai]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:sheep"]
[mob-ai.overrides.idle]
enabled = false
[mob-ai.overrides.target-search]
interval-ticks = 4294967295
)";
    const auto sheep = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:sheep");
    EXPECT_FALSE(sheep.animal);
    EXPECT_EQ(sheep.target_search_interval_ticks, UINT32_MAX);
    const auto chicken = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:chicken");
    ASSERT_TRUE(chicken.animal);
    EXPECT_EQ(chicken.animal->activation_range_blocks, 32);
    EXPECT_TRUE(chicken.animal->random_stroll);
    EXPECT_TRUE(chicken.animal->random_look_around);
    EXPECT_EQ(chicken.target_search_interval_ticks, 20);
}

TEST_F(EntityAiTest, VillagerSleepControlResolvesOnlyForVillagerV2)
{
    for (const auto type : {"minecraft:villager", "minecraft:wandering_trader", "minecraft:cow",
                            "custom:villager_v2"}) {
        EXPECT_FALSE(EntityAiPoliciesTestAccess::parseAndResolve(sleep, type).prevent_sleep);
    }
    EXPECT_TRUE(EntityAiPoliciesTestAccess::parseAndResolve(sleep, "minecraft:villager_v2").prevent_sleep);
    const auto overridden = sleep + R"([[mob-ai.overrides]]
types = ["minecraft:villager_v2"]
[mob-ai.overrides.villagers]
prevent-sleep = false
)";
    EXPECT_FALSE(EntityAiPoliciesTestAccess::parseAndResolve(overridden, "minecraft:villager_v2").prevent_sleep);
}

TEST_F(EntityAiTest, SingleIdleBehaviorAndSearchIntervalBoundsAreAccepted)
{
    const auto config = R"([mob-ai]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:sheep"]
[mob-ai.overrides.idle]
player-range-blocks = 0.5
disable-random-look-around = false
[[mob-ai.overrides]]
types = ["minecraft:chicken"]
[mob-ai.overrides.idle]
player-range-blocks = 1
disable-random-stroll = false
[[mob-ai.overrides]]
types = ["minecraft:zombie"]
[mob-ai.overrides.target-search]
interval-ticks = 1
[[mob-ai.overrides]]
types = ["minecraft:skeleton"]
[mob-ai.overrides.target-search]
interval-ticks = 4294967295
)";
    const auto sheep = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:sheep").animal;
    ASSERT_TRUE(sheep);
    EXPECT_EQ(sheep->activation_range_blocks, 0.5);
    EXPECT_TRUE(sheep->random_stroll);
    EXPECT_FALSE(sheep->random_look_around);
    const auto chicken = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:chicken").animal;
    ASSERT_TRUE(chicken);
    EXPECT_EQ(chicken->activation_range_blocks, 1);
    EXPECT_FALSE(chicken->random_stroll);
    EXPECT_TRUE(chicken->random_look_around);
    EXPECT_EQ(EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:zombie").target_search_interval_ticks, 1);
    EXPECT_EQ(EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:skeleton").target_search_interval_ticks,
              UINT32_MAX);
}

TEST_F(EntityAiTest, DuplicateTypesAndFeatureOverridesAreRejected)
{
    EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(
                     "[[mob-ai.overrides]]\ntypes=['minecraft:cow','minecraft:cow']\n", "minecraft:cow"),
                 std::exception);
    for (const std::string feature : {"idle", "target-search", "villagers"}) {
        const auto rule = "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n[mob-ai.overrides." + feature + "]\n";
        EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(rule + rule, "minecraft:cow"), std::exception);
    }
    const auto disjoint = R"([[mob-ai.overrides]]
types = ["minecraft:skeleton"]
[mob-ai.overrides.idle]
disable-random-stroll = false
[[mob-ai.overrides]]
types = ["minecraft:skeleton"]
[mob-ai.overrides.idle]
disable-random-look-around = false
)";
    EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(disjoint, "minecraft:skeleton"), std::exception);
}

TEST_F(EntityAiTest, MalformedTomlAndInvalidNumericBoundsAreRejected)
{
    for (const std::string config : {"[mob-ai", "[mob-ai]\nenabled =", "[mob-ai]\nenabled=true\n[[mob-ai.overrides]]"}) {
        EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:cow"), std::exception);
    }
    for (const std::string feature : {"idle", "target-search"}) {
        for (const std::string range : {"nan", "inf", "-inf", "0", "-1", "true", "'32'", "[]"}) {
            for (const std::string prefix : {"[mob-ai]\nenabled=false\n[mob-ai." + feature + "]\nenabled=false\n",
                                            "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n[mob-ai.overrides." +
                                                feature + "]\nenabled=false\n"}) {
                SCOPED_TRACE(prefix + range);
                EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(prefix + "player-range-blocks=" + range,
                                                                         "minecraft:cow"), std::exception);
            }
        }
    }
    for (const std::string interval : {"0", "-1", "4294967296", "9223372036854775807", "2.0", "true", "'20'", "nan"}) {
        for (const std::string prefix : {"[mob-ai.target-search]\nenabled=false\n",
                                        "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n"
                                        "[mob-ai.overrides.target-search]\nenabled=false\n"}) {
            SCOPED_TRACE(prefix + interval);
            EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(prefix + "interval-ticks=" + interval,
                                                                     "minecraft:cow"), std::exception);
        }
    }
}

TEST_F(EntityAiTest, OverrideSchemaAndChildTypesAreStrict)
{
    for (const std::string fields : {"", "types=[]", "types=['']", "types='minecraft:cow'", "types=[1]",
                                     "types=['minecraft:cow',false]", "types=['minecraft:cow']\nunknown=true",
                                     "types=['minecraft:cow']\nidle=false", "types=['minecraft:cow']\ntarget-search=[]",
                                     "types=['minecraft:cow']\nvillagers=1"}) {
        SCOPED_TRACE(fields);
        EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve("[[mob-ai.overrides]]\n" + fields,
                                                                 "minecraft:cow"), std::exception);
    }
    EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve("[mob-ai]\noverrides=[1]\n", "minecraft:cow"),
                 std::exception);
    for (const auto &[feature, field] : std::vector<std::pair<std::string, std::string>>{
             {"idle", "enabled"}, {"idle", "disable-random-stroll"}, {"idle", "disable-random-look-around"},
             {"target-search", "enabled"}, {"villagers", "prevent-sleep"}}) {
        for (const std::string value : {"1", "'true'", "[]"}) {
            const auto assignment = field + "=" + value;
            EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve("[mob-ai." + feature + "]\n" + assignment,
                                                                     "minecraft:cow"), std::exception);
            EXPECT_THROW(EntityAiPoliciesTestAccess::parseAndResolve(
                             "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n[mob-ai.overrides." + feature + "]\n" +
                                 assignment, "minecraft:cow"), std::exception);
        }
    }
}

TEST_F(EntityAiTest, FeatureEnabledOverridesInheritGlobalValues)
{
    const auto config = R"([mob-ai]
enabled = true
[mob-ai.idle]
enabled = false
player-range-blocks = 12
disable-random-stroll = false
disable-random-look-around = false
[mob-ai.target-search]
enabled = false
player-range-blocks = 9
interval-ticks = 43
[[mob-ai.overrides]]
types = ["minecraft:skeleton"]
[mob-ai.overrides.idle]
enabled = true
disable-random-look-around = true
[mob-ai.overrides.target-search]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:zombie"]
[mob-ai.overrides.idle]
player-range-blocks = 10
[mob-ai.overrides.target-search]
interval-ticks = 22
)";
    const auto skeleton = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:skeleton");
    ASSERT_TRUE(skeleton.animal);
    EXPECT_EQ(skeleton.animal->activation_range_blocks, 12);
    EXPECT_FALSE(skeleton.animal->random_stroll);
    EXPECT_TRUE(skeleton.animal->random_look_around);
    EXPECT_EQ(skeleton.target_search_interval_ticks, 43);
    EXPECT_EQ(skeleton.target_search_player_range_blocks, 9);
    const auto zombie = EntityAiPoliciesTestAccess::parseAndResolve(config, "minecraft:zombie");
    EXPECT_FALSE(zombie.animal);
    EXPECT_FALSE(zombie.target_search_interval_ticks);
    EXPECT_FALSE(zombie.target_search_player_range_blocks);
}

TEST_F(EntityAiTest, FeaturesCanBeDisabledIndependently)
{
    const auto idle_only = EntityAiPoliciesTestAccess::parseAndResolve(
        "[mob-ai]\nenabled=true\n[mob-ai.target-search]\nenabled=false\n", "minecraft:zombie");
    EXPECT_TRUE(idle_only.animal);
    EXPECT_FALSE(idle_only.target_search_interval_ticks);
    EXPECT_FALSE(idle_only.target_search_player_range_blocks);
    const auto sleep_only = EntityAiPoliciesTestAccess::parseAndResolve(
        sleep + "[mob-ai.idle]\nenabled=false\n[mob-ai.target-search]\nenabled=false\n", "minecraft:villager_v2");
    EXPECT_TRUE(sleep_only.prevent_sleep);
    EXPECT_FALSE(sleep_only.animal);
    EXPECT_FALSE(sleep_only.target_search_interval_ticks);
    EXPECT_FALSE(sleep_only.target_search_player_range_blocks);
}

TEST_F(EntityAiTest, DifferentFeaturesForTheSameTypeCompose)
{
    const auto config = R"([mob-ai]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:villager_v2", "minecraft:skeleton"]
[mob-ai.overrides.idle]
player-range-blocks = 16
[[mob-ai.overrides]]
types = ["minecraft:villager_v2", "minecraft:skeleton"]
[mob-ai.overrides.target-search]
player-range-blocks = 8
interval-ticks = 40
[[mob-ai.overrides]]
types = ["minecraft:villager_v2", "minecraft:skeleton"]
[mob-ai.overrides.villagers]
prevent-sleep = true
)";
    for (const auto type : {"minecraft:villager_v2", "minecraft:skeleton"}) {
        const auto decision = EntityAiPoliciesTestAccess::parseAndResolve(config, type);
        ASSERT_TRUE(decision.animal);
        EXPECT_EQ(decision.animal->activation_range_blocks, 16);
        EXPECT_EQ(decision.target_search_player_range_blocks, 8);
        EXPECT_EQ(decision.target_search_interval_ticks, 40);
        EXPECT_EQ(decision.prevent_sleep, std::string_view(type) == "minecraft:villager_v2");
    }
}

#ifdef _WIN32
TEST_F(EntityAiTest, WindowsCannotActivateOrCreateSidecarFiles)
{
    write(policies, sleep);
    EXPECT_FALSE(subject.initialize(policies));
    EXPECT_FALSE(subject.reload());
    EXPECT_FALSE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    EXPECT_EQ(read(policies), sleep);
    EXPECT_FALSE(std::filesystem::exists(directory / "endstone-world-id"));
    EXPECT_FALSE(std::filesystem::exists(directory / "entity-ai-bindings.json"));
}
#else
TEST_F(EntityAiTest, RemovingLoadedAiSectionRejectsButExplicitDisableCommits)
{
    write(policies, "[server]\nname='example'\n");
    ASSERT_TRUE(subject.initialize(policies));
    EXPECT_EQ(subject.resolve("minecraft:villager_v2").revision, 0);
    write(policies, sleep);
    ASSERT_TRUE(subject.reload());
    const auto before = subject.resolve("minecraft:villager_v2");
    ASSERT_TRUE(before.prevent_sleep);
    write(policies, "[server]\nname='example'\n");
    EXPECT_FALSE(subject.reload());
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    auto disabled = sleep;
    disabled.replace(disabled.find("enabled = true"), 14, "enabled = false");
    write(policies, disabled);
    const auto result = subject.reload();
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->changed);
    EXPECT_EQ(result->revision, before.revision + 1);
    EXPECT_FALSE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    write(policies, sleep);
    ASSERT_TRUE(subject.reload());
    EXPECT_TRUE(subject.resolve("minecraft:villager_v2").prevent_sleep);
}

TEST_F(EntityAiTest, InvalidDisabledReloadRetainsEnabledSnapshotAndOverrides)
{
    const auto config = sleep + "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n"
                                "[mob-ai.overrides.idle]\nplayer-range-blocks=16\n";
    write(policies, config);
    ASSERT_TRUE(subject.initialize(policies));
    const auto before = subject.resolve("minecraft:cow");
    auto disabled = config;
    disabled.replace(disabled.find("enabled = true"), 14, "enabled = false");
    write(policies, disabled + "unknown=true\n");
    EXPECT_FALSE(subject.reload());
    EXPECT_EQ(subject.resolve("minecraft:cow"), before);
    EXPECT_TRUE(subject.resolve("minecraft:villager_v2").prevent_sleep);
}

TEST_F(EntityAiTest, MaximumIntegerPlayerRangeReloadAndFailureRetainDecision)
{
    write(policies, "[mob-ai]\nenabled=true\n[mob-ai.idle]\nplayer-range-blocks=9223372036854775807\n");
    ASSERT_TRUE(subject.initialize(policies));
    const auto before = subject.resolve("minecraft:sheep");
    ASSERT_TRUE(before.animal);
    EXPECT_DOUBLE_EQ(before.animal->activation_range_blocks, 9223372036854775808.0);
    write(policies, "[mob-ai]\nenabled=true\n[mob-ai.idle]\nplayer-range-blocks=nan\n");
    EXPECT_FALSE(subject.reload());
    EXPECT_EQ(subject.resolve("minecraft:sheep"), before);
}

TEST_F(EntityAiTest, ReadFailureAfterValidPrefixDoesNotCommitTruncatedConfig)
{
    write(policies, sleep);
    ASSERT_TRUE(subject.initialize(policies));
    const auto before = subject.resolve("minecraft:villager_v2");
    EntityAiPoliciesTestAccess::fileOperations(subject).read = [](int fd, void *buffer, std::size_t) -> std::ptrdiff_t {
        if (::lseek(fd, 0, SEEK_CUR) == 0) {
            constexpr std::string_view prefix = "[mob-ai]\nenabled = true\n";
            return ::read(fd, buffer, prefix.size());
        }
        errno = EIO;
        return -1;
    };
    const auto result = subject.reload();
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("Cannot read"), std::string::npos);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    EXPECT_EQ(read(policies), sleep);
    EntityAiPoliciesTestAccess::fileOperations(subject).read = nullptr;
    ASSERT_TRUE(subject.reload());
}

TEST_F(EntityAiTest, DuplicateFeatureReloadPreservesPriorDecisionAndRevision)
{
    const auto rule = "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n[mob-ai.overrides.idle]\n";
    write(policies, sleep + rule);
    ASSERT_TRUE(subject.initialize(policies));
    const auto before = subject.resolve("minecraft:cow");
    write(policies, sleep + rule + rule);
    EXPECT_FALSE(subject.reload());
    EXPECT_EQ(subject.resolve("minecraft:cow"), before);
}

TEST_F(EntityAiTest, ConfigCloseFailureIsNotRetriedAndPreservesSnapshot)
{
    write(policies, sleep);
    ASSERT_TRUE(subject.initialize(policies));
    const auto before = subject.resolve("minecraft:villager_v2");
    write(policies, "[mob-ai]\nenabled=true\n");
    static int close_count;
    close_count = 0;
    EntityAiPoliciesTestAccess::fileOperations(subject).close = [](int fd) {
        ++close_count;
        ::close(fd);
        errno = EIO;
        return -1;
    };
    const auto result = subject.reload();
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("Cannot close"), std::string::npos);
    EXPECT_EQ(close_count, 1);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    EntityAiPoliciesTestAccess::fileOperations(subject).close = nullptr;
    const auto repaired = subject.reload();
    ASSERT_TRUE(repaired);
    EXPECT_TRUE(repaired->changed);
    EXPECT_FALSE(subject.resolve("minecraft:villager_v2").prevent_sleep);
}

TEST_F(EntityAiTest, InterruptedConfigReadRetriesAndCommitsCompleteSnapshot)
{
    write(policies, sleep);
    static int read_count;
    read_count = 0;
    EntityAiPoliciesTestAccess::fileOperations(subject).read = [](int fd, void *buffer, std::size_t count) -> std::ptrdiff_t {
        if (++read_count == 1) {
            errno = EINTR;
            return -1;
        }
        return ::read(fd, buffer, count);
    };
    const auto result = subject.initialize(policies);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->changed);
    EXPECT_GE(read_count, 3);
    EXPECT_TRUE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    EXPECT_EQ(read(policies), sleep);
    EntityAiPoliciesTestAccess::fileOperations(subject).read = nullptr;
}

TEST_F(EntityAiTest, FailedInitializeCanBeRepairedThroughReload)
{
    write(policies, "[mob-ai]\nenabled=1\n");
    ASSERT_FALSE(subject.initialize(policies));
    EXPECT_FALSE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    write(policies, sleep);
    const auto repaired = subject.reload();
    ASSERT_TRUE(repaired) << repaired.error();
    EXPECT_TRUE(repaired->changed);
    EXPECT_TRUE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2").revision, 1);
}

TEST_F(EntityAiTest, MissingConfigIsInactiveAndDoesNotCreateSidecars)
{
    const auto result = subject.initialize(policies);
    ASSERT_TRUE(result) << result.error();
    EXPECT_FALSE(result->changed);
    EXPECT_EQ(result->override_types, 0);
    EXPECT_EQ(result->revision, 0);
    const auto reloaded = subject.reload();
    ASSERT_TRUE(reloaded);
    EXPECT_FALSE(reloaded->changed);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2").revision, 0);
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(EntityAiTest, ExactOverrideRemovalRestoresGlobalDefaultsAndCountsUniqueTypes)
{
    write(policies, sleep + R"([[mob-ai.overrides]]
types = ["minecraft:cow", "minecraft:pig"]
[mob-ai.overrides.idle]
enabled = false
[[mob-ai.overrides]]
types = ["minecraft:cow"]
[mob-ai.overrides.target-search]
interval-ticks = 40
)");
    const auto initialized = subject.initialize(policies);
    ASSERT_TRUE(initialized);
    EXPECT_EQ(initialized->override_types, 2);
    EXPECT_FALSE(subject.resolve("minecraft:cow").animal);
    EXPECT_TRUE(subject.resolve("minecraft:sheep").animal);
    write(policies, sleep);
    const auto result = subject.reload();
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->changed);
    EXPECT_EQ(result->override_types, 0);
    const auto cow = subject.resolve("minecraft:cow");
    ASSERT_TRUE(cow.animal);
    EXPECT_EQ(cow.animal->activation_range_blocks, 32);
    EXPECT_EQ(cow.target_search_interval_ticks, 20);
    EXPECT_EQ(cow.revision, initialized->revision + 1);
}

TEST_F(EntityAiTest, FailedReloadPreservesSnapshotAndRevisionAndDoesNotRewriteConfig)
{
    write(policies, sleep);
    ASSERT_TRUE(subject.initialize(policies));
    const auto revision = subject.resolve("minecraft:villager_v2").revision;
    ASSERT_GT(revision, 0);
    write(policies, sleep + "unknown = true\n");
    const auto content = read(policies);
    EXPECT_FALSE(subject.reload());
    EXPECT_EQ(read(policies), content);
    EXPECT_TRUE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2").revision, revision);
    write(policies, sleep + "# semantically unchanged\n");
    const auto unchanged = subject.reload();
    ASSERT_TRUE(unchanged);
    EXPECT_FALSE(unchanged->changed);
    EXPECT_EQ(unchanged->revision, revision);
    write(policies, "[mob-ai]\nenabled=true\n");
    const auto removed = subject.reload();
    ASSERT_TRUE(removed);
    EXPECT_TRUE(removed->changed);
    EXPECT_FALSE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    EXPECT_EQ(removed->revision, revision + 1);
}

TEST_F(EntityAiTest, LoadedConfigCannotDisappearAndCanRecover)
{
    write(policies, sleep);
    ASSERT_TRUE(subject.initialize(policies));
    const auto before = subject.resolve("minecraft:villager_v2");
    std::filesystem::remove(policies);
    EXPECT_FALSE(subject.reload());
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    std::filesystem::create_directory(policies);
    const auto non_file = subject.reload();
    ASSERT_FALSE(non_file);
    EXPECT_NE(non_file.error().find("Expected regular file"), std::string::npos);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    std::filesystem::remove(policies);
    write(policies, sleep);
    const auto recovered = subject.reload();
    ASSERT_TRUE(recovered);
    EXPECT_FALSE(recovered->changed);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
}

TEST_F(EntityAiTest, LegacyFilesAreIgnoredAndNeverRewritten)
{
    write(policies, sleep);
    const std::vector<std::pair<std::string, std::string>> legacy{
        {"entity-ai.toml", "not valid TOML ["}, {"entity-ai-bindings.json", "not valid JSON"},
        {"endstone-world-id", "not a UUID\n"}};
    for (const auto &[name, content] : legacy) {
        write(directory / name, content);
    }
    const auto initialized = subject.initialize(policies);
    ASSERT_TRUE(initialized);
    const auto before = subject.resolve("minecraft:villager_v2");
    EXPECT_TRUE(before.prevent_sleep);
    for (const auto &[name, content] : legacy) {
        EXPECT_EQ(read(directory / name), content);
        write(directory / name, "changed legacy contents");
    }
    const auto reloaded = subject.reload();
    ASSERT_TRUE(reloaded);
    EXPECT_FALSE(reloaded->changed);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    for (const auto &[name, content] : legacy) {
        EXPECT_EQ(read(directory / name), "changed legacy contents");
        std::filesystem::remove(directory / name);
    }
    ASSERT_TRUE(subject.reload());
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), before);
    for (const auto &[name, content] : legacy) {
        EXPECT_FALSE(std::filesystem::exists(directory / name));
    }
}
#endif

}  // namespace
}  // namespace endstone::core
