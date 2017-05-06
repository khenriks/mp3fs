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

#include <cstdarg>
#include <ctime>
#include <iostream>

#include <syslog.h>

namespace {
    Logging* logging;
}

Logging::Logging(std::string logfile, level max_level, bool to_stderr,
                 bool to_syslog) :
    max_level_(max_level), to_stderr_(to_stderr), to_syslog_(to_syslog) {
    if (!logfile.empty()) {
        logfile_.open(logfile);
    }
    if (to_syslog_) {
        openlog("mp3fs", 0, LOG_USER);
    }
}

Logging::Logger::~Logger() {
    if (!logging_ || loglevel_ > logging_->max_level_) return;

    // Construct string containing time
    std::time_t now = std::time(nullptr);
    std::string time_string(30, '\0');
    time_string.resize(std::strftime(&time_string[0], time_string.size(),
                                     "%F %T", std::localtime(&now)));

    std::string msg = "[" + time_string + "] " +
        level_name_map_.at(loglevel_) + ": " + str();

    if (logging_->to_syslog_) {
        syslog(syslog_level_map_.at(loglevel_), "%s", msg.c_str());
    }
    if (logging_->logfile_.is_open()) logging_->logfile_ << msg << std::endl;
    if (logging_->to_stderr_) std::clog << msg << std::endl;
}

const std::map<Logging::level,int> Logging::Logger::syslog_level_map_ = {
    {ERROR, LOG_ERR},
    {INFO, LOG_INFO},
    {DEBUG, LOG_DEBUG},
};

const std::map<Logging::level,std::string> Logging::Logger::level_name_map_ = {
    {ERROR, "ERROR"},
    {INFO, "INFO"},
    {DEBUG, "DEBUG"},
};

Logging::Logger Log(Logging::level lev) {
    return {lev, logging};
}

bool InitLogging(std::string logfile, Logging::level max_level, bool to_stderr,
                 bool to_syslog) {
    logging = new Logging(logfile, max_level, to_stderr, to_syslog);
    return !logging->GetFail();
}

void log_with_level(Logging::level level, const char* format, va_list ap) {
    log_with_level(level, "", format, ap);
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
