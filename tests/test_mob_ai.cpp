#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>

#include "endstone/core/config.h"
#include "endstone/core/perf/mob_ai.h"
#include "endstone/core/perf/mob_ai_capabilities.h"

namespace endstone::core {

struct MobAiPoliciesTestAccess {
    static void parse(MobAiPolicies &subject, const toml::table &config)
    {
        subject.snapshot_ = MobAiPolicies::parse(config);
    }

    static MobAiDecision parseAndResolve(std::string_view config, std::string_view type)
    {
        MobAiPolicies subject;
        parse(subject, toml::parse(config));
        return subject.resolve(type);
    }
};

namespace {

TEST(MobAiCapabilitiesTest, IdleAdmission)
{
    EXPECT_TRUE(supportsMobAiIdle("minecraft:cow", ActorType::Mob));
    EXPECT_TRUE(supportsMobAiIdle("minecraft:spider", ActorType::Mob));
    EXPECT_FALSE(supportsMobAiIdle("minecraft:cow", ActorType::Undefined));
    EXPECT_FALSE(supportsMobAiIdle("minecraft:cow", ActorType::Player));
    EXPECT_FALSE(supportsMobAiIdle("minecraft:villager_v2", ActorType::Mob));
    EXPECT_FALSE(supportsMobAiIdle("minecraft:ender_dragon", ActorType::Mob));
    EXPECT_FALSE(supportsMobAiIdle("custom:cow", ActorType::Mob));
}

TEST(MobAiCapabilitiesTest, SearchAndActivityRequireSupportedCapabilities)
{
    MobAiDecision decision;
    decision.target_search_interval_ticks = 20;
    decision.target_search_player_range_blocks = 32;
    EXPECT_TRUE(supportsMobAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    EXPECT_TRUE(supportsMobAiSearch("minecraft:skeleton", ActorType::Skeleton, decision));
    EXPECT_TRUE(hasMobAiActivityCapability("minecraft:zombie", ActorType::Zombie, decision));
    EXPECT_FALSE(supportsMobAiSearch("minecraft:zombie", ActorType::Mob, decision));
    EXPECT_FALSE(supportsMobAiSearch("minecraft:spider", ActorType::Spider, decision));
    EXPECT_FALSE(hasMobAiActivityCapability("minecraft:spider", ActorType::Spider, decision));
    decision.animal = MobAiAnimalPolicy{16, false, false};
    decision.target_search_player_range_blocks.reset();
    EXPECT_FALSE(supportsMobAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    EXPECT_FALSE(hasMobAiActivityCapability("minecraft:cow", ActorType::Cow, decision));
    decision.animal->random_stroll = true;
    EXPECT_TRUE(hasMobAiActivityCapability("minecraft:cow", ActorType::Cow, decision));
    EXPECT_FALSE(hasMobAiActivityCapability("minecraft:villager_v2", ActorType::Villager, decision));
    decision.target_search_player_range_blocks = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(supportsMobAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    decision.target_search_player_range_blocks = 32;
    decision.target_search_interval_ticks.reset();
    EXPECT_FALSE(supportsMobAiSearch("minecraft:zombie", ActorType::Zombie, decision));
    decision.animal.reset();
    decision.prevent_sleep = true;
    EXPECT_FALSE(hasMobAiActivityCapability("minecraft:villager_v2", ActorType::Villager, decision));
}

TEST(MobAiCapabilitiesTest, FeetDistanceIsInclusiveAndThreeDimensional)
{
    EXPECT_EQ(mobAiWithinRange({0, 0, 0}, {3, 4, 0}, 5), true);
    EXPECT_EQ(mobAiWithinRange({0, 0, 0}, {3, 4, 0}, 4.99), false);
    EXPECT_EQ(mobAiWithinRange({0, 0, 0}, {0, 0, 6}, 5), false);
    EXPECT_FALSE(mobAiWithinRange({std::numeric_limits<double>::quiet_NaN(), 0, 0}, {0, 0, 0}, 32).has_value());
    EXPECT_FALSE(mobAiWithinRange({0, 0, 0}, {0, 0, 0}, 0).has_value());
}

class MobAiTest : public testing::Test {
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
    MobAiPolicies subject;
};

TEST_F(MobAiTest, MissingOrDisabledMasterKeepsVanilla)
{
    for (const auto &config : std::vector<std::string>{
             "", "[server]\nname='example'\n",
             "[mob-ai.villagers]\nprevent-sleep=true\n[[mob-ai.overrides]]\n"
             "types=['minecraft:villager_v2']\n[mob-ai.overrides.idle]\nenabled=true\n",
             "[mob-ai]\nenabled=false\n[mob-ai.villagers]\nprevent-sleep=true\n"
             "[[mob-ai.overrides]]\ntypes=['minecraft:villager_v2']\n[mob-ai.overrides.idle]\nenabled=true\n"}) {
        const auto decision = MobAiPoliciesTestAccess::parseAndResolve(config, "minecraft:villager_v2");
        EXPECT_FALSE(decision.prevent_sleep);
        EXPECT_FALSE(decision.animal);
        EXPECT_FALSE(decision.target_search_interval_ticks);
        EXPECT_FALSE(decision.target_search_player_range_blocks);
    }
}

TEST_F(MobAiTest, DefaultsAndExactFeatureOverridesCompose)
{
    const std::string config = R"([mob-ai]
enabled = true
[[mob-ai.overrides]]
types = ["minecraft:skeleton", "minecraft:villager_v2"]
[mob-ai.overrides.idle]
player-range-blocks = 16
disable-random-stroll = false
[[mob-ai.overrides]]
types = ["minecraft:skeleton", "minecraft:villager_v2"]
[mob-ai.overrides.target-search]
player-range-blocks = 8
interval-ticks = 40
[[mob-ai.overrides]]
types = ["minecraft:skeleton", "minecraft:villager_v2"]
[mob-ai.overrides.villagers]
prevent-sleep = true
)";
    const auto cow = MobAiPoliciesTestAccess::parseAndResolve(config, "minecraft:cow");
    ASSERT_TRUE(cow.animal);
    EXPECT_EQ(cow.animal->activation_range_blocks, 32);
    EXPECT_TRUE(cow.animal->random_stroll);
    EXPECT_TRUE(cow.animal->random_look_around);
    EXPECT_EQ(cow.target_search_interval_ticks, 20);
    EXPECT_EQ(cow.target_search_player_range_blocks, 32);
    EXPECT_FALSE(cow.prevent_sleep);
    for (const auto type : {"minecraft:skeleton", "minecraft:villager_v2"}) {
        const auto decision = MobAiPoliciesTestAccess::parseAndResolve(config, type);
        ASSERT_TRUE(decision.animal);
        EXPECT_EQ(decision.animal->activation_range_blocks, 16);
        EXPECT_FALSE(decision.animal->random_stroll);
        EXPECT_TRUE(decision.animal->random_look_around);
        EXPECT_EQ(decision.target_search_interval_ticks, 40);
        EXPECT_EQ(decision.target_search_player_range_blocks, 8);
        EXPECT_EQ(decision.prevent_sleep, std::string_view(type) == "minecraft:villager_v2");
    }
    EXPECT_FALSE(MobAiPoliciesTestAccess::parseAndResolve(sleep, "minecraft:villager").prevent_sleep);
}

TEST_F(MobAiTest, FeatureOverridesCanReenableAndInheritGlobals)
{
    const std::string config = R"([mob-ai]
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
    const auto skeleton = MobAiPoliciesTestAccess::parseAndResolve(config, "minecraft:skeleton");
    ASSERT_TRUE(skeleton.animal);
    EXPECT_EQ(skeleton.animal->activation_range_blocks, 12);
    EXPECT_FALSE(skeleton.animal->random_stroll);
    EXPECT_TRUE(skeleton.animal->random_look_around);
    EXPECT_EQ(skeleton.target_search_interval_ticks, 43);
    EXPECT_EQ(skeleton.target_search_player_range_blocks, 9);
    const auto zombie = MobAiPoliciesTestAccess::parseAndResolve(config, "minecraft:zombie");
    EXPECT_FALSE(zombie.animal);
    EXPECT_FALSE(zombie.target_search_interval_ticks);
    EXPECT_FALSE(zombie.target_search_player_range_blocks);
}

TEST_F(MobAiTest, FeaturesCanBeDisabledIndependently)
{
    const auto search_only = MobAiPoliciesTestAccess::parseAndResolve(
        "[mob-ai]\nenabled=true\n[mob-ai.idle]\nenabled=false\nplayer-range-blocks=64\n"
        "[mob-ai.target-search]\nplayer-range-blocks=12.5\ninterval-ticks=40\n",
        "minecraft:zombie");
    EXPECT_FALSE(search_only.prevent_sleep);
    EXPECT_FALSE(search_only.animal);
    EXPECT_EQ(search_only.target_search_interval_ticks, 40);
    EXPECT_EQ(search_only.target_search_player_range_blocks, 12.5);
    EXPECT_TRUE(supportsMobAiSearch("minecraft:zombie", ActorType::Monster, search_only));
    const auto idle_only = MobAiPoliciesTestAccess::parseAndResolve(
        "[mob-ai]\nenabled=true\n[mob-ai.target-search]\nenabled=false\n", "minecraft:zombie");
    EXPECT_TRUE(idle_only.animal);
    EXPECT_FALSE(idle_only.prevent_sleep);
    EXPECT_FALSE(idle_only.target_search_interval_ticks);
    EXPECT_FALSE(idle_only.target_search_player_range_blocks);
    const auto sleep_only = MobAiPoliciesTestAccess::parseAndResolve(
        sleep + "[mob-ai.idle]\nenabled=false\n[mob-ai.target-search]\nenabled=false\n", "minecraft:villager_v2");
    EXPECT_TRUE(sleep_only.prevent_sleep);
    EXPECT_FALSE(sleep_only.animal);
    EXPECT_FALSE(sleep_only.target_search_interval_ticks);
    EXPECT_FALSE(sleep_only.target_search_player_range_blocks);
}

TEST_F(MobAiTest, InvalidSchemaIsRejectedEvenWhenDisabled)
{
    const std::string rule = "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n[mob-ai.overrides.idle]\n";
    for (const auto &[label, config] : std::vector<std::pair<std::string, std::string>>{
             {"non-table root", "mob-ai=false"},
             {"wrong master boolean", "[mob-ai]\nenabled=1"},
             {"unknown field while disabled", "[mob-ai]\nenabled=false\nunknown=true"},
             {"non-table feature", "[mob-ai]\nenabled=false\nidle=[]"},
             {"empty types", "[[mob-ai.overrides]]\ntypes=[]"},
             {"duplicate types", "[[mob-ai.overrides]]\ntypes=['minecraft:cow','minecraft:cow']"},
             {"wildcard selector", "[[mob-ai.overrides]]\ntypes=['minecraft:*']"},
             {"disjoint fields repeat feature",
              rule + "disable-random-stroll=false\n" + rule + "disable-random-look-around=false\n"}}) {
        SCOPED_TRACE(label);
        EXPECT_THROW(MobAiPoliciesTestAccess::parseAndResolve(config, "minecraft:cow"), std::exception);
    }
}

TEST_F(MobAiTest, NumericBoundsAreValidated)
{
    for (const std::string interval : {"1", "4294967295"}) {
        const auto decision =
            MobAiPoliciesTestAccess::parseAndResolve("[mob-ai]\nenabled=true\n[mob-ai.idle]\nplayer-range-blocks=0.5\n"
                                                     "[mob-ai.target-search]\ninterval-ticks=" +
                                                         interval,
                                                     "minecraft:cow");
        ASSERT_TRUE(decision.animal);
        EXPECT_EQ(decision.animal->activation_range_blocks, 0.5);
        EXPECT_EQ(decision.target_search_interval_ticks, interval == "1" ? 1U : UINT32_MAX);
    }
    for (const std::string range : {"0", "nan", "'32'"}) {
        SCOPED_TRACE(range);
        EXPECT_THROW(
            MobAiPoliciesTestAccess::parseAndResolve("[mob-ai.idle]\nplayer-range-blocks=" + range, "minecraft:cow"),
            std::exception);
    }
    for (const std::string interval : {"0", "4294967296", "2.0"}) {
        SCOPED_TRACE(interval);
        EXPECT_THROW(MobAiPoliciesTestAccess::parseAndResolve("[mob-ai.target-search]\ninterval-ticks=" + interval,
                                                              "minecraft:cow"),
                     std::exception);
    }
}

TEST_F(MobAiTest, MaximumIntegerPlayerRangesDoNotDereferenceEmptyConversion)
{
    const auto decision = MobAiPoliciesTestAccess::parseAndResolve(R"([mob-ai]
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

TEST_F(MobAiTest, SnapshotOwnsValuesAfterTableMutationAndDestruction)
{
    MobAiDecision cow;
    MobAiDecision villager;
    {
        auto config = toml::parse(sleep + "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n"
                                          "[mob-ai.overrides.idle]\nplayer-range-blocks=16\n");
        MobAiPoliciesTestAccess::parse(subject, config);
        cow = subject.resolve("minecraft:cow");
        villager = subject.resolve("minecraft:villager_v2");
        ASSERT_TRUE(cow.animal);
        EXPECT_EQ(cow.animal->activation_range_blocks, 16);
        ASSERT_TRUE(villager.prevent_sleep);
        config.clear();
        config.insert_or_assign("mob-ai", false);
        EXPECT_EQ(subject.resolve("minecraft:cow"), cow);
        EXPECT_EQ(subject.resolve("minecraft:villager_v2"), villager);
    }
    EXPECT_EQ(subject.resolve("minecraft:cow"), cow);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), villager);
}

#ifdef _WIN32
TEST_F(MobAiTest, WindowsCannotActivateOrModifyMainConfiguration)
{
    write(policies, sleep);
    EXPECT_FALSE(subject.initialize(loadConfig(policies)));
    EXPECT_FALSE(subject.resolve("minecraft:villager_v2").prevent_sleep);
    EXPECT_EQ(read(policies), sleep);
}
#else
TEST_F(MobAiTest, MissingConfigStaysInactiveAndDoesNotWrite)
{
    ASSERT_TRUE(subject.initialize(loadConfig(policies)));
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), MobAiDecision{});
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

#ifdef __linux__
TEST_F(MobAiTest, ReadFailureAfterValidPrefixDoesNotPublishTruncatedConfig)
{
    struct Cookie {
        std::string_view content = "[commands]\nlog=false\n[mob-ai]\nenabled=true\n";
        std::size_t offset = 0;
    } cookie;
    ASSERT_EQ(toml::parse(cookie.content).at_path("mob-ai.enabled").value<bool>(), true);
    cookie_io_functions_t operations{};
    operations.read = [](void *state, char *buffer, std::size_t size) -> ssize_t {
        auto &cookie = *static_cast<Cookie *>(state);
        if (cookie.offset == cookie.content.size()) {
            errno = EIO;
            return -1;
        }
        const auto count = std::min(size, cookie.content.size() - cookie.offset);
        std::memcpy(buffer, cookie.content.data() + cookie.offset, count);
        cookie.offset += count;
        return static_cast<ssize_t>(count);
    };
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(::fopencookie(&cookie, "r", operations),
                                                                  &std::fclose);
    ASSERT_NE(file, nullptr);
    try {
        (void)subject.initialize(config_detail::loadConfig(file.get(), policies));
        FAIL() << "Read failure accepted a valid TOML prefix.";
    }
    catch (const std::filesystem::filesystem_error &error) {
        EXPECT_EQ(error.code(), std::error_code(EIO, std::generic_category()));
    }
    EXPECT_EQ(cookie.offset, cookie.content.size());
    EXPECT_NE(std::ferror(file.get()), 0);
    EXPECT_EQ(subject.resolve("minecraft:cow"), MobAiDecision{});
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), MobAiDecision{});
}
#endif

TEST_F(MobAiTest, StartupSnapshotIgnoresFileChangesBeforeInitialization)
{
    write(policies, sleep + "[[mob-ai.overrides]]\ntypes=['minecraft:cow']\n"
                            "[mob-ai.overrides.idle]\nplayer-range-blocks=16\n");
    const auto config = loadConfig(policies);
    write(policies, "[mob-ai]\nenabled=false\n");
    ASSERT_TRUE(subject.initialize(config));
    const auto cow = subject.resolve("minecraft:cow");
    const auto villager = subject.resolve("minecraft:villager_v2");
    ASSERT_TRUE(cow.animal);
    ASSERT_EQ(cow.animal->activation_range_blocks, 16);
    ASSERT_TRUE(villager.prevent_sleep);
    EXPECT_FALSE(subject.initialize(loadConfig(policies)));
    EXPECT_EQ(subject.resolve("minecraft:cow"), cow);
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), villager);
    MobAiPolicies restarted;
    ASSERT_TRUE(restarted.initialize(loadConfig(policies)));
    EXPECT_EQ(restarted.resolve("minecraft:cow"), MobAiDecision{});
    EXPECT_EQ(restarted.resolve("minecraft:villager_v2"), MobAiDecision{});
}

TEST_F(MobAiTest, SchemaFailureStaysInactiveAndRejectsSecondInitialization)
{
    const auto config = toml::parse("[commands]\nlog=false\n[mob-ai]\nenabled=true\nunknown=true\n");
    EXPECT_FALSE(subject.initialize(config));
    EXPECT_EQ(config.at_path("commands.log").value<bool>(), false);
    EXPECT_EQ(subject.resolve("minecraft:cow"), MobAiDecision{});
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), MobAiDecision{});
    EXPECT_FALSE(subject.initialize(toml::parse(sleep)));
    EXPECT_EQ(subject.resolve("minecraft:cow"), MobAiDecision{});
    EXPECT_EQ(subject.resolve("minecraft:villager_v2"), MobAiDecision{});
}
#endif

}  // namespace
}  // namespace endstone::core
