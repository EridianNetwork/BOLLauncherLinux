#include "updater.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <vector>

struct ManifestEntry {
    std::string md5;
    std::string path;
};

static std::string joinUrl(std::string baseUrl, const std::string& path)
{
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    gchar* escaped = g_uri_escape_string(path.c_str(), "/", FALSE);
    std::string result = baseUrl + "/" + escaped;
    g_free(escaped);
    return result;
}

static bool downloadFile(const std::string& url, const std::string& destination, std::string& errorMessage)
{
    const char* args[] = {"curl", "-fsSL", "--connect-timeout", "15", "-o", destination.c_str(), url.c_str(), NULL};
    gchar* errorText = NULL;
    gint status = 0;
    GError* error = NULL;

    if (!g_spawn_sync(NULL, const_cast<gchar**>(args), NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &errorText, &status, &error)) {
        errorMessage = std::string("Could not run curl: ") + (error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    bool itWorked = g_spawn_check_exit_status(status, NULL);
    if (!itWorked) {
        errorMessage = std::string("Download failed: ") + url + (errorText ? std::string(" - ") + errorText : "");
        g_remove(destination.c_str());
    }
    g_free(errorText);
    return itWorked;
}

static std::vector<ManifestEntry> readManifest(const std::string& text)
{
    std::vector<ManifestEntry> entries;
    size_t start = 0;
    while (start < text.size()) {
        size_t lineEnd = text.find('\n', start);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }
        std::string line = text.substr(start, lineEnd - start);
        start = lineEnd + 1;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < 35 || line[32] != ' ') {
            continue;
        }

        ManifestEntry entry;
        entry.md5 = line.substr(0, 32);
        entry.path = line.substr(34);
        while (entry.path.compare(0, 2, "./") == 0) {
            entry.path = entry.path.substr(2);
        }

        if (entry.path.empty() || entry.path[0] == '/' || entry.path.find("../") != std::string::npos || entry.path == ".." || entry.path == "mad5_manifest.md5") {
            continue;
        }
        entries.push_back(entry);
    }
    return entries;
}

static std::string md5OfFile(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == NULL) {
        return "";
    }
    GChecksum* checksum = g_checksum_new(G_CHECKSUM_MD5);
    static guchar buffer[1 << 16];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        g_checksum_update(checksum, buffer, count);
    }
    std::fclose(file);
    std::string result = g_checksum_get_string(checksum);
    g_checksum_free(checksum);
    return result;
}

static std::string formatSize(gint64 bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        unit++;
    }
    gchar* text = unit == 0 ? g_strdup_printf("%ld B", static_cast<long>(bytes)) : g_strdup_printf("%.2f %s", value, units[unit]);
    std::string result = text;
    g_free(text);
    return result;
}

static std::string withTime(const std::string& message)
{
    GDateTime* now = g_date_time_new_now_local();
    gchar* time = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
    std::string result = std::string("[") + time + "] " + message;
    g_free(time);
    g_date_time_unref(now);
    return result;
}

bool updateGameFiles(const std::string& baseUrl, const std::string& gameFolder, const UpdateProgress& progress, const UpdateLog& log, std::string& resultMessage)
{
    if (baseUrl.empty()) {
        resultMessage = "No update URL configured";
        return false;
    }
    if (gameFolder.empty()) {
        resultMessage = "First select BOL.exe in Options";
        return false;
    }

    progress("Reading the md5 manifest...", 0);
    std::string manifestFile = gameFolder + "/.md5_manifest.md5.tmp";
    if (!downloadFile(joinUrl(baseUrl, "md5_manifest.md5"), manifestFile, resultMessage)) {
        return false;
    }
    gchar* manifestText = NULL;
    bool readOk = g_file_get_contents(manifestFile.c_str(), &manifestText, NULL, NULL);
    g_remove(manifestFile.c_str());
    if (!readOk) {
        resultMessage = "Could not read the downloaded manifest";
        return false;
    }
    std::vector<ManifestEntry> entries = readManifest(manifestText);
    g_free(manifestText);
    if (entries.empty()) {
        resultMessage = "The manifest is empty or has a wrong format";
        return false;
    }

    std::vector<ManifestEntry> outdated;
    for (size_t i = 0; i < entries.size(); i++) {
        progress("Checking " + entries[i].path, 0.5 * i / entries.size());
        std::string localPath = gameFolder + "/" + entries[i].path;
        if (md5OfFile(localPath) != entries[i].md5) {
            outdated.push_back(entries[i]);
        }
    }

    if (outdated.empty()) {
        resultMessage = "Everything is up to date (" + std::to_string(entries.size()) + " files checked)";
        return true;
    }

    log(withTime("Update started: " + std::to_string(outdated.size()) + " files to download"));
    int failed = 0;
    gint64 totalBytes = 0;
    for (size_t i = 0; i < outdated.size(); i++) {
        const ManifestEntry& entry = outdated[i];
        progress("Downloading " + entry.path + " (" + std::to_string(i + 1) + "/" + std::to_string(outdated.size()) + ")", 0.5 + 0.5 * i / outdated.size());

        std::string localPath = gameFolder + "/" + entry.path;
        gchar* folder = g_path_get_dirname(localPath.c_str());
        g_mkdir_with_parents(folder, 0755);
        g_free(folder);

        std::string tempPath = localPath + ".part";
        std::string errorMessage;
        if (!downloadFile(joinUrl(baseUrl, entry.path), tempPath, errorMessage)) {
            failed++;
            resultMessage = errorMessage;
            log(withTime("FAILED " + entry.path + " - " + errorMessage));
            continue;
        }
        if (md5OfFile(tempPath) != entry.md5) {
            g_remove(tempPath.c_str());
            failed++;
            resultMessage = "The MD5 of the downloaded file does not match: " + entry.path;
            log(withTime("FAILED " + entry.path + " - MD5 mismatch"));
            continue;
        }
        GStatBuf info;
        gint64 size = g_stat(tempPath.c_str(), &info) == 0 ? static_cast<gint64>(info.st_size) : 0;
        g_rename(tempPath.c_str(), localPath.c_str());
        totalBytes += size;
        log(withTime("Downloaded " + entry.path + " (" + formatSize(size) + ")"));
    }
    log(withTime("Update finished: " + std::to_string(outdated.size() - failed) + " ok, " + std::to_string(failed) + " failed, " + formatSize(totalBytes) + " total"));

    if (failed > 0) {
        resultMessage = std::to_string(failed) + " of " + std::to_string(outdated.size()) + " files failed. Last error: " + resultMessage;
        return false;
    }
    resultMessage = "Updated " + std::to_string(outdated.size()) + " files";
    return true;
}
