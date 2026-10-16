#include "gamelauncher.h"
#include "serverlist.h"
#include "settings.h"
#include "updater.h"

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <csignal>
#include <thread>

struct LauncherWindow {
    GtkWidget* window;
    GtkListStore* serverStore;
    GtkTreeView* serverTable;
    GtkLabel* statusLabel;
    GtkButton* refreshButton;
    GtkButton* connectButton;
    GtkButton* offlineButton;
    GtkFileChooser* gameExePicker;
    GtkFileChooser* serverExePicker;
    GtkComboBoxText* runnerCombo;
    GtkComboBoxText* protonCombo;
    GtkEntry* wineEntry;
    GtkFileChooser* prefixPicker;
    GtkLabel* prefixHint;
    GtkEntry* argsEntry;
    GtkComboBoxText* themeCombo;
    GtkEntry* updateUrlEntry;
    GtkButton* updateButton;
    GtkProgressBar* updateProgress;
    GtkLabel* updateStatus;
    bool isUpdating = false;
    LauncherSettings settings;
    bool isLoadingServers = false;
    bool gameIsRunning = false;
    bool startedServer = false;
};

std::string forcedThemeName;
bool systemWantsDark = false;

static void checkForcedTheme()
{
    const gchar* forcedTheme = g_getenv("GTK_THEME");
    if (forcedTheme == NULL || forcedTheme[0] == '\0') {
        return;
    }

    std::string themeText = forcedTheme;
    size_t colon = themeText.find(':');
    forcedThemeName = themeText.substr(0, colon);
    if (colon != std::string::npos) {
        systemWantsDark = themeText.substr(colon + 1) == "dark";
    }
    g_unsetenv("GTK_THEME");
}

static void applyTheme(const std::string& theme)
{
    GtkSettings* gtkSettings = gtk_settings_get_default();

    if (!forcedThemeName.empty()) {
        g_object_set(gtkSettings, "gtk-theme-name", forcedThemeName.c_str(), NULL);
    }

    if (theme == "dark") {
        g_object_set(gtkSettings, "gtk-application-prefer-dark-theme", TRUE, NULL);
    } else if (theme == "light") {
        g_object_set(gtkSettings, "gtk-application-prefer-dark-theme", FALSE, NULL);
    } else if (!forcedThemeName.empty()) {
        g_object_set(gtkSettings, "gtk-application-prefer-dark-theme", systemWantsDark, NULL);
    } else {
        gtk_settings_reset_property(gtkSettings, "gtk-application-prefer-dark-theme");
    }
}

struct ServerSearchResult {
    LauncherWindow* launcher;
    bool itWorked;
    std::vector<GameServer> serverList;
    std::string errorMessage;
};

static void setStatus(LauncherWindow* launcher, const std::string& text)
{
    gtk_label_set_text(launcher->statusLabel, text.c_str());
}

static void showMessage(LauncherWindow* launcher, const std::string& text)
{
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(launcher->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", text.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static std::string niceUptime(int seconds)
{
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    if (hours > 24) {
        return std::to_string(hours / 24) + "d " + std::to_string(hours % 24) + "h";
    }
    if (hours > 0) {
        return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    }
    return std::to_string(minutes) + "m";
}

static std::string getChosenFile(GtkFileChooser* picker)
{
    gchar* file = gtk_file_chooser_get_filename(picker);
    if (file == NULL) {
        return "";
    }
    std::string result = file;
    g_free(file);
    return result;
}

static void setChosenFile(GtkFileChooser* picker, const std::string& path)
{
    if (!path.empty()) {
        gtk_file_chooser_set_filename(picker, path.c_str());
    }
}

static gboolean showServers(gpointer data)
{
    ServerSearchResult* result = static_cast<ServerSearchResult*>(data);
    LauncherWindow* launcher = result->launcher;

    gtk_list_store_clear(launcher->serverStore);

    if (result->itWorked) {
        for (const GameServer& server : result->serverList) {
            std::string address = server.ip + ":" + std::to_string(server.port);
            std::string players = std::to_string(server.players) + " / " + std::to_string(server.maxPlayers);
            std::string uptime = niceUptime(server.uptimeSeconds);

            GtkTreeIter row;
            gtk_list_store_append(launcher->serverStore, &row);
            gtk_list_store_set(launcher->serverStore, &row, 0, server.name.c_str(), 1, address.c_str(), 2, players.c_str(), 3, uptime.c_str(), -1);
        }

        if (result->serverList.empty()) {
            setStatus(launcher, "No servers online right now");
        } else if (result->serverList.size() == 1) {
            setStatus(launcher, "1 server found");
        } else {
            setStatus(launcher, std::to_string(result->serverList.size()) + " servers found");
        }
    } else {
        setStatus(launcher, "Error: " + result->errorMessage);
    }

    launcher->isLoadingServers = false;
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->refreshButton), TRUE);
    delete result;
    return G_SOURCE_REMOVE;
}

static void loadServers(LauncherWindow* launcher)
{
    if (launcher->isLoadingServers) {
        return;
    }
    launcher->isLoadingServers = true;
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->refreshButton), FALSE);
    setStatus(launcher, "Looking for servers...");

    std::thread searchThread([launcher]() {
        ServerSearchResult* result = new ServerSearchResult();
        result->launcher = launcher;
        result->itWorked = getServersFromMaster(result->serverList, result->errorMessage);
        g_idle_add(showServers, result);
    });
    searchThread.detach();
}

static void gameClosed(GPid programId, gint status, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    g_spawn_close_pid(programId);
    launcher->gameIsRunning = false;
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->connectButton), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->offlineButton), TRUE);
    setStatus(launcher, "The game was closed");
}

static std::string serverPidFile()
{
    gchar* path = g_build_filename(g_get_user_runtime_dir(), "bol-launcher-server.pid", NULL);
    std::string result = path;
    g_free(path);
    return result;
}

// The pid of the local server this launcher started, or 0 if it is not running anymore
static GPid runningServerId(LauncherWindow* launcher)
{
    if (!launcher->startedServer) {
        return 0;
    }
    gchar* text = NULL;
    if (!g_file_get_contents(serverPidFile().c_str(), &text, NULL, NULL)) {
        return 0;
    }
    GPid serverId = atoi(text);
    g_free(text);
    if (serverId <= 0 || kill(serverId, 0) != 0) {
        return 0;
    }
    return serverId;
}

// The server runs in the system terminal, closing that terminal stops it
static bool startLocalServer(LauncherWindow* launcher, std::string& errorMessage)
{
    std::string pidFile = serverPidFile();
    g_remove(pidFile.c_str());
    if (!startLinuxProgramInTerminal(launcher->settings.serverExePath, "Local server log", pidFile, errorMessage)) {
        return false;
    }
    launcher->startedServer = true;
    return true;
}

static bool startGame(LauncherWindow* launcher, const std::string& serverAddress, const std::string& serverName)
{
    std::vector<std::string> args = makeGameArgs(launcher->settings, serverAddress);
    GPid gameId;
    std::string errorMessage;

    if (!startWindowsProgram(launcher->settings, launcher->settings.gameExePath, args, gameId, errorMessage)) {
        showMessage(launcher, errorMessage);
        setStatus(launcher, "Could not start the game");
        gtk_widget_set_sensitive(GTK_WIDGET(launcher->connectButton), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(launcher->offlineButton), TRUE);
        return false;
    }

    launcher->gameIsRunning = true;
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->connectButton), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->offlineButton), FALSE);
    g_child_watch_add(gameId, gameClosed, launcher);

    if (serverAddress.empty()) {
        setStatus(launcher, "Playing offline");
    } else {
        setStatus(launcher, "Playing on " + serverName);
    }
    return true;
}

static bool checkGameExe(LauncherWindow* launcher)
{
    if (launcher->settings.gameExePath.empty()) {
        showMessage(launcher, "First select BOL.exe in Options");
        return false;
    }
    return true;
}

static void readOptionsFromScreen(LauncherWindow* launcher)
{
    LauncherSettings& settings = launcher->settings;

    settings.gameExePath = getChosenFile(launcher->gameExePicker);
    settings.serverExePath = getChosenFile(launcher->serverExePicker);
    settings.prefixPath = getChosenFile(launcher->prefixPicker);

    const gchar* runner = gtk_combo_box_get_active_id(GTK_COMBO_BOX(launcher->runnerCombo));
    settings.runner = runner != NULL ? runner : "wine";

    const gchar* proton = gtk_combo_box_get_active_id(GTK_COMBO_BOX(launcher->protonCombo));
    settings.protonPath = proton != NULL ? proton : "";

    const gchar* theme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(launcher->themeCombo));
    settings.theme = theme != NULL ? theme : "system";

    settings.winePath = gtk_entry_get_text(launcher->wineEntry);
    settings.extraArgs = gtk_entry_get_text(launcher->argsEntry);
    settings.updateUrl = gtk_entry_get_text(launcher->updateUrlEntry);
}

static void onOptionChanged(GtkWidget* widget, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    readOptionsFromScreen(launcher);
    saveSettings(launcher->settings);
}

struct UpdateStep {
    LauncherWindow* launcher;
    std::string text;
    double fraction;
    bool finished;
    bool itWorked;
};

static gboolean showUpdateStep(gpointer data)
{
    UpdateStep* step = static_cast<UpdateStep*>(data);
    LauncherWindow* launcher = step->launcher;
    gtk_label_set_text(launcher->updateStatus, step->text.c_str());
    gtk_progress_bar_set_fraction(launcher->updateProgress, step->fraction);
    if (step->finished) {
        launcher->isUpdating = false;
        gtk_widget_set_sensitive(GTK_WIDGET(launcher->updateButton), TRUE);
        setStatus(launcher, step->itWorked ? "Update finished" : "Update failed");
    }
    delete step;
    return G_SOURCE_REMOVE;
}

static void onUpdateClicked(GtkButton* button, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    if (launcher->isUpdating) {
        return;
    }
    readOptionsFromScreen(launcher);
    saveSettings(launcher->settings);

    std::string url = launcher->settings.updateUrl;
    std::string gameFolder;
    if (!launcher->settings.gameExePath.empty()) {
        gchar* folder = g_path_get_dirname(launcher->settings.gameExePath.c_str());
        gameFolder = folder;
        g_free(folder);
    }

    launcher->isUpdating = true;
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->updateButton), FALSE);

    std::thread updateThread([launcher, url, gameFolder]() {
        std::string message;
        bool itWorked = updateGameFiles(url, gameFolder, [launcher](const std::string& text, double fraction) {
            g_idle_add(showUpdateStep, new UpdateStep{launcher, text, fraction, false, false});
        }, message);
        g_idle_add(showUpdateStep, new UpdateStep{launcher, message, itWorked ? 1.0 : 0.0, true, itWorked});
    });
    updateThread.detach();
}

static void onConnectClicked(GtkButton* button, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    readOptionsFromScreen(launcher);
    if (launcher->gameIsRunning || !checkGameExe(launcher)) {
        return;
    }

    GtkTreeSelection* selection = gtk_tree_view_get_selection(launcher->serverTable);
    GtkTreeModel* model;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(selection, &model, &row)) {
        showMessage(launcher, "Select a server from the list");
        return;
    }

    gchar* name;
    gchar* address;
    gtk_tree_model_get(model, &row, 0, &name, 1, &address, -1);
    std::string serverName = name;
    std::string serverAddress = address;
    g_free(name);
    g_free(address);

    startGame(launcher, serverAddress, serverName);
}

static void onServerDoubleClicked(GtkTreeView* table, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data)
{
    onConnectClicked(NULL, data);
}

static gboolean startOfflineGame(gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    startGame(launcher, "", "");
    return G_SOURCE_REMOVE;
}

static void onOfflineClicked(GtkButton* button, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    readOptionsFromScreen(launcher);
    if (launcher->gameIsRunning || !checkGameExe(launcher)) {
        return;
    }
    if (launcher->settings.serverExePath.empty()) {
        showMessage(launcher, "First select the Linux local server (eridian-server) in Options");
        return;
    }

    // If the local server is already running, reuse it and start the game right away
    if (runningServerId(launcher) != 0) {
        startGame(launcher, "", "");
        return;
    }

    std::string errorMessage;
    if (!startLocalServer(launcher, errorMessage)) {
        showMessage(launcher, errorMessage);
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(launcher->connectButton), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->offlineButton), FALSE);
    setStatus(launcher, "Starting the local server, please wait...");
    g_timeout_add_seconds(5, startOfflineGame, launcher);
}

static void onRefreshClicked(GtkButton* button, gpointer data)
{
    loadServers(static_cast<LauncherWindow*>(data));
}

static void updateProtonCombo(LauncherWindow* launcher)
{
    const gchar* runner = gtk_combo_box_get_active_id(GTK_COMBO_BOX(launcher->runnerCombo));
    bool usingProton = runner != NULL && std::string(runner) == "proton";
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->protonCombo), usingProton);
    gtk_widget_set_sensitive(GTK_WIDGET(launcher->wineEntry), !usingProton);
}

static void onRunnerChanged(GtkComboBox* combo, gpointer data)
{
    updateProtonCombo(static_cast<LauncherWindow*>(data));
}

static std::string getChosenTheme(LauncherWindow* launcher)
{
    const gchar* theme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(launcher->themeCombo));
    return theme != NULL ? theme : "system";
}

static void onThemeChanged(GtkComboBox* combo, gpointer data)
{
    applyTheme(getChosenTheme(static_cast<LauncherWindow*>(data)));
}

static void onClearPrefixClicked(GtkButton* button, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    gtk_file_chooser_unselect_all(launcher->prefixPicker);
    onOptionChanged(NULL, launcher);
}
yastatic void onArgsInfoClicked(GtkButton* button, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);

    std::string helpText =
        "<b>-terminal</b>\nOpens the game's debug console window next to the game.\n\n"
        "<b>-logfile</b>\nMakes the game write a log file.\n    \n"
        "<b>-server:x.x.x.x</b>\nConnects to the server at that address, for example -server:127.0.0.1:5055. "
        "Connect already adds this one for you.\n\n"
        "<b>-language:INT</b>\nGame language, the launcher always adds this one for you.";

    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(launcher->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Extra arguments");
    gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(dialog), "%s", helpText.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void fillOptions(LauncherWindow* launcher)
{
    LauncherSettings& settings = launcher->settings;

    setChosenFile(launcher->gameExePicker, settings.gameExePath);
    setChosenFile(launcher->serverExePicker, settings.serverExePath);
    setChosenFile(launcher->prefixPicker, settings.prefixPath);

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(launcher->runnerCombo), settings.runner.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(launcher->runnerCombo)) < 0) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(launcher->runnerCombo), "wine");
    }

    std::vector<ProtonVersion> protonList = findProtonVersions();
    bool savedProtonFound = false;
    for (const ProtonVersion& proton : protonList) {
        gtk_combo_box_text_append(launcher->protonCombo, proton.folder.c_str(), proton.name.c_str());
        if (proton.folder == settings.protonPath) {
            savedProtonFound = true;
        }
    }
    if (!settings.protonPath.empty() && !savedProtonFound) {
        gchar* folderName = g_path_get_basename(settings.protonPath.c_str());
        gtk_combo_box_text_append(launcher->protonCombo, settings.protonPath.c_str(), folderName);
        g_free(folderName);
    }

    if (!settings.protonPath.empty()) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(launcher->protonCombo), settings.protonPath.c_str());
    } else if (!protonList.empty()) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(launcher->protonCombo), protonList.size() - 1);
    }

    gtk_entry_set_text(launcher->wineEntry, settings.winePath.c_str());
    gtk_entry_set_text(launcher->argsEntry, settings.extraArgs.c_str());
    gtk_entry_set_text(launcher->updateUrlEntry, settings.updateUrl.c_str());

    std::string hint = "If left empty this is used: " + getDefaultPrefix();
    gtk_label_set_text(launcher->prefixHint, hint.c_str());

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(launcher->themeCombo), settings.theme.c_str());
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(launcher->themeCombo)) < 0) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(launcher->themeCombo), "system");
    }

    updateProtonCombo(launcher);
}

static void onSaveClicked(GtkButton* button, gpointer data)
{
    LauncherWindow* launcher = static_cast<LauncherWindow*>(data);
    LauncherSettings& settings = launcher->settings;
    readOptionsFromScreen(launcher);

    if (settings.runner == "proton" && settings.protonPath.empty()) {
        showMessage(launcher, "You picked Proton but no installed version was found");
    }

    if (saveSettings(settings)) {
        setStatus(launcher, "Options saved");
    } else {
        showMessage(launcher, "Could not save the options");
    }
}

static GObject* getPiece(GtkBuilder* builder, const char* name)
{
    return gtk_builder_get_object(builder, name);
}

int main(int argc, char* argv[])
{
    checkForcedTheme();
    gtk_init(&argc, &argv);

    GtkBuilder* builder = gtk_builder_new_from_resource("/bol/launcher/ui/launcher.glade");

    LauncherWindow* launcher = new LauncherWindow();
    launcher->window = GTK_WIDGET(getPiece(builder, "mainWindow"));
    launcher->serverStore = GTK_LIST_STORE(getPiece(builder, "serverStore"));
    launcher->serverTable = GTK_TREE_VIEW(getPiece(builder, "serverTable"));
    launcher->statusLabel = GTK_LABEL(getPiece(builder, "statusLabel"));
    launcher->refreshButton = GTK_BUTTON(getPiece(builder, "refreshButton"));
    launcher->connectButton = GTK_BUTTON(getPiece(builder, "connectButton"));
    launcher->offlineButton = GTK_BUTTON(getPiece(builder, "offlineButton"));
    launcher->gameExePicker = GTK_FILE_CHOOSER(getPiece(builder, "gameExePicker"));
    launcher->serverExePicker = GTK_FILE_CHOOSER(getPiece(builder, "serverExePicker"));
    launcher->runnerCombo = GTK_COMBO_BOX_TEXT(getPiece(builder, "runnerCombo"));
    launcher->protonCombo = GTK_COMBO_BOX_TEXT(getPiece(builder, "protonCombo"));
    launcher->wineEntry = GTK_ENTRY(getPiece(builder, "wineEntry"));
    launcher->prefixPicker = GTK_FILE_CHOOSER(getPiece(builder, "prefixPicker"));
    launcher->prefixHint = GTK_LABEL(getPiece(builder, "prefixHint"));
    launcher->argsEntry = GTK_ENTRY(getPiece(builder, "argsEntry"));

    launcher->themeCombo = GTK_COMBO_BOX_TEXT(getPiece(builder, "themeCombo"));
    launcher->updateUrlEntry = GTK_ENTRY(getPiece(builder, "updateUrlEntry"));
    launcher->updateButton = GTK_BUTTON(getPiece(builder, "updateButton"));
    launcher->updateProgress = GTK_PROGRESS_BAR(getPiece(builder, "updateProgress"));
    launcher->updateStatus = GTK_LABEL(getPiece(builder, "updateStatus"));

    launcher->settings = loadSettings();
    applyTheme(launcher->settings.theme);
    fillOptions(launcher);

    g_signal_connect(launcher->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(launcher->refreshButton, "clicked", G_CALLBACK(onRefreshClicked), launcher);
    g_signal_connect(launcher->updateButton, "clicked", G_CALLBACK(onUpdateClicked), launcher);
    g_signal_connect(launcher->connectButton, "clicked", G_CALLBACK(onConnectClicked), launcher);
    g_signal_connect(launcher->offlineButton, "clicked", G_CALLBACK(onOfflineClicked), launcher);
    g_signal_connect(launcher->serverTable, "row-activated", G_CALLBACK(onServerDoubleClicked), launcher);
    g_signal_connect(launcher->runnerCombo, "changed", G_CALLBACK(onRunnerChanged), launcher);
    g_signal_connect(launcher->themeCombo, "changed", G_CALLBACK(onThemeChanged), launcher);
    g_signal_connect(getPiece(builder, "clearPrefixButton"), "clicked", G_CALLBACK(onClearPrefixClicked), launcher);
    g_signal_connect(getPiece(builder, "saveButton"), "clicked", G_CALLBACK(onSaveClicked), launcher);
    g_signal_connect(launcher->gameExePicker, "file-set", G_CALLBACK(onOptionChanged), launcher);
    g_signal_connect(launcher->serverExePicker, "file-set", G_CALLBACK(onOptionChanged), launcher);
    g_signal_connect(launcher->prefixPicker, "file-set", G_CALLBACK(onOptionChanged), launcher);
    g_signal_connect(launcher->runnerCombo, "changed", G_CALLBACK(onOptionChanged), launcher);
    g_signal_connect(launcher->protonCombo, "changed", G_CALLBACK(onOptionChanged), launcher);
    g_signal_connect(launcher->themeCombo, "changed", G_CALLBACK(onOptionChanged), launcher);
    g_signal_connect(getPiece(builder, "argsInfoButton"), "clicked", G_CALLBACK(onArgsInfoClicked), launcher);

    gtk_widget_show_all(launcher->window);
    loadServers(launcher);

    gtk_main();

    GPid serverId = runningServerId(launcher);
    if (serverId != 0) {
        kill(serverId, SIGTERM);
    }

    g_object_unref(builder);
    return 0;
}
