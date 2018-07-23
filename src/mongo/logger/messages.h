/*    Copyright 2018 MongoDB Inc.
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

#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdint>
#include <sstream>
#include <variant>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/timestamp.h"
#include "mongo/db/opdebug_extra.h"
#include "mongo/util/exit_code.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace logger {

// Amazing sorcery.
// From: https://en.cppreference.com/w/cpp/utility/variant/visit
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// The basic C++17 variant approach from https://gieseanw.wordpress.com/2017/05/03/a-true-heterogeneous-container-in-c/
template<class... T>
struct VariantContainer {
    template<class V>
    void visit(V&& visitor) {
        for (auto& object : objects) {
            std::visit(visitor, object);
        }
    }

    template<class V>
    void visit(V&& visitor) const {
        for (const auto& object : objects) {
            std::visit(visitor, object);
        }
    }

    void clear() {
        objects.clear();
    }

    using value_type = std::variant<T...>;
    std::vector<value_type> objects;

    template <typename X>
    VariantContainer<T...>& operator<<(const X& x) {
        objects.emplace_back(x);
        return *this;
    }

    void toBSONArray(BSONArrayBuilder& out) const {
        visit(overloaded {
            [&](const auto& x) { out << x; },
            // Do better than this, eg. { $unsignedLong: x }
            [&](const unsigned long& x) { out << static_cast<long>(x); },
            [&](const unsigned long long& x) { out << static_cast<long long>(x); },
            // Pity, if this was a real object with overloaded operator() then it would be easier to have a single template function for Duration<Period>.  Meh.
            // Possible formats: { $duration: 12, units: "ms" } or { $duration: [ 12, "ms" ] } or { $ms: 12 } or { $secs: 60 }, etc.
            [&](const Nanoseconds& x) { out << BSON("$duration" << x.count() << "$units" << "ns"); },
            [&](const Microseconds& x) { out << BSON("$duration" << x.count() << "$units" << "\xce\xbcs"); },
            [&](const Milliseconds& x) { out << BSON("$duration" << x.count() << "$units" << "ms"); },
            [&](const Seconds& x) { out << BSON("$duration" << x.count() << "$units" << "s"); },
            [&](const Minutes& x) { out << BSON("$duration" << x.count() << "$units" << "min"); },
            [&](const Hours& x) { out << BSON("$duration" << x.count() << "$units" << "hr"); },
            [&](const boost::posix_time::ptime& x) { std::ostringstream os; os << x; out << os.str(); },
            [&](const OpDebugExtra& x) { BSONObjBuilder bob; x.append(bob); out << bob.obj(); },
        });
    }

    void toString(std::ostream& out) const {
        visit(overloaded {
            [&](const auto& x) { out << x; },
            [&](const Timestamp& x) { out << x.toString(); },
            [&](const OpDebugExtra& x) { out << x.report(); },
        });
    }

    std::string toString() const {
        std::ostringstream out;
        toString(out);
        return out.str();
    }
};

struct LogLambda {
    LogLambda(std::function<void(std::ostream&)> ostreamer, std::function<void(BSONArrayBuilder&)> baber)
        : _ostreamer(ostreamer), _baber(baber)
        {}

    std::function<void(std::ostream&)> _ostreamer;
    std::function<void(BSONArrayBuilder&)> _baber;

    friend std::ostream& operator<<(std::ostream& out, const LogLambda& ll) {
        ll._ostreamer(out);
        return out;
    }

    friend BSONArrayBuilder& operator<<(BSONArrayBuilder& out, const LogLambda& ll) {
        ll._baber(out);
        return out;
    }
};

// For the non-POD types below, write a template class Unowned<Foo>, which
// stores a const pointer/reference to Foo but casts to a const Foo& so it
// looks and smells like a Foo.

// Then we can add to the VariantContainer template list Unowned<BSONObj>
// (instead of BSONObj), and it'll store a pointer/ref to the BSONObj, but all
// the typing will be done as if it was actually a BSONObj.

// Surely there is a boost:: or std:: for this Unowned<> concept already?

using Messages = VariantContainer<StringData,
                                  std::string,
                                  const char*,
                                  char,
                                  int,
                                  ExitCode,
                                  long,
                                  unsigned long,
                                  unsigned,
                                  unsigned short,
                                  double,
                                  void*,
                                  long long,
                                  unsigned long long,
                                  Timestamp,
                                  Nanoseconds,
                                  Microseconds,
                                  Milliseconds,
                                  Seconds,
                                  Minutes,
                                  Hours,
                                  boost::posix_time::ptime,
                                  BSONObj,
                                  BSONElement,
                                  OpDebugExtra,
                                  LogLambda,
                                  bool>;

}  // namespace logger
}  // namespace mongo
