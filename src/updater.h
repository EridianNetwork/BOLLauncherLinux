#pragma once

#include <functional>
#include <string>

using UpdateProgress = std::function<void(const std::string& text, double fraction)>;

using UpdateLog = std::function<void(const std::string& line)>;

bool updateGameFiles(const std::string& baseUrl, const std::string& gameFolder, const UpdateProgress& progress, const UpdateLog& log, std::string& resultMessage);

constexpr const char* UPDATE_URL = "https://d2o70mw1v6xhb2.cloudfront.net/manifest";
