#include "runtime_paths.h"
#include <cstdlib>

namespace {
#ifndef _WIN32
std::filesystem::path environmentPath(const char *name) {
    const char *value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path();
}
#endif
#ifdef _WIN32
std::filesystem::path windowsEnvironmentPath(const wchar_t *name) {
    const wchar_t *value = _wgetenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path();
}
#endif
}

std::filesystem::path userDataDirectoryForPlatform(
        RuntimePlatform platform,
        const std::filesystem::path &home,
        const std::filesystem::path &localAppData,
        const std::filesystem::path &xdgDataHome) {
    if (platform == RuntimePlatform::WINDOWS) {
        const auto base = localAppData.empty() ? home / "AppData" / "Local" : localAppData;
        return base / "CityUStudyPlan";
    }
    if (platform == RuntimePlatform::MACOS)
        return home / "Library" / "Application Support" / "CityUStudyPlan";
    const auto base = xdgDataHome.empty() ? home / ".local" / "share" : xdgDataHome;
    return base / "cityu-study-plan";
}

std::filesystem::path userDataDirectory() {
#ifdef _WIN32
    return userDataDirectoryForPlatform(RuntimePlatform::WINDOWS,
        windowsEnvironmentPath(L"USERPROFILE"), windowsEnvironmentPath(L"LOCALAPPDATA"), {});
#elif defined(__APPLE__)
    return userDataDirectoryForPlatform(RuntimePlatform::MACOS,
        environmentPath("HOME"), {}, {});
#else
    return userDataDirectoryForPlatform(RuntimePlatform::OTHER,
        environmentPath("HOME"), {}, environmentPath("XDG_DATA_HOME"));
#endif
}
