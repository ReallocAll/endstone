#pragma once

#include <cstdio>
#include <filesystem>

#include <toml++/toml.h>

namespace endstone::core {

toml::table loadConfig(const std::filesystem::path &path);

namespace config_detail {

// Borrows the file until the complete configuration has been read and parsed.
toml::table loadConfig(std::FILE *file, const std::filesystem::path &path);

}  // namespace config_detail
}  // namespace endstone::core
