#pragma once
// remake/platform/log.gd: messages of the app and the tools: info with its level, debug details
// only when verbose, warnings and errors on their own channels (stderr here; logcat on Android).
#include <string>

namespace t3::Log {

inline bool verbose = false;   // Godot's --verbose

void debug(const std::string& message);
void info(const std::string& message);
void warning(const std::string& message);
void error(const std::string& message);
// Every message from here on is also appended to the file at `path` (the file is started anew),
// for reading a device's log without adb (Android: the app's external files folder).
void to_file(const std::string& path);

}  // namespace t3::Log
