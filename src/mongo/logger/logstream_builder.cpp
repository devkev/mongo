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

#include "mongo/logger/logstream_builder.h"

#include <memory>

#include "mongo/base/init.h"
#include "mongo/base/status.h"
#include "mongo/logger/message_event_utf8_encoder.h"
#include "mongo/logger/tee.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/assert_util.h"  // TODO: remove apple dep for this in threadlocal.h
#include "mongo/util/time_support.h"

namespace mongo {

namespace {

/// This flag indicates whether the system providing a per-thread cache of ostringstreams
/// for use by LogstreamBuilder instances is initialized and ready for use.  Until this
/// flag is true, LogstreamBuilder instances must not use the cache.
bool isThreadOstreamCacheInitialized = false;

MONGO_INITIALIZER(LogstreamBuilder)(InitializerContext*) {
    isThreadOstreamCacheInitialized = true;
    return Status::OK();
}

thread_local std::unique_ptr<logger::Messages> threadOstreamCache;

// During unittests, where we don't use quickExit(), static finalization may destroy the
// cache before its last use, so mark it as not initialized in that case.
// This must be after the definition of threadOstreamCache so that it is destroyed first.
struct ThreadOstreamCacheFinalizer {
    ~ThreadOstreamCacheFinalizer() {
        isThreadOstreamCacheInitialized = false;
    }
} threadOstreamCacheFinalizer;

}  // namespace

namespace logger {

LogstreamBuilder::LogstreamBuilder(MessageLogDomain* domain,
                                   StringData contextName,
                                   LogSeverity severity)
    : LogstreamBuilder(domain, contextName, std::move(severity), LogComponent::kDefault) {}

LogstreamBuilder::LogstreamBuilder(MessageLogDomain* domain,
                                   StringData contextName,
                                   LogSeverity severity,
                                   LogComponent component,
                                   bool shouldCache)
    : _domain(domain),
      _contextName(contextName.toString()),
      _severity(std::move(severity)),
      _component(std::move(component)),
      _tee(nullptr),
      _shouldCache(shouldCache) {}

LogstreamBuilder::LogstreamBuilder(logger::MessageLogDomain* domain,
                                   StringData contextName,
                                   LabeledLevel labeledLevel)
    : LogstreamBuilder(domain, contextName, static_cast<LogSeverity>(labeledLevel)) {
    setBaseMessage(labeledLevel.getLabel());
}

LogstreamBuilder::~LogstreamBuilder() {
    _handleStr();
    if (_os) {
        MessageEventEphemeral message(
            Date_t::now(), _severity, _component, _contextName, _baseMessage, stream());
        message.setIsTruncatable(_isTruncatable);
        _domain->append(message).transitional_ignore();
        if (_tee) {
            std::ostringstream os;
            // FIXME: should Tees always use Plain format?
            // Probably each Tee should be given its own encoder?
            // That way each could have its own default.
            // The only other alternative is to use an encoder from _domain - but they are buried inside its Appenders.  Which one is most appropriate?
            // Besides, I like the idea of startupWarnings still always being Plain, even if the server logs are JSON/BSON.
            logger::MessageEventDetailsEncoder teeEncoder;
            teeEncoder.encode(message, os);
            _tee->write(os.str());
        }
        _os->clear();
        if (_shouldCache && isThreadOstreamCacheInitialized && !threadOstreamCache) {
            threadOstreamCache = std::move(_os);
        }
    }
}

void operator<<(LogstreamBuilder& log, Tee* tee) {
    log.makeStream();  // Adding a Tee counts for purposes of deciding to make a log message.
    // TODO: dassert(!_tee);
    log._tee = tee;
}

void operator<<(LogstreamBuilder&& log, Tee* tee) {
    log << tee;
}

void LogstreamBuilder::makeStream() {
    if (!_os) {
        if (_shouldCache && isThreadOstreamCacheInitialized && threadOstreamCache) {
            _os = std::move(threadOstreamCache);
        } else {
            _os = stdx::make_unique<Messages>();
        }
    }
}

}  // namespace logger
}  // namespace mongo
