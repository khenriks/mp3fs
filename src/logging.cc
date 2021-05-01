/*
 * Logging class source for mp3fs
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

#include "logging.h"

#include <syslog.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {
Logging* logging;
constexpr size_t kTimeBufferSize = 30;

std::string multi_substitute(
    std::string src, std::unordered_map<std::string, std::string> subs) {
    std::string result;
    for (auto it = src.cbegin(); it != src.cend();) {
        bool matched = false;
        for (const auto& kv : subs) {
            if (std::equal(kv.first.begin(), kv.first.end(), it)) {
                result.append(kv.second);
                it += static_cast<ptrdiff_t>(kv.first.length());
                matched = true;
                break;
            }
        }
        if (!matched) {
            result.push_back(*it);
            ++it;
        }
    }
    return result;
}
}  // namespace

Logging::Logging(std::string logfile, Level max_level, std::string log_format,
                 bool to_stderr, bool to_syslog)
    : max_level_(max_level),
      log_format_(std::move(log_format)),
      to_stderr_(to_stderr),
      to_syslog_(to_syslog) {
    if (!logfile.empty()) {
        logfile_.open(logfile);
    }
    if (to_syslog_) {
        openlog("mp3fs", 0, LOG_USER);
    }
}

Logging::Logger::~Logger() {
    if (logging_ == nullptr || loglevel_ > logging_->max_level_) {
        return;
    }

    // Construct string containing time
    std::time_t now = std::time(nullptr);
    std::string time_string(kTimeBufferSize, '\0');
    time_string.resize(std::strftime(&time_string[0], time_string.size(),
                                     "%F %T", std::localtime(&now)));

    // Construct string with thread ID
    std::ostringstream tid_stream;
    tid_stream << std::this_thread::get_id();

    std::string msg = multi_substitute(logging_->log_format_,
                                       {{"%T", time_string},
                                        {"%I", tid_stream.str()},
                                        {"%L", kLevelNameMap.at(loglevel_)},
                                        {"%M", str()}});

    if (logging_->to_syslog_) {
        syslog(kSyslogLevelMap.at(loglevel_), "%s", msg.c_str());
    }
    if (logging_->logfile_.is_open()) {
        logging_->logfile_ << msg << std::endl;
    }
    if (logging_->to_stderr_) {
        std::clog << msg << std::endl;
    }
}

const std::map<Logging::Level, int> Logging::Logger::kSyslogLevelMap = {
    {ERROR, LOG_ERR},
    {INFO, LOG_INFO},
    {DEBUG, LOG_DEBUG},
};

const std::map<Logging::Level, std::string> Logging::Logger::kLevelNameMap = {
    {ERROR, "ERROR"},
    {INFO, "INFO"},
    {DEBUG, "DEBUG"},
};

Logging::Logger Log(Logging::Level lev) {
    return {lev, logging};
}

Logging::Level string_to_level(std::string level) {
    static const std::map<std::string, Logging::Level> kLevelMap = {
        {"DEBUG", DEBUG},
        {"INFO", INFO},
        {"ERROR", ERROR},
    };
    auto it = kLevelMap.find(level);

    if (it == kLevelMap.end()) {
        std::cerr << "Invalid logging level string: " << level << std::endl;
        return Logging::Level::INVALID;
    }

    return it->second;
}

bool init_logging(std::string logfile, Logging::Level max_level,
                  std::string log_format, bool to_stderr, bool to_syslog) {
    if (max_level == Logging::Level::INVALID) {
        return false;
    }
    logging = new Logging(logfile, max_level, log_format, to_stderr, to_syslog);
    return !logging->get_fail();
}

void log_with_level(Logging::Level level, const char* prefix,
                    const char* format, va_list ap) {
    // This copy is because we call vsnprintf twice, and ap is undefined after
    // the first call.
    va_list ap2;
    va_copy(ap2, ap);

    int size = vsnprintf(nullptr, 0, format, ap);
    std::string buffer(size, '\0');
    vsnprintf(&buffer[0], buffer.size() + 1, format, ap2);

    va_end(ap2);

    Log(level) << prefix << buffer;
}
