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

#pragma once

#include <boost/optional.hpp>
#include <memory>
#include <sstream>
#include <string>

#include "mongo/base/error_codes.h"
#include "mongo/bson/bsontypes.h"
#include "mongo/logger/labeled_level.h"
#include "mongo/logger/log_component.h"
#include "mongo/logger/log_severity.h"
#include "mongo/logger/message_log_domain.h"
#include "mongo/stdx/chrono.h"
#include "mongo/util/exit_code.h"

namespace mongo {
namespace logger {

class Tee;

/**
 * Stream-ish object used to build and append log messages.
 */
class LogstreamBuilder {
public:
    static LogSeverity severityCast(int ll) {
        return LogSeverity::cast(ll);
    }
    static LogSeverity severityCast(LogSeverity ls) {
        return ls;
    }
    static LabeledLevel severityCast(const LabeledLevel& labeled) {
        return labeled;
    }

    /**
     * Construct a LogstreamBuilder that writes to "domain" on destruction.
     *
     * "contextName" is a short name of the thread or other context.
     * "severity" is the logging severity of the message.
     */
    LogstreamBuilder(MessageLogDomain* domain, StringData contextName, LogSeverity severity);

    /**
     * Construct a LogstreamBuilder that writes to "domain" on destruction.
     *
     * "contextName" is a short name of the thread or other context.
     * "severity" is the logging severity of the message.
     * "component" is the primary log component of the message.
     *
     * By default, this class will create one ostream per thread, and it
     * will cache that object in a threadlocal and reuse it for subsequent
     * logs messages. Set "shouldCache" to false to create a new ostream
     * for each instance of this class rather than cacheing.
     */
    LogstreamBuilder(MessageLogDomain* domain,
                     StringData contextName,
                     LogSeverity severity,
                     LogComponent component,
                     bool shouldCache = true);

    /**
     * Deprecated.
     */
    LogstreamBuilder(MessageLogDomain* domain, StringData contextName, LabeledLevel labeledLevel);

    LogstreamBuilder(LogstreamBuilder&& other) = default;
    LogstreamBuilder& operator=(LogstreamBuilder&& other) = default;

    /**
     * Destroys a LogstreamBuilder().  If anything was written to it via stream() or operator<<,
     * constructs a MessageLogDomain::Event and appends it to the associated domain.
     */
    ~LogstreamBuilder();


    /**
     * Sets an optional prefix for the message.
     */
    LogstreamBuilder& setBaseMessage(const std::string& baseMessage) {
        _baseMessage = baseMessage;
        return *this;
    }

    LogstreamBuilder& setIsTruncatable(bool isTruncatable) {
        _isTruncatable = isTruncatable;
        return *this;
    }

    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const char* x) {
        log._coalesceStr(x);
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const std::string& x) {
        log._coalesceStr(x);
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, StringData x) {
        log._coalesceStr(x.toString());  // blah, causes a copy
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, char* x) {
        log._coalesceStr(x);
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, char x) {
        log._coalesceStr(x);
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, int x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, ExitCode x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, long x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, unsigned long x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, unsigned x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, unsigned short x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, double x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, void* x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const void* x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, long long x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, unsigned long long x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, bool x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const BSONObj& x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const BSONElement& x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }

    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const boost::posix_time::ptime& x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }

    template <typename Period>
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const Duration<Period>& d) {
        log._handleStr();
        log.stream() << d;
        return log;
    }

    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, BSONType t) {
        log._handleStr();
        log.stream() << typeName(t);
        return log;
    }

    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, ErrorCodes::Error ec) {
        log._handleStr();
        log.stream() << ErrorCodes::errorString(ec);
        return log;
    }

    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const LogLambda& x) {
        log._handleStr();
        log.stream() << x;
        return log;
    }

    template <class T>
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const T& x) {
        log._handleStr();
        log.stream() << x.toString();
        // Would be good to convert the canonical stringifier from member Foo::toString() to friend operator<<(ostream&, const Foo&)
        //log.stream() << (str::stream() << x);
        return log;
    }

    // Hmmmmm.
    // These are used in a bunch of places (and not just for std::endl).
    // rg -w 'std::((no)?(boolalpha|showbase|showpoint|skipws|uppercase|unitbuf|emit_on_flush)|internal|left|right|dec|hex|oct|fixed|scientific|hexfloat|defaultfloat|ends|flush|endl|flush_emit|resetiosflags|setiosflags|setbase|setfill|setprecision|setw|get_money|put_money|get_time|put_time|quoted)'
    // Probably we can add it to the list of types in Messages, and actually store them properly.
    // Then BSONArray visitation can just ignore them, while String visitation can just apply them as normal (and they will take effect, as normal).
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, std::ostream& (*manip)(std::ostream&)) {
        log._handleStr();
        //log.stream() << manip;
        return log;
    }
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, std::ios_base& (*manip)(std::ios_base&)) {
        log._handleStr();
        //log.stream() << manip;
        return log;
    }

    template <typename OptionalType>
    friend LogstreamBuilder& operator<<(LogstreamBuilder& log, const boost::optional<OptionalType>& optional) {
        if (optional) {
            (log << *optional);
        } else {
            // Don't want this to be subject to string coalescence.
            //(*this << "(nothing)");
            log._handleStr();
            log.stream() << "(nothing)";
        }
        return log;
    }

    /**
     * In addition to appending the message to _domain, write it to the given tee.  May only
     * be called once per instance of LogstreamBuilder.
     */
    friend void operator<<(LogstreamBuilder& log, Tee* tee);



    // Don't know why this doesn't work to replace all the below.
    //template <typename T>
    //friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, T x) {
    //    //return std::move(log << x);
    //    auto& l = log;
    //    return std::move(l << x);
    //}

    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const char* x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const std::string& x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, StringData x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, char* x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, char x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, int x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, ExitCode x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, long x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, unsigned long x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, unsigned x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, unsigned short x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, double x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, void* x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const void* x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, long long x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, unsigned long long x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, bool x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const BSONObj& x) {
        return std::move(log << x);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const BSONElement& x) {
        return std::move(log << x);
    }

    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const boost::posix_time::ptime& x) {
        return std::move(log << x);
    }

    template <typename Period>
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const Duration<Period>& d) {
        return std::move(log << d);
    }

    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, BSONType t) {
        return std::move(log << t);
    }

    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, ErrorCodes::Error ec) {
        return std::move(log << ec);
    }

    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const LogLambda& x) {
        return std::move(log << x);
    }

    template <class T>
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const T& x) {
        return std::move(log << x);
    }

    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, std::ostream& (*manip)(std::ostream&)) {
        return std::move(log << manip);
    }
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, std::ios_base& (*manip)(std::ios_base&)) {
        return std::move(log << manip);
    }

    template <typename OptionalType>
    friend LogstreamBuilder&& operator<<(LogstreamBuilder&& log, const boost::optional<OptionalType>& optional) {
        return std::move(log << optional);
    }

    friend void operator<<(LogstreamBuilder&& log, Tee* tee);


private:
    void makeStream();

    Messages& stream() {
        if (!_os)
            makeStream();
        return *_os;
    }

    MessageLogDomain* _domain;
    std::string _contextName;
    LogSeverity _severity;
    LogComponent _component;
    std::string _baseMessage;
    std::unique_ptr<Messages> _os;
    Tee* _tee;
    bool _isTruncatable = true;
    bool _shouldCache;

    std::string _str;
    bool _strUsed = false;

    template <typename Str>
    void _coalesceStr(const Str& s) {
        _str += s;
        _strUsed = true;
    }

    void _handleStr() {
        if (_strUsed) {
            stream() << _str;
            _str.clear();
            _strUsed = false;
        }
    }

};


}  // namespace logger
}  // namespace mongo
