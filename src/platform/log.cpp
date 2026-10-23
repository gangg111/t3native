#include "platform/log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace t3::Log {

namespace {
std::mutex g_file_mutex;
std::FILE* g_file = nullptr;
void (*g_tap)(const std::string&) = nullptr;

// The copy in the log file: a time stamp, the level, the message (flushed: a crash keeps it).
void to_log_file(const char* tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    if (!g_file) return;
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&now));
    std::fprintf(g_file, "%s [%s] %s\n", stamp, tag, message.c_str());
    std::fflush(g_file);
}

void to_tap(const std::string& message) {
    if (g_tap) g_tap(message);
}

#ifdef __ANDROID__
void put(int level, const char* tag, const std::string& message) {
    __android_log_write(level, "t3native", message.c_str());
    to_log_file(tag, message);
    to_tap(message);
}
constexpr int DEBUG = ANDROID_LOG_DEBUG, INFO = ANDROID_LOG_INFO, WARN = ANDROID_LOG_WARN, ERROR = ANDROID_LOG_ERROR;
#else
void put(int, const char* tag, const std::string& message) {
    std::fprintf(stderr, "[%s] %s\n", tag, message.c_str());
    to_log_file(tag, message);
    to_tap(message);
}
constexpr int DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3;
#endif
}  // namespace

void debug(const std::string& m) {
    if (verbose) put(DEBUG, "debug", m);
}
void info(const std::string& m) { put(INFO, "info", m); }
void warning(const std::string& m) { put(WARN, "warning", m); }
void error(const std::string& m) { put(ERROR, "error", m); }

void to_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    if (g_file) std::fclose(g_file);
    g_file = std::fopen(path.c_str(), "w");
}

void tap(void (*listener)(const std::string& message)) { g_tap = listener; }

}  // namespace t3::Log
