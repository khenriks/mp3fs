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
#include <cstdio>
#include <ctime>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {
Logging* logging;
constexpr size_t kTimeBufferSize = 30;

std::string MultiSubstitute(std::string src,
                            std::unordered_map<std::string, std::string> subs) {
    std::string result;
    for (auto it = src.cbegin(); it != src.cend();) {
        bool matched = false;
        for (const auto& kv : subs) {
            if (std::equal(kv.first.begin(), kv.first.end(), it)) {
                result.append(kv.second);
                it += kv.first.length();
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

Logging::Logging(std::string logfile, level max_level, std::string log_format,
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

    std::string msg = MultiSubstitute(logging_->log_format_,
                                      {{"%T", time_string},
                                       {"%I", tid_stream.str()},
                                       {"%L", level_name_map_.at(loglevel_)},
                                       {"%M", str()}});

    if (logging_->to_syslog_) {
        syslog(syslog_level_map_.at(loglevel_), "%s", msg.c_str());
    }
    if (logging_->logfile_.is_open()) {
        logging_->logfile_ << msg << std::endl;
    }
    if (logging_->to_stderr_) {
        std::clog << msg << std::endl;
    }
}

const std::map<Logging::level, int> Logging::Logger::syslog_level_map_ = {
    {ERROR, LOG_ERR},
    {INFO, LOG_INFO},
    {DEBUG, LOG_DEBUG},
};

const std::map<Logging::level, std::string> Logging::Logger::level_name_map_ = {
    {ERROR, "ERROR"},
    {INFO, "INFO"},
    {DEBUG, "DEBUG"},
};

Logging::Logger Log(Logging::level lev) {
    return {lev, logging};
}

Logging::level StringToLevel(std::string level) {
    static const std::map<std::string, Logging::level> level_map = {
        {"DEBUG", DEBUG},
        {"INFO", INFO},
        {"ERROR", ERROR},
    };
    auto it = level_map.find(level);

    if (it == level_map.end()) {
        std::cerr << "Invalid logging level string: " << level << std::endl;
        return Logging::level::INVALID;
    }

    return it->second;
}

bool InitLogging(std::string logfile, Logging::level max_level,
                 std::string log_format, bool to_stderr, bool to_syslog) {
    if (max_level == Logging::level::INVALID) {
        return false;
    }
    logging = new Logging(logfile, max_level, log_format, to_stderr, to_syslog);
    return !logging->GetFail();
}

void log_with_level(Logging::level level, const char* prefix,
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
