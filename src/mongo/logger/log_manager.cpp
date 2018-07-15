/*    Copyright 2013 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/logger/log_manager.h"

#include "mongo/logger/console_appender.h"
#include "mongo/logger/log_format.h"

namespace mongo {
namespace logger {

LogManager::LogManager() {
    reattachDefaultConsoleAppender();
}

LogManager::~LogManager() {
    for (DomainsByNameMap::iterator iter = _domains.begin(); iter != _domains.end(); ++iter) {
        delete iter->second;
    }
}

MessageLogDomain* LogManager::getNamedDomain(const std::string& name) {
    MessageLogDomain*& domain = _domains[name];
    if (!domain) {
        domain = new MessageLogDomain;
    }
    return domain;
}

void LogManager::detachDefaultConsoleAppender() {
    invariant(_defaultAppender);
    _globalDomain.detachAppender(_defaultAppender);
    _defaultAppender.reset();
}

void LogManager::reattachDefaultConsoleAppender() {
    invariant(!_defaultAppender);
    invariant(logger::resolveDefaultLogFormat(logger::DEFAULT_LOG_FORMAT_CONSOLE).isOK());
    _defaultAppender =
        _globalDomain.attachAppender(std::make_unique<ConsoleAppender<MessageEventEphemeral>>(
            logger::makeUniqueMessageEventEncoder()));
}

bool LogManager::isDefaultConsoleAppenderAttached() const {
    return static_cast<bool>(_defaultAppender);
}

}  // logger
}  // mongo
