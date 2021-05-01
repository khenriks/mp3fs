/*
 * Logging class header for mp3fs
 *
 * Copyright (C) 2017 K. Henriksson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef MP3FS_LOGGING_H_
#define MP3FS_LOGGING_H_

#include <cstdarg>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

class Logging {
 public:
    enum class Level { INVALID = 0, ERROR = 1, INFO = 2, DEBUG = 3 };

    /*
     * Arguments:
     *   logfile: The name of a file to write logging output to. If empty, no
     *     output will be written.
     *   max_level: The maximum level of log output to write.
     *   log_format: The format to use for writing log entries.
     *   to_stderr: Whether to write log output to stderr.
     *   to_syslog: Whether to write log output to syslog.
     */
    explicit Logging(std::string logfile, Level max_level,
                     std::string log_format, bool to_stderr, bool to_syslog);

    bool get_fail() const { return logfile_.fail(); }

 private:
    class Logger : public std::ostringstream {
     public:
        Logger(Level loglevel, Logging* logging)
            : loglevel_(loglevel), logging_(logging) {}
        Logger() = default;
        ~Logger() override;

     private:
        const Level loglevel_ = Level::DEBUG;

        Logging* logging_ = nullptr;

        static const std::map<Level, int> kSyslogLevelMap;
        static const std::map<Level, std::string> kLevelNameMap;
    };

    friend Logger Log(Level lev);  // NOLINT(readability-identifier-naming)
    friend Logger;

    std::ofstream logfile_;
    const Level max_level_;
    const std::string log_format_;
    const bool to_stderr_;
    const bool to_syslog_;
};

Logging::Level string_to_level(std::string level);

bool init_logging(std::string logfile, Logging::Level max_level,
                  std::string log_format, bool to_stderr, bool to_syslog);

// NOLINTNEXTLINE(readability-identifier-naming)
constexpr auto ERROR = Logging::Level::ERROR;
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr auto INFO = Logging::Level::INFO;
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr auto DEBUG = Logging::Level::DEBUG;

void log_with_level(Logging::Level level, const char* prefix,
                    const char* format, va_list ap);

#endif  // MP3FS_LOGGING_H_
