#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "endstone/util/result.h"

namespace endstone::core {

struct EntityAiAnimalPolicy {
    double activation_range_blocks;
    bool random_stroll = false;
    bool random_look_around = false;
    bool operator==(const EntityAiAnimalPolicy &) const = default;
};

struct EntityAiDecision {
    bool prevent_sleep = false;
    std::optional<EntityAiAnimalPolicy> animal;
    std::optional<std::uint32_t> target_search_interval_ticks;
    std::uint64_t revision = 0;
    std::optional<double> target_search_player_range_blocks;
    bool operator==(const EntityAiDecision &) const = default;
};

struct EntityAiReloadResult {
    std::size_t override_types = 0;
    bool changed = false;
    std::uint64_t revision = 0;
};

// Main-thread only; returned decisions do not refer to the active snapshot.
class EntityAiPolicies {
public:
    Result<EntityAiReloadResult> initialize(const std::filesystem::path &policies_file);
    Result<EntityAiReloadResult> reload();
    [[nodiscard]] EntityAiDecision resolve(std::string_view type) const;

private:
    friend struct EntityAiPoliciesTestAccess;

    struct FileOperations {
        std::ptrdiff_t (*read)(int, void *, std::size_t) = nullptr;
        int (*close)(int) = nullptr;
    };

    struct Snapshot {
        bool present = false;
        bool enabled = false;
        EntityAiDecision default_decision;
        std::map<std::string, EntityAiDecision, std::less<>> overrides;
        bool operator==(const Snapshot &) const = default;
    };

    static Snapshot parse(const std::optional<std::string> &policies);
    static std::optional<std::string> readOptional(const std::filesystem::path &path, const FileOperations &operations);

    FileOperations file_operations_;

    Snapshot snapshot_;
    std::filesystem::path policies_file_;
    std::uint64_t revision_ = 0;
    bool initialized_ = false;
};

}  // namespace endstone::core
