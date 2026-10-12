#include "gamelauncher.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

static std::string joinPath(const std::string& first, const std::string& second)
{
    gchar* path = g_build_filename(first.c_str(), second.c_str(), NULL);
    std::string result = path;
    g_free(path);
    return result;
}

static bool fileExists(const std::string& path)
{
    return g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR);
}

static bool folderExists(const std::string& path)
{
    return g_file_test(path.c_str(), G_FILE_TEST_IS_DIR);
}

static std::string toLower(std::string text)
{
    for (char& letter : text) {
        letter = g_ascii_tolower(letter);
    }
    return text;
}

static std::vector<std::string> getSteamFolders()
{
    std::string home = g_get_home_dir();
    std::vector<std::string> possibleFolders = {
        joinPath(home, ".steam/steam"),
        joinPath(home, ".steam/root"),
        joinPath(home, ".local/share/Steam"),
        joinPath(home, ".var/app/com.valvesoftware.Steam/data/Steam"),
        joinPath(home, ".var/app/com.valvesoftware.Steam/.local/share/Steam"),
    };

    std::vector<std::string> steamFolders;
    for (const std::string& folder : possibleFolders) {
        if (folderExists(folder)) {
            steamFolders.push_back(folder);
        }
    }
    return steamFolders;
}

static void lookForProtonIn(const std::string& folder, std::vector<ProtonVersion>& protonList)
{
    GDir* dir = g_dir_open(folder.c_str(), 0, NULL);
    if (dir == NULL) {
        return;
    }

    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        std::string protonFolder = joinPath(folder, name);
        if (!fileExists(joinPath(protonFolder, "proton"))) {
            continue;
        }

        gchar* realFolder = realpath(protonFolder.c_str(), NULL);
        std::string folderToSave = realFolder != NULL ? realFolder : protonFolder;
        free(realFolder);

        bool alreadyAdded = false;
        for (const ProtonVersion& proton : protonList) {
            if (proton.folder == folderToSave) {
                alreadyAdded = true;
            }
        }
        if (!alreadyAdded) {
            protonList.push_back({name, folderToSave});
        }
    }
    g_dir_close(dir);
}

std::vector<ProtonVersion> findProtonVersions()
{
    std::vector<ProtonVersion> protonList;
    for (const std::string& steamFolder : getSteamFolders()) {
        lookForProtonIn(joinPath(steamFolder, "steamapps/common"), protonList);
        lookForProtonIn(joinPath(steamFolder, "compatibilitytools.d"), protonList);
    }
    lookForProtonIn("/usr/share/steam/compatibilitytools.d", protonList);

    std::sort(protonList.begin(), protonList.end(), [](const ProtonVersion& a, const ProtonVersion& b) {
        return a.name < b.name;
    });
    return protonList;
}

std::vector<std::string> makeGameArgs(const LauncherSettings& settings, const std::string& serverAddress)
{
    std::vector<std::string> args;
    args.push_back("-language:INT");

    gchar** extraWords = g_strsplit_set(settings.extraArgs.c_str(), " \t", -1);
    for (int i = 0; extraWords[i] != NULL; i++) {
        std::string word = extraWords[i];
        if (word.empty() || toLower(word).rfind("-language", 0) == 0) {
            continue;
        }
        args.push_back(word);
    }
    g_strfreev(extraWords);

    if (!serverAddress.empty()) {
        args.push_back("-server:" + serverAddress);
    }
    return args;
}

static std::string findWine(const LauncherSettings& settings)
{
    if (!settings.winePath.empty()) {
        if (folderExists(settings.winePath)) {
            std::string insideBin = joinPath(settings.winePath, "bin/wine");
            if (fileExists(insideBin)) {
                return insideBin;
            }
            return "";
        }
        if (fileExists(settings.winePath)) {
            return settings.winePath;
        }
        return "";
    }

    gchar* found = g_find_program_in_path("wine");
    if (found == NULL) {
        return "";
    }
    std::string result = found;
    g_free(found);
    return result;
}

static gchar** removeAppImageFolders(gchar** environment, const std::string& appFolder, const char* name)
{
    const gchar* value = g_environ_getenv(environment, name);
    if (value == NULL) {
        return environment;
    }

    std::string keptFolders;
    gchar** folders = g_strsplit(value, ":", -1);
    for (int i = 0; folders[i] != NULL; i++) {
        std::string folder = folders[i];
        if (folder.empty() || folder.rfind(appFolder, 0) == 0) {
            continue;
        }
        if (!keptFolders.empty()) {
            keptFolders += ":";
        }
        keptFolders += folder;
    }
    g_strfreev(folders);

    if (keptFolders.empty()) {
        return g_environ_unsetenv(environment, name);
    }
    return g_environ_setenv(environment, name, keptFolders.c_str(), TRUE);
}

static gchar** makeCleanEnvironment()
{
    gchar** environment = g_get_environ();

    const gchar* appFolder = g_environ_getenv(environment, "APPDIR");
    if (appFolder != NULL && appFolder[0] != '\0') {
        std::string appFolderText = appFolder;
        const char* folderLists[] = {"PATH", "LD_LIBRARY_PATH", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS", "GSETTINGS_SCHEMA_DIR", "PYTHONPATH"};
        for (const char* name : folderLists) {
            environment = removeAppImageFolders(environment, appFolderText, name);
        }
    }

    const char* badNames[] = {"APPDIR", "APPIMAGE", "ARGV0", "OWD", "LD_PRELOAD", "PYTHONHOME", "GIO_LAUNCHED_DESKTOP_FILE"};
    for (const char* name : badNames) {
        environment = g_environ_unsetenv(environment, name);
    }

    std::vector<std::string> gtkNames;
    for (int i = 0; environment[i] != NULL; i++) {
        std::string line = environment[i];
        std::string name = line.substr(0, line.find('='));
        if (name.rfind("GTK_", 0) == 0 || name.rfind("GDK_", 0) == 0 || name.rfind("GIO_", 0) == 0 || name.rfind("GST_", 0) == 0) {
            gtkNames.push_back(name);
        }
    }
    for (const std::string& name : gtkNames) {
        environment = g_environ_unsetenv(environment, name.c_str());
    }
    return environment;
}

static bool runCommand(std::vector<std::string>& command, const std::string& exePath, gchar** environment, GPid& programId, std::string& errorMessage)
{
    std::vector<gchar*> argv;
    std::cout << "Launching:";
    for (std::string& part : command) {
        argv.push_back(const_cast<gchar*>(part.c_str()));
        std::cout << " " << part;
    }
    argv.push_back(NULL);
    std::cout << std::endl;

    gchar* workFolder = g_path_get_dirname(exePath.c_str());
    GError* spawnError = NULL;
    bool itWorked = g_spawn_async(workFolder, argv.data(), environment, GSpawnFlags(G_SPAWN_DO_NOT_REAP_CHILD), NULL, NULL, &programId, &spawnError);

    if (!itWorked) {
        errorMessage = std::string("Could not start: ") + (spawnError != NULL ? spawnError->message : "unknown error");
        if (spawnError != NULL) {
            g_error_free(spawnError);
        }
    }

    g_free(workFolder);
    g_strfreev(environment);
    return itWorked;
}

// Runs the program inside a shell that writes the program's pid to pidFile, so the launcher can stop it later.
// The shell also stops the program when the terminal is closed, and waits for Enter at the end so errors can be read.
static const char* terminalScript =
    "cd \"$(dirname \"$1\")\" || exit 1\n"
    "trap 'kill $child 2>/dev/null' INT TERM HUP\n"
    "\"$1\" & child=$!\n"
    "echo $child > \"$2\"\n"
    "wait $child; code=$?\n"
    "while kill -0 $child 2>/dev/null; do wait $child; code=$?; done\n"
    "rm -f \"$2\"\n"
    "echo; echo \"--- The server stopped (exit code $code) ---\"; echo 'Press Enter to close'; read answer\n";

bool startLinuxProgramInTerminal(const std::string& exePath, const std::string& title, const std::string& pidFile, std::string& errorMessage)
{
    if (exePath.empty() || !fileExists(exePath)) {
        errorMessage = "Executable not found: " + exePath;
        return false;
    }
    if (g_str_has_suffix(toLower(exePath).c_str(), ".exe")) {
        errorMessage = "The local server must be the Linux version (eridian-server), not the Windows .exe. Select it again in Options";
        return false;
    }
    if (!g_file_test(exePath.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
        errorMessage = "The server file is not executable. Run: chmod +x " + exePath;
        return false;
    }

    // Each terminal with the arguments it needs before the command to run
    const std::vector<std::vector<std::string>> terminals = {
        {"xdg-terminal-exec"},
        {"ptyxis", "-T", title, "--"},
        {"gnome-terminal", "--title=" + title, "--"},
        {"konsole", "-p", "tabtitle=" + title, "-e"},
        {"xfce4-terminal", "--disable-server", "--title=" + title, "-x"},
        {"mate-terminal", "--disable-factory", "--title=" + title, "-x"},
        {"kgx", "--title=" + title, "--"},
        {"kitty", "--title", title},
        {"alacritty", "--title", title, "-e"},
        {"foot", "--title", title},
        {"wezterm", "start", "--"},
        {"x-terminal-emulator", "-e"},
        {"xterm", "-T", title, "-e"},
    };

    for (const std::vector<std::string>& terminal : terminals) {
        gchar* terminalPath = g_find_program_in_path(terminal[0].c_str());
        if (terminalPath == NULL) {
            continue;
        }
        g_free(terminalPath);

        std::vector<std::string> command = terminal;
        command.insert(command.end(), {"sh", "-c", terminalScript, "sh", exePath, pidFile});

        std::vector<gchar*> argv;
        std::cout << "Launching in " << terminal[0] << ": " << exePath << std::endl;
        for (std::string& part : command) {
            argv.push_back(const_cast<gchar*>(part.c_str()));
        }
        argv.push_back(NULL);

        gchar** environment = makeCleanEnvironment();
        GError* spawnError = NULL;
        bool itWorked = g_spawn_async(NULL, argv.data(), environment, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &spawnError);
        g_strfreev(environment);
        if (itWorked) {
            return true;
        }
        std::cout << "Could not start " << terminal[0] << ": " << spawnError->message << std::endl;
        g_error_free(spawnError);
    }

    errorMessage = "No terminal was found to show the server log. Install one, for example xterm";
    return false;
}

bool startWindowsProgram(const LauncherSettings& settings, const std::string& exePath, const std::vector<std::string>& args, GPid& programId, std::string& errorMessage)
{
    if (exePath.empty() || !fileExists(exePath)) {
        errorMessage = "Executable not found: " + exePath;
        return false;
    }

    std::string prefix = settings.prefixPath.empty() ? getDefaultPrefix() : settings.prefixPath;
    if (g_mkdir_with_parents(prefix.c_str(), 0755) != 0) {
        errorMessage = "Could not create the prefix folder: " + prefix;
        return false;
    }

    gchar** environment = makeCleanEnvironment();
    std::vector<std::string> command;

    if (settings.runner == "proton") {
        std::string protonScript = joinPath(settings.protonPath, "proton");
        if (settings.protonPath.empty() || !fileExists(protonScript)) {
            g_strfreev(environment);
            errorMessage = "No valid Proton version selected in Options";
            return false;
        }

        std::vector<std::string> steamFolders = getSteamFolders();
        std::string steamFolder = steamFolders.empty() ? joinPath(g_get_home_dir(), ".steam/steam") : steamFolders[0];

        environment = g_environ_setenv(environment, "STEAM_COMPAT_DATA_PATH", prefix.c_str(), TRUE);
        environment = g_environ_setenv(environment, "STEAM_COMPAT_CLIENT_INSTALL_PATH", steamFolder.c_str(), TRUE);
        environment = g_environ_setenv(environment, "SteamAppId", "0", TRUE);
        environment = g_environ_setenv(environment, "SteamGameId", "0", TRUE);

        command.push_back(protonScript);
        command.push_back("run");
    } else {
        std::string wine = findWine(settings);
        if (wine.empty()) {
            g_strfreev(environment);
            errorMessage = "Wine was not found. Install it or set its path in Options";
            return false;
        }

        environment = g_environ_setenv(environment, "WINEPREFIX", prefix.c_str(), TRUE);
        environment = g_environ_setenv(environment, "WINEDEBUG", "-all", TRUE);

        command.push_back(wine);
    }

    command.push_back(exePath);
    for (const std::string& arg : args) {
        command.push_back(arg);
    }
    return runCommand(command, exePath, environment, programId, errorMessage);
}
