#pragma once

#include "settings.h"

#include <glib.h>
#include <string>
#include <vector>

struct ProtonVersion {
    std::string name;
    std::string folder;
};

std::vector<ProtonVersion> findProtonVersions();
std::vector<std::string> makeGameArgs(const LauncherSettings& settings, const std::string& serverAddress);
bool startWindowsProgram(const LauncherSettings& settings, const std::string& exePath, const std::vector<std::string>& args, GPid& programId, std::string& errorMessage);
// Opens the system terminal running the program. The program's pid is written to pidFile while it runs
bool startLinuxProgramInTerminal(const std::string& exePath, const std::string& title, const std::string& pidFile, std::string& errorMessage);
