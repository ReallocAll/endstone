#include "endstone/core/perf/entity_ai.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <toml++/toml.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace endstone::core {
namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void keys(const toml::table &table, std::initializer_list<std::string_view> allowed)
{
    for (const auto &[key, value] : table) {
        require(std::find(allowed.begin(), allowed.end(), key.str()) != allowed.end(),
                "Unknown policy field '" + std::string(key.str()) + "'.");
    }
}

std::set<std::string, std::less<>> strings(const toml::node *node, std::string_view field)
{
    require(node && node->is_array(), "Expected array for '" + std::string(field) + "'.");
    std::set<std::string, std::less<>> result;
    for (const auto &entry : *node->as_array()) {
        const auto value = entry.value<std::string>();
        require(value && !value->empty(), "Expected nonempty strings in '" + std::string(field) + "'.");
        require(result.insert(*value).second, "Duplicate string in '" + std::string(field) + "'.");
    }
    require(!result.empty(), "Empty array for '" + std::string(field) + "'.");
    return result;
}

bool boolean(const toml::table &table, std::string_view field, bool fallback)
{
    const auto *node = table.get(field);
    if (!node) {
        return fallback;
    }
    require(node->is_boolean(), std::string(field) + " must be a boolean.");
    return *node->value<bool>();
}

double playerRange(const toml::node &node)
{
    require(node.is_integer() || node.is_floating_point(), "player-range-blocks must be numeric.");
    const auto range = node.is_integer() ? static_cast<double>(node.as_integer()->get())
                                         : node.as_floating_point()->get();
    require(std::isfinite(range) && range > 0, "player-range-blocks must be finite and positive.");
    return range;
}

bool exactEntityType(std::string_view type)
{
    const auto separator = type.find(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == type.size()) {
        return false;
    }
    const auto identifier_character = [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_' || character == '-' || character == '.';
    };
    const auto namespace_name = type.substr(0, separator);
    const auto path = type.substr(separator + 1);
    return std::all_of(namespace_name.begin(), namespace_name.end(), identifier_character) &&
           std::all_of(path.begin(), path.end(), [&](char character) {
               return identifier_character(character) || character == '/';
           });
}

struct MobAiSettings {
    bool idle_enabled = true;
    EntityAiAnimalPolicy idle{32, true, true};
    bool target_search_enabled = true;
    double target_search_player_range_blocks = 32;
    std::uint32_t target_search_interval_ticks = 20;
    bool prevent_sleep = false;
};

MobAiSettings mobAiSettings(const toml::table &table, MobAiSettings settings)
{
    if (const auto *node = table.get("idle")) {
        require(node->is_table(), "Expected idle table.");
        const auto &idle = *node->as_table();
        keys(idle, {"enabled", "player-range-blocks", "disable-random-stroll", "disable-random-look-around"});
        settings.idle_enabled = boolean(idle, "enabled", settings.idle_enabled);
        if (const auto *range = idle.get("player-range-blocks")) {
            settings.idle.activation_range_blocks = playerRange(*range);
        }
        settings.idle.random_stroll = boolean(idle, "disable-random-stroll", settings.idle.random_stroll);
        settings.idle.random_look_around =
            boolean(idle, "disable-random-look-around", settings.idle.random_look_around);
    }
    if (const auto *node = table.get("target-search")) {
        require(node->is_table(), "Expected target-search table.");
        const auto &search = *node->as_table();
        keys(search, {"enabled", "player-range-blocks", "interval-ticks"});
        settings.target_search_enabled = boolean(search, "enabled", settings.target_search_enabled);
        if (const auto *range = search.get("player-range-blocks")) {
            settings.target_search_player_range_blocks = playerRange(*range);
        }
        if (const auto *interval = search.get("interval-ticks")) {
            require(interval->is_integer(), "interval-ticks must be an integer.");
            const auto ticks = *interval->value<std::int64_t>();
            require(ticks >= 1 && ticks <= std::numeric_limits<std::uint32_t>::max(),
                    "interval-ticks must be between 1 and 4294967295.");
            settings.target_search_interval_ticks = static_cast<std::uint32_t>(ticks);
        }
    }
    if (const auto *node = table.get("villagers")) {
        require(node->is_table(), "Expected villagers table.");
        const auto &villagers = *node->as_table();
        keys(villagers, {"prevent-sleep"});
        settings.prevent_sleep = boolean(villagers, "prevent-sleep", settings.prevent_sleep);
    }
    return settings;
}

EntityAiDecision mobAiDecision(const MobAiSettings &settings)
{
    EntityAiDecision result;
    result.prevent_sleep = settings.prevent_sleep;
    if (settings.idle_enabled) {
        result.animal = settings.idle;
    }
    if (settings.target_search_enabled) {
        result.target_search_player_range_blocks = settings.target_search_player_range_blocks;
        result.target_search_interval_ticks = settings.target_search_interval_ticks;
    }
    return result;
}

#ifndef _WIN32
void checkIo(bool success, const char *operation)
{
    if (!success) {
        throw std::system_error(errno, std::generic_category(), operation);
    }
}

class FileDescriptor {
public:
    explicit FileDescriptor(int fd, int (*close)(int)) : fd_(fd), close_(close ? close : ::close)
    {
        if (fd_ < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot open entity AI file");
        }
    }
    ~FileDescriptor() noexcept
    {
        if (fd_ >= 0) {
            close_(fd_);
        }
    }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    [[nodiscard]] int get() const { return fd_; }
    void close(const char *operation)
    {
        const auto fd = std::exchange(fd_, -1);
        checkIo(close_(fd) == 0, operation);
    }

private:
    int fd_;
    int (*close_)(int);
};
#endif

}  // namespace

#ifndef _WIN32
std::optional<std::string> EntityAiPolicies::readOptional(const std::filesystem::path &path,
                                                          const FileOperations &operations)
{
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    require(std::filesystem::is_regular_file(path), "Expected regular file: " + path.string());
    FileDescriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC), operations.close);
    std::array<char, 4096> buffer;
    std::string content;
    for (;;) {
        const auto count = operations.read ? operations.read(file.get(), buffer.data(), buffer.size())
                                           : ::read(file.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        checkIo(count >= 0, ("Cannot read " + path.string()).c_str());
        if (count == 0) {
            break;
        }
        content.append(buffer.data(), static_cast<std::size_t>(count));
    }
    file.close(("Cannot close " + path.string()).c_str());
    return content;
}
#endif

EntityAiPolicies::Snapshot EntityAiPolicies::parse(const std::optional<std::string> &policies)
{
    Snapshot result;
    if (!policies) {
        return result;
    }
    const auto root = toml::parse(*policies);
    const auto *section = root.get("mob-ai");
    require(!section || section->is_table(), "Expected mob-ai table.");
    if (!section) {
        return result;
    }
    const auto &config = *section->as_table();
    result.present = true;
    keys(config, {"enabled", "idle", "target-search", "villagers", "overrides"});
    result.enabled = boolean(config, "enabled", false);
    const auto settings = mobAiSettings(config, MobAiSettings{});
    result.default_decision = mobAiDecision(settings);
    if (const auto *overrides = config.get("overrides")) {
        require(overrides->is_array(), "Expected mob-ai overrides array.");
        std::array<std::set<std::string, std::less<>>, 3> assigned;
        constexpr std::array features{"idle", "target-search", "villagers"};
        for (const auto &entry : *overrides->as_array()) {
            require(entry.is_table(), "Expected table for mob-ai override.");
            const auto &table = *entry.as_table();
            keys(table, {"types", "idle", "target-search", "villagers"});
            const auto types = strings(table.get("types"), "types");
            const auto decision = mobAiDecision(mobAiSettings(table, settings));
            for (const auto &type : types) {
                require(exactEntityType(type),
                        "Expected exact namespace:path entity type, got '" + type + "'.");
                auto &resolved = result.overrides.try_emplace(type, result.default_decision).first->second;
                for (std::size_t index = 0; index < features.size(); ++index) {
                    if (!table.contains(features[index])) {
                        continue;
                    }
                    require(assigned[index].insert(type).second,
                            "Multiple mob-ai " + std::string(features[index]) + " overrides for '" + type + "'.");
                    if (index == 0) {
                        resolved.animal = decision.animal;
                    }
                    else if (index == 1) {
                        resolved.target_search_interval_ticks = decision.target_search_interval_ticks;
                        resolved.target_search_player_range_blocks = decision.target_search_player_range_blocks;
                    }
                    else {
                        resolved.prevent_sleep = decision.prevent_sleep;
                    }
                }
            }
        }
    }
    return result;
}

Result<EntityAiReloadResult> EntityAiPolicies::initialize(const std::filesystem::path &policies_file)
{
#ifdef _WIN32
    return nonstd::make_unexpected(std::string("Entity AI policies are not supported on Windows."));
#else
    ENDSTONE_CHECK(!initialized_, std::string("Entity AI policies are already initialized."));
    policies_file_ = policies_file;
    initialized_ = true;
    return reload();
#endif
}

Result<EntityAiReloadResult> EntityAiPolicies::reload()
{
#ifdef _WIN32
    return nonstd::make_unexpected(std::string("Entity AI policies are not supported on Windows."));
#else
    try {
        require(initialized_, "Entity AI policies have not been initialized.");
        auto candidate = parse(readOptional(policies_file_, file_operations_));
        require(!snapshot_.present || candidate.present, "Previously loaded mob-ai section is missing.");
        const auto changed = candidate != snapshot_;
        if (changed) {
            snapshot_ = std::move(candidate);
            ++revision_;
        }
        return EntityAiReloadResult{snapshot_.overrides.size(), changed, revision_};
    }
    catch (const std::exception &error) {
        return nonstd::make_unexpected(std::string(error.what()));
    }
#endif
}

EntityAiDecision EntityAiPolicies::resolve(std::string_view type) const
{
    EntityAiDecision result;
    result.revision = revision_;
    if (!snapshot_.enabled) {
        return result;
    }
    const auto found = snapshot_.overrides.find(type);
    result = found == snapshot_.overrides.end() ? snapshot_.default_decision : found->second;
    result.prevent_sleep = result.prevent_sleep && type == "minecraft:villager_v2";
    result.revision = revision_;
    return result;
}

}  // namespace endstone::core
