#pragma once

#include <filesystem>

enum class RuntimePlatform {
    WINDOWS,
    MACOS,
    OTHER
};

std::filesystem::path userDataDirectoryForPlatform(
    RuntimePlatform platform,
    const std::filesystem::path &home,
    const std::filesystem::path &localAppData,
    const std::filesystem::path &xdgDataHome);
std::filesystem::path userDataDirectory();
