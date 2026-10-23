#pragma once

#include <filesystem>

#include <toml++/toml.h>

namespace endstone::core {

toml::table loadConfig(const std::filesystem::path &path);

}  // namespace endstone::core
