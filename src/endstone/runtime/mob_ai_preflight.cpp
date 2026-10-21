#include "endstone/runtime/mob_ai_preflight.h"

#ifdef __linux__

#include <link.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#include "bedrock/symbol.h"

namespace endstone::runtime {
namespace {
bool contains(const dl_phdr_info &info, std::uintptr_t address, std::size_t size, unsigned flags)
{
    for (std::size_t i = 0; i < info.dlpi_phnum; ++i) {
        const auto &ph = info.dlpi_phdr[i];
        if (ph.p_type != PT_LOAD || (ph.p_flags & flags) != flags ||
            ph.p_vaddr > std::numeric_limits<std::uintptr_t>::max() - info.dlpi_addr) {
            continue;
        }
        const auto start = info.dlpi_addr + ph.p_vaddr;
        if (address >= start && address - start <= ph.p_memsz && size <= ph.p_memsz - (address - start)) {
            return true;
        }
    }
    return false;
}

struct Check {
    std::uintptr_t base;
    const char *error = "executable ELF headers were not found";
};

int checkExecutable(dl_phdr_info *info, std::size_t, void *context)
{
    if (info->dlpi_name && *info->dlpi_name) {
        return 0;
    }
    auto &check = *static_cast<Check *>(context);
    if (info->dlpi_addr != check.base) {
        check.error = "executable ELF base does not match the resolved base";
        return 1;
    }
    for (const auto &hook : mob_ai_hooks) {
        const auto slot_offset = hook.layout.vtable + hook.slot * sizeof(void *);
        const auto max = std::numeric_limits<std::uintptr_t>::max();
        if (slot_offset > max - check.base || hook.function > max - check.base ||
            !contains(*info, check.base + slot_offset, sizeof(void *), PF_R) ||
            !contains(*info, check.base + hook.function, 1, PF_R | PF_X)) {
            check.error = "mob AI hook address is outside the expected ELF segments";
            return 1;
        }
        std::uintptr_t function;
        std::memcpy(&function, reinterpret_cast<const void *>(check.base + slot_offset), sizeof(function));
        if (function != check.base + hook.function) {
            check.error = "mob AI vtable slot does not match the verified function";
            return 1;
        }
    }
    check.error = nullptr;
    return 1;
}
}  // namespace

bool mob_ai_preflight(std::string &reason)
{
    Check check{reinterpret_cast<std::uintptr_t>(get_executable_base())};
    dl_iterate_phdr(checkExecutable, &check);
    if (check.error) {
        reason = check.error;
        return false;
    }
    return true;
}

bool is_mob_ai_hook(std::string_view name)
{
    return std::ranges::any_of(mob_ai_hooks, [name](const auto &hook) { return hook.name == name; });
}

}  // namespace endstone::runtime

#endif
