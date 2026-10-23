#include "endstone/runtime/mob_ai_preflight.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <link.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#include "bedrock/symbol.h"

namespace endstone::runtime {
namespace {
#ifdef _WIN32
bool containsImage(std::uintptr_t base, std::uintptr_t address, std::size_t size, bool executable)
{
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || info.Type != MEM_IMAGE ||
        info.AllocationBase != reinterpret_cast<const void *>(base) || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (address < start || address - start > info.RegionSize || size > info.RegionSize - (address - start)) {
        return false;
    }
    const auto protection = info.Protect & 0xFF;
    if (executable) {
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
               protection == PAGE_EXECUTE_WRITECOPY;
    }
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

bool pathIdentity(std::uintptr_t base)
{
    constexpr auto function = get_symbol("endstone_mob_ai_find_path");
    constexpr std::size_t call = 0x7CE52D;
    constexpr std::size_t target = 0x2F688B0;
    constexpr std::array<unsigned char, 27> prefix = {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
                                                      0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0xB8, 0x03, 0x00,
                                                      0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00};
    constexpr std::array<unsigned char, 19> target_prefix = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57,
                                                             0x55, 0x53, 0x48, 0x81, 0xEC, 0x78, 0x01, 0x00, 0x00};
    constexpr std::array<unsigned char, 5> call_bytes = {0xE8, 0x7E, 0xA3, 0x79, 0x02};
    const auto max = std::numeric_limits<std::uintptr_t>::max();
    auto readable_code = [base](std::size_t offset, std::size_t size) {
        return offset <= std::numeric_limits<std::uintptr_t>::max() - base &&
               containsImage(base, base + offset, size, false) && containsImage(base, base + offset, size, true);
    };
    if (!readable_code(function, prefix.size()) || !readable_code(call, call_bytes.size()) ||
        !readable_code(target, target_prefix.size())) {
        return false;
    }
    for (const auto vtable : {0xA70BB00, 0xA70EEE0}) {
        const auto slot = static_cast<std::size_t>(vtable) + 128 * sizeof(void *);
        if (slot > max - base || !containsImage(base, base + slot, sizeof(void *), false)) {
            return false;
        }
        std::uintptr_t value;
        std::memcpy(&value, reinterpret_cast<const void *>(base + slot), sizeof(value));
        if (value != base + function) {
            return false;
        }
    }
    if (std::memcmp(reinterpret_cast<const void *>(base + function), prefix.data(), prefix.size()) != 0 ||
        std::memcmp(reinterpret_cast<const void *>(base + target), target_prefix.data(), target_prefix.size()) != 0 ||
        std::memcmp(reinterpret_cast<const void *>(base + call), call_bytes.data(), call_bytes.size()) != 0) {
        return false;
    }
    std::int32_t displacement;
    std::memcpy(&displacement, reinterpret_cast<const void *>(base + call + 1), sizeof(displacement));
    return static_cast<std::int64_t>(call) + 5 + displacement == static_cast<std::int64_t>(target);
}
#else
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
    const auto max = std::numeric_limits<std::uintptr_t>::max();
    for (const auto &hook : mob_ai_hooks) {
        if (hook.function > max - check.base || !contains(*info, check.base + hook.function, 1, PF_R | PF_X)) {
            check.error = "mob AI hook address is outside the expected ELF segments";
            return 1;
        }
        if (!hook.layout) {
            continue;
        }
        if (hook.slot > (max - hook.layout->vtable) / sizeof(void *)) {
            check.error = "mob AI vtable slot address overflows";
            return 1;
        }
        const auto slot_offset = hook.layout->vtable + hook.slot * sizeof(void *);
        if (slot_offset > max - check.base || !contains(*info, check.base + slot_offset, sizeof(void *), PF_R)) {
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
    constexpr auto function = get_symbol("endstone_mob_ai_find_path");
    constexpr std::size_t call = 0xC899BE5;
    constexpr std::array<unsigned char, 33> prefix = {0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xEC, 0x50, 0x01, 0x00,
                                                      0x00, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x0C, 0xF3, 0x0F, 0x2A, 0xC9,
                                                      0x49, 0x89, 0xF6, 0xF3, 0x41, 0x0F, 0x2A, 0xD0, 0x48, 0x89, 0xFB};
    constexpr std::array<unsigned char, 5> call_bytes = {0xE8, 0x96, 0xA2, 0x8D, 0x00};
    if (function > max - check.base || call > max - check.base ||
        !contains(*info, check.base + function, prefix.size(), PF_R | PF_X) ||
        !contains(*info, check.base + call, call_bytes.size(), PF_R | PF_X) ||
        std::memcmp(reinterpret_cast<const void *>(check.base + function), prefix.data(), prefix.size()) != 0 ||
        std::memcmp(reinterpret_cast<const void *>(check.base + call), call_bytes.data(), call_bytes.size()) != 0) {
        check.error = "mob AI path observer does not match the verified native boundary";
        return 1;
    }
    std::int32_t displacement;
    std::memcpy(&displacement, reinterpret_cast<const void *>(check.base + call + 1), sizeof(displacement));
    if (static_cast<std::int64_t>(call) + 5 + displacement != static_cast<std::int64_t>(function)) {
        check.error = "mob AI path observer call does not match the verified target";
        return 1;
    }
    check.error = nullptr;
    return 1;
}
#endif
}  // namespace

bool mob_ai_preflight(std::string &reason)
{
#ifdef _WIN32
    const auto base = reinterpret_cast<std::uintptr_t>(get_executable_base());
    const auto max = std::numeric_limits<std::uintptr_t>::max();
    for (const auto &hook : mob_ai_hooks) {
        if (hook.function > max - base || !containsImage(base, base + hook.function, 1, true)) {
            reason = "mob AI hook address is outside the expected executable image regions";
            return false;
        }
        if (!hook.layout) {
            continue;
        }
        if (hook.slot > (max - hook.layout->vtable) / sizeof(void *)) {
            reason = "mob AI vtable slot address overflows";
            return false;
        }
        const auto slot_offset = hook.layout->vtable + hook.slot * sizeof(void *);
        if (slot_offset > max - base || !containsImage(base, base + slot_offset, sizeof(void *), false)) {
            reason = "mob AI hook address is outside the expected executable image regions";
            return false;
        }
        std::uintptr_t function;
        std::memcpy(&function, reinterpret_cast<const void *>(base + slot_offset), sizeof(function));
        if (function != base + hook.function) {
            reason = "mob AI vtable slot does not match the verified function";
            return false;
        }
    }
    if (!pathIdentity(base)) {
        reason = "mob AI path observer does not match the verified native boundary";
        return false;
    }
#else
    Check check{reinterpret_cast<std::uintptr_t>(get_executable_base())};
    dl_iterate_phdr(checkExecutable, &check);
    if (check.error) {
        reason = check.error;
        return false;
    }
#endif
    return true;
}

bool is_mob_ai_hook(std::string_view name)
{
    return std::ranges::any_of(mob_ai_hooks, [name](const auto &hook) { return hook.name == name; });
}

}  // namespace endstone::runtime
