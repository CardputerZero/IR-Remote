#include "models/ir_recording_files_model.hpp"
#include "core/ir_remote_config.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>

namespace ir_remote {
namespace {

constexpr const char* kFileHeader = "M5CardputerZero IR Remote signal v1";
constexpr const char* kFileExt    = ".ir";

bool hasIrExtension(const std::string& name)
{
    if (name.size() < 3) {
        return false;
    }

    std::string ext = name.substr(name.size() - 3);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == kFileExt;
}

uint64_t totalDurationUs(const IrSignal& signal)
{
    uint64_t total = 0;
    for (const auto& pulse : signal.pulses) {
        total += pulse.durationUs;
    }
    return total;
}

std::string fileBaseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    std::string name   = slash == std::string::npos ? path : path.substr(slash + 1);
    if (hasIrExtension(name)) {
        name.resize(name.size() - 3);
    }
    return name;
}

std::string sanitizeFileName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ' || c == '.') {
            out.push_back('_');
        }
    }

    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? "signal" : out;
}

std::string escapeValue(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string unescapeValue(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out.push_back(value[i]);
            continue;
        }

        const char next = value[++i];
        switch (next) {
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            default:
                out.push_back(next);
                break;
        }
    }
    return out;
}

uint64_t parseU64(const std::string& value, uint64_t fallback = 0)
{
    char* end                       = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    return end && end != value.c_str() ? static_cast<uint64_t>(parsed) : fallback;
}

uint32_t parseU32(const std::string& value, uint32_t fallback = 0)
{
    return static_cast<uint32_t>(parseU64(value, fallback));
}

std::string uniquePath(const std::string& dir, const std::string& base)
{
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    for (int suffix = 0; suffix < 1000; ++suffix) {
        char buffer[256] = {};
        if (suffix == 0) {
            std::snprintf(buffer, sizeof(buffer), "%llu_%s%s", static_cast<unsigned long long>(now), base.c_str(),
                          kFileExt);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%llu_%s_%03d%s", static_cast<unsigned long long>(now), base.c_str(),
                          suffix, kFileExt);
        }

        const std::string path = dir + "/" + buffer;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0) {
            return path;
        }
    }
    return dir + "/" + std::to_string(now) + "_" + base + kFileExt;
}

bool parseRawUs(const std::string& raw, IrSignal& outSignal)
{
    std::stringstream stream(raw);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            continue;
        }

        const bool pulse = token.front() != '-';
        if (token.front() == '+' || token.front() == '-') {
            token.erase(token.begin());
        }

        const uint32_t duration = parseU32(token, 0);
        if (duration == 0) {
            continue;
        }
        outSignal.pulses.push_back({pulse, duration});
    }

    return !outSignal.pulses.empty();
}

std::string rawUsText(const IrSignal& signal)
{
    std::string out;
    for (size_t i = 0; i < signal.pulses.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        out.push_back(signal.pulses[i].pulse ? '+' : '-');
        out += std::to_string(signal.pulses[i].durationUs);
    }
    return out;
}

bool readSignalFile(const std::string& path, IrSignal* outSignal, IrRecordingFile* outInfo)
{
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    std::string line;
    if (!std::getline(file, line) || line != kFileHeader) {
        return false;
    }

    IrSignal signal;
    IrRecordingFile info;
    info.path = path;
    info.name = fileBaseName(path);

    std::string raw;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const size_t equal = line.find('=');
        if (equal == std::string::npos) {
            continue;
        }

        const std::string key   = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);
        if (key == "name") {
            info.name = unescapeValue(value);
        } else if (key == "created_unix_sec") {
            info.createdUnixSec = parseU64(value, 0);
        } else if (key == "carrier_hz") {
            info.carrierHz   = parseU32(value, info.carrierHz);
            signal.carrierHz = info.carrierHz;
        } else if (key == "duty_cycle_percent") {
            info.dutyCyclePercent   = parseU32(value, info.dutyCyclePercent);
            signal.dutyCyclePercent = info.dutyCyclePercent;
        } else if (key == "source_driver") {
            info.sourceDriver   = unescapeValue(value);
            signal.sourceDriver = info.sourceDriver;
        } else if (key == "source_device") {
            info.sourceDevice   = unescapeValue(value);
            signal.sourceDevice = info.sourceDevice;
        } else if (key == "raw_us") {
            raw = value;
        }
    }

    if (!parseRawUs(raw, signal)) {
        return false;
    }

    info.pulseCount      = static_cast<uint32_t>(signal.pulses.size());
    info.totalDurationUs = totalDurationUs(signal);

    if (outSignal) {
        *outSignal = std::move(signal);
    }
    if (outInfo) {
        *outInfo = std::move(info);
    }
    return true;
}

}  // namespace

IrRecordingFilesModel::IrRecordingFilesModel() : IrRecordingFilesModel(defaultIrRecordingsDirectory())
{
}

IrRecordingFilesModel::IrRecordingFilesModel(std::string recordings_dir)
    : _recordings_dir(normalizeIrDirectory(recordings_dir))
{
    spdlog::info("IrRecordingFilesModel: recordingsDir={}", _recordings_dir);
}

const IrRecordingFile* IrRecordingFilesModel::selectedFile() const
{
    const auto& list = _files.get();
    const int index  = _selected_index.get();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return nullptr;
    }
    return &list[index];
}

void IrRecordingFilesModel::refresh(bool preserveSelected)
{
    const IrRecordingFile* selected = preserveSelected ? selectedFile() : nullptr;
    const std::string selected_path = selected ? selected->path : "";

    struct Entry {
        IrRecordingFile file;
        time_t modified = 0;
    };

    std::vector<Entry> entries;
    if (!ensureDirectoryExists(_recordings_dir, "IrRecordingFilesModel")) {
        _files.set(std::vector<IrRecordingFile>{});
        _selected_index.set(-1);
        return;
    }

    DIR* handle = opendir(_recordings_dir.c_str());
    if (!handle) {
        spdlog::warn("IrRecordingFilesModel: failed to open recordings directory {}, errno={}", _recordings_dir, errno);
        _files.set(std::vector<IrRecordingFile>{});
        _selected_index.set(-1);
        return;
    }

    while (dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || !hasIrExtension(name)) {
            continue;
        }

        const std::string path = _recordings_dir + "/" + name;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
            continue;
        }

        IrRecordingFile recording;
        if (readSignalFile(path, nullptr, &recording)) {
            entries.push_back({std::move(recording), info.st_mtime});
        }
    }
    closedir(handle);

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.modified != b.modified) {
            return a.modified > b.modified;
        }
        return a.file.path < b.file.path;
    });

    std::vector<IrRecordingFile> list;
    list.reserve(entries.size());
    int selected_index = entries.empty() ? -1 : 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!selected_path.empty() && entries[i].file.path == selected_path) {
            selected_index = static_cast<int>(i);
        }
        list.push_back(std::move(entries[i].file));
    }

    _files.set(std::move(list));
    _selected_index.set(selected_index);
}

void IrRecordingFilesModel::selectPrevious()
{
    const auto& list = _files.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    int index = _selected_index.get() - 1;
    if (index < 0) {
        index = static_cast<int>(list.size()) - 1;
    }
    _selected_index.set(index);
}

void IrRecordingFilesModel::selectNext()
{
    const auto& list = _files.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    int index = _selected_index.get() + 1;
    if (index >= static_cast<int>(list.size())) {
        index = 0;
    }
    _selected_index.set(index);
}

bool IrRecordingFilesModel::selectPath(const std::string& path)
{
    const auto& list = _files.get();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].path == path) {
            _selected_index.set(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

bool IrRecordingFilesModel::deleteFile(const std::string& path)
{
    if (std::remove(path.c_str()) != 0) {
        spdlog::warn("IrRecordingFilesModel: failed to delete recording path={}, errno={}", path, errno);
        return false;
    }

    spdlog::info("IrRecordingFilesModel: deleted recording path={}", path);
    return true;
}

bool IrRecordingFilesModel::loadSignal(const std::string& path, IrSignal& outSignal, IrRecordingFile* outInfo) const
{
    return readSignalFile(path, &outSignal, outInfo);
}

bool IrRecordingFilesModel::saveSignal(const IrSignal& signal, const std::string& name, IrRecordingFile& outFile)
{
    if (signal.empty()) {
        spdlog::warn("IrRecordingFilesModel: refusing to save empty signal");
        return false;
    }

    if (!ensureDirectoryExists(_recordings_dir, "IrRecordingFilesModel")) {
        return false;
    }

    const std::string title = name.empty() ? "IR Signal" : name;
    const std::string path  = uniquePath(_recordings_dir, sanitizeFileName(title));
    const uint64_t now      = static_cast<uint64_t>(std::time(nullptr));

    std::ofstream file(path);
    if (!file) {
        spdlog::warn("IrRecordingFilesModel: failed to create recording file path={}", path);
        return false;
    }

    file << kFileHeader << "\n";
    file << "name=" << escapeValue(title) << "\n";
    file << "created_unix_sec=" << now << "\n";
    file << "carrier_hz=" << signal.carrierHz << "\n";
    file << "duty_cycle_percent=" << signal.dutyCyclePercent << "\n";
    file << "source_driver=" << escapeValue(signal.sourceDriver) << "\n";
    file << "source_device=" << escapeValue(signal.sourceDevice) << "\n";
    file << "raw_us=" << rawUsText(signal) << "\n";
    if (!file) {
        spdlog::warn("IrRecordingFilesModel: failed while writing recording file path={}", path);
        return false;
    }
    file.close();
    if (!file) {
        spdlog::warn("IrRecordingFilesModel: failed to close recording file path={}", path);
        return false;
    }

    outFile = IrRecordingFile{path,
                              title,
                              signal.carrierHz,
                              signal.dutyCyclePercent,
                              static_cast<uint32_t>(signal.pulses.size()),
                              totalDurationUs(signal),
                              now,
                              signal.sourceDriver,
                              signal.sourceDevice};
    spdlog::info("IrRecordingFilesModel: saved recording path={}, pulses={}", path, signal.pulses.size());
    refresh(true);
    selectPath(outFile.path);
    return true;
}

}  // namespace ir_remote
