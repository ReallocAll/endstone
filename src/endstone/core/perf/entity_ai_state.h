#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace endstone::core {

// Main-thread state; native availability is fixed before server threads start.
class EntityAiState {
public:
    static void setNativeAvailable(bool available);
    [[nodiscard]] static bool isNativeAvailable();

    void recordActivity(std::int64_t id, std::uint64_t tick);
    [[nodiscard]] bool recentlyActive(std::int64_t id, std::uint64_t tick);
    bool allowNewTargetSearch(std::int64_t id, std::uint64_t tick, std::optional<std::uint32_t> interval);
    void clearSearch(std::int64_t id);
    void clearSearchDelays();
    void erase(std::int64_t id);
    void prune(std::uint64_t tick);
    void clear();

private:
    friend struct EntityAiStateTestAccess;
    struct Entry {
        std::uint64_t protected_until = 0;
        std::uint64_t search_after = 0;
    };
    std::unordered_map<std::int64_t, Entry> entries_;
    std::uint64_t prune_after_ = 0;
};

}  // namespace endstone::core
