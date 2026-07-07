#pragma once

#include <string>

namespace ir_remote {

std::string defaultIrRecordingsDirectory();
std::string normalizeIrDirectory(const std::string& path);
bool ensureDirectoryExists(const std::string& path, const char* log_tag);

}  // namespace ir_remote
