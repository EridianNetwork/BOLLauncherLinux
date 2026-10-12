#pragma once

#include <string>

struct LauncherSettings {
    std::string gameExePath;
    std::string serverExePath;
    std::string runner = "wine";
    std::string protonPath;
    std::string winePath;
    std::string prefixPath;
    std::string extraArgs;
    std::string theme = "system";
};

LauncherSettings loadSettings();
bool saveSettings(const LauncherSettings& settings);
std::string getDefaultPrefix();
