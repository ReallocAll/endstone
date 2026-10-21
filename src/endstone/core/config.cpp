#include "endstone/core/config.h"

#include <array>
#include <cerrno>
#include <memory>
#include <string>
#include <system_error>

namespace endstone::core {

toml::table config_detail::loadConfig(std::FILE *file, const std::filesystem::path &path)
{
    std::array<char, 4096> buffer;
    std::string content;
    for (;;) {
        const auto count = std::fread(buffer.data(), 1, buffer.size(), file);
        if (std::ferror(file)) {
            throw std::filesystem::filesystem_error("Cannot read config file", path,
                                                    std::error_code(errno, std::generic_category()));
        }
        content.append(buffer.data(), count);
        if (count < buffer.size()) {
            break;
        }
    }
    const auto source_path = path.u8string();
    return toml::parse(content, std::string(source_path.begin(), source_path.end()));
}

toml::table loadConfig(const std::filesystem::path &path)
{
    const auto status = std::filesystem::status(path);
    if (!std::filesystem::exists(status)) {
        return {};
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw std::filesystem::filesystem_error("Expected regular config file", path,
                                                std::make_error_code(std::errc::invalid_argument));
    }
#ifdef _WIN32
    auto *handle = ::_wfopen(path.c_str(), L"rb");
#else
    auto *handle = std::fopen(path.c_str(), "rb");
#endif
    if (!handle) {
        throw std::filesystem::filesystem_error("Cannot open config file", path,
                                                std::error_code(errno, std::generic_category()));
    }
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(handle, &std::fclose);
    return config_detail::loadConfig(file.get(), path);
}

}  // namespace endstone::core
