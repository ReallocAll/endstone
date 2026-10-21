#include "endstone/runtime/entity_ai_preflight.h"

#ifdef __linux__

#include <link.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#include "bedrock/symbol.h"

namespace endstone::runtime {
namespace {
constexpr std::array<unsigned char, 20> build_id = {
    0xc0, 0xff, 0x36, 0xe2, 0xea, 0xcf, 0xc1, 0x91, 0xf0, 0x1e,
    0x18, 0x92, 0x37, 0xdf, 0x71, 0xf1, 0x39, 0xb5, 0xd0, 0x4a,
};

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

bool matchesBuildId(const dl_phdr_info &info)
{
    for (std::size_t i = 0; i < info.dlpi_phnum; ++i) {
        const auto &ph = info.dlpi_phdr[i];
        if (ph.p_type != PT_NOTE || ph.p_vaddr > std::numeric_limits<std::uintptr_t>::max() - info.dlpi_addr) {
            continue;
        }
        const auto address = info.dlpi_addr + ph.p_vaddr;
        if (!contains(info, address, ph.p_memsz, PF_R)) {
            continue;
        }
        const auto *data = reinterpret_cast<const unsigned char *>(address);
        std::size_t remaining = ph.p_memsz;
        while (remaining >= sizeof(ElfW(Nhdr))) {
            ElfW(Nhdr) note;
            std::memcpy(&note, data, sizeof(note));
            data += sizeof(note);
            remaining -= sizeof(note);
            const auto name_size = (static_cast<std::uint64_t>(note.n_namesz) + 3) & ~std::uint64_t{3};
            const auto desc_size = (static_cast<std::uint64_t>(note.n_descsz) + 3) & ~std::uint64_t{3};
            if (name_size > remaining || desc_size > remaining - name_size) {
                return false;
            }
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && std::memcmp(data, "GNU", 4) == 0) {
                return note.n_descsz == build_id.size() &&
                       std::memcmp(data + name_size, build_id.data(), build_id.size()) == 0;
            }
            data += name_size + desc_size;
            remaining -= name_size + desc_size;
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
    if (info->dlpi_addr != check.base || !matchesBuildId(*info)) {
        check.error = "executable GNU build ID does not match the verified binary";
        return 1;
    }
    for (const auto &hook : entity_ai_hooks) {
        const auto slot_offset = hook.layout.vtable + hook.slot * sizeof(void *);
        const auto max = std::numeric_limits<std::uintptr_t>::max();
        if (slot_offset > max - check.base || hook.function > max - check.base ||
            !contains(*info, check.base + slot_offset, sizeof(void *), PF_R) ||
            !contains(*info, check.base + hook.function, 1, PF_R | PF_X)) {
            check.error = "entity AI hook address is outside the expected ELF segments";
            return 1;
        }
        std::uintptr_t function;
        std::memcpy(&function, reinterpret_cast<const void *>(check.base + slot_offset), sizeof(function));
        if (function != check.base + hook.function) {
            check.error = "entity AI vtable slot does not match the verified function";
            return 1;
        }
    }
    check.error = nullptr;
    return 1;
}
}  // namespace

bool entity_ai_preflight(std::string &reason)
{
    Check check{reinterpret_cast<std::uintptr_t>(get_executable_base())};
    dl_iterate_phdr(checkExecutable, &check);
    if (check.error) {
        reason = check.error;
        return false;
    }
    return true;
}

bool is_entity_ai_hook(std::string_view name)
{
    return std::ranges::any_of(entity_ai_hooks, [name](const auto &hook) { return hook.name == name; });
}

}  // namespace endstone::runtime

#endif
