#pragma once

#include <string>

namespace logx {

enum class Level { Debug, Info, Warn, Error };

void set_verbose(bool v);
void set_log_file(const std::wstring& path); // empty disables file logging
void write(Level level, const std::string& msg);
void vwrite(Level level, const char* fmt, ...);

inline void debug(const std::string& m) { write(Level::Debug, m); }
inline void info(const std::string& m) { write(Level::Info, m); }
inline void warn(const std::string& m) { write(Level::Warn, m); }
inline void error(const std::string& m) { write(Level::Error, m); }

} // namespace logx
