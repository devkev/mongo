/*
 *    Copyright (C) 2018 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/logger/encoder.h"
#include "mongo/logger/message_event.h"

namespace mongo {

template <typename T>
class StatusWith;

namespace logger {

enum class LogFormat {
    Default,   // Figure out based on context (file, console, unittest, etc)
    Plain,     // Use MessageEventDetailsEncoder
    JSON,      // Use MessageEventDocumentEncoder with JSON output
    BSON       // Use MessageEventDocumentEncoder with BSON output
};

// These must never be set to LogFormat::Default.
constexpr LogFormat DEFAULT_LOG_FORMAT_FILE = LogFormat::Plain;
constexpr LogFormat DEFAULT_LOG_FORMAT_CONSOLE = LogFormat::Plain;
constexpr LogFormat DEFAULT_LOG_FORMAT_UNITTEST = LogFormat::Plain;

StatusWith<LogFormat> parseLogFormat(const std::string&);

Status resolveDefaultLogFormat(const LogFormat& defaultFormat);

std::unique_ptr<Encoder<MessageEventEphemeral>> makeUniqueMessageEventEncoder();

}  // namespace logger
}  // namespace mongo
