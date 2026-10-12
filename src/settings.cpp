#include "settings.h"

#include <glib.h>

static std::string getSettingsFolder()
{
    gchar* folder = g_build_filename(g_get_user_config_dir(), "bol-launcher", NULL);
    std::string result = folder;
    g_free(folder);
    return result;
}

static std::string getSettingsFile()
{
    gchar* file = g_build_filename(getSettingsFolder().c_str(), "settings.ini", NULL);
    std::string result = file;
    g_free(file);
    return result;
}

std::string getDefaultPrefix()
{
    gchar* folder = g_build_filename(g_get_user_data_dir(), "bol-launcher", "prefix", NULL);
    std::string result = folder;
    g_free(folder);
    return result;
}

static void readText(GKeyFile* file, const char* name, std::string& value)
{
    gchar* text = g_key_file_get_string(file, "launcher", name, NULL);
    if (text != NULL) {
        value = text;
        g_free(text);
    }
}

LauncherSettings loadSettings()
{
    LauncherSettings settings;
    GKeyFile* file = g_key_file_new();

    if (g_key_file_load_from_file(file, getSettingsFile().c_str(), G_KEY_FILE_NONE, NULL)) {
        readText(file, "game_exe", settings.gameExePath);
        readText(file, "server_exe", settings.serverExePath);
        readText(file, "runner", settings.runner);
        readText(file, "proton", settings.protonPath);
        readText(file, "wine", settings.winePath);
        readText(file, "prefix", settings.prefixPath);
        readText(file, "extra_args", settings.extraArgs);
        readText(file, "theme", settings.theme);
    }

    g_key_file_free(file);
    return settings;
}

bool saveSettings(const LauncherSettings& settings)
{
    g_mkdir_with_parents(getSettingsFolder().c_str(), 0755);

    GKeyFile* file = g_key_file_new();
    g_key_file_set_string(file, "launcher", "game_exe", settings.gameExePath.c_str());
    g_key_file_set_string(file, "launcher", "server_exe", settings.serverExePath.c_str());
    g_key_file_set_string(file, "launcher", "runner", settings.runner.c_str());
    g_key_file_set_string(file, "launcher", "proton", settings.protonPath.c_str());
    g_key_file_set_string(file, "launcher", "wine", settings.winePath.c_str());
    g_key_file_set_string(file, "launcher", "prefix", settings.prefixPath.c_str());
    g_key_file_set_string(file, "launcher", "extra_args", settings.extraArgs.c_str());
    g_key_file_set_string(file, "launcher", "theme", settings.theme.c_str());

    bool itWorked = g_key_file_save_to_file(file, getSettingsFile().c_str(), NULL);
    g_key_file_free(file);
    return itWorked;
}
