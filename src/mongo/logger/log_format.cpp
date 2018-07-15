/**
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

#include "mongo/platform/basic.h"

#include "mongo/logger/log_format.h"

#include "mongo/db/server_options.h"
#include "mongo/logger/message_event_utf8_encoder.h"

namespace mongo {
namespace logger {

StatusWith<LogFormat> parseLogFormat(const std::string& logFormat) {
    if (logFormat == "default") {
        return LogFormat::Default;
    } else if (logFormat == "plain") {
        return LogFormat::Plain;
    } else if (logFormat == "json" || logFormat == "JSON") {
        return LogFormat::JSON;
    } else if (logFormat == "bson" || logFormat == "BSON") {
        return LogFormat::BSON;
    }
    return Status(ErrorCodes::BadValue, "unsupported logFormat value " + logFormat);
}

Status resolveDefaultLogFormat(const LogFormat& defaultFormat) {
    if (serverGlobalParams.logFormat == LogFormat::Default) {
        serverGlobalParams.logFormat = defaultFormat;
    }
    invariant(serverGlobalParams.logFormat != LogFormat::Default);
    if (serverGlobalParams.logAppend) {
        if (serverGlobalParams.logFormat == LogFormat::JSON) {
            return Status(ErrorCodes::BadValue, "logFormat json doesn't support logAppend");
        }
        if (serverGlobalParams.logFormat == LogFormat::BSON) {
            return Status(ErrorCodes::BadValue, "logFormat bson doesn't support logAppend");
        }
    }
    return Status::OK();
}

std::unique_ptr<Encoder<MessageEventEphemeral>> makeUniqueMessageEventEncoder() {
    invariant(serverGlobalParams.logFormat != LogFormat::Default);
    if (serverGlobalParams.logFormat == LogFormat::Plain) {
        return std::make_unique<MessageEventDetailsEncoder>();
    } else if (serverGlobalParams.logFormat == LogFormat::JSON) {
        return std::make_unique<MessageEventDocumentEncoder>();
    } else if (serverGlobalParams.logFormat == LogFormat::BSON) {
        return std::make_unique<MessageEventDocumentEncoder>();  // FIXME
    }
    MONGO_UNREACHABLE;
}

}  // namespace logger
}  // namespace mongo
