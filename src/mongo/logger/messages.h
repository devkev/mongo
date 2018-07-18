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
#include "mongo/util/exit_code.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace logger {

// Amazing black magic.
// From: https://en.cppreference.com/w/cpp/utility/variant/visit
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


// The basic approach from https://gieseanw.wordpress.com/2017/05/03/a-true-heterogeneous-container-in-c/
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
            [&](const auto& elem) { out << elem; },
            // Do better than this.
            [&](const unsigned long& elem) { out << static_cast<long>(elem); },
            [&](const unsigned long long& elem) { out << static_cast<long long>(elem); },
            // Pity, if this was a real object with overloaded operator() then it would be easier to have a single template function for Duration<Period>.  Meh.
            // Do better than just stringifying.
            // eg. something like { $duration: 12, units: "ms" } or { $duration: [ 12, "ms" ] } or { $ms: 12 } or { $secs: 60 }, etc.
            [&](const Nanoseconds& elem) { BSONObjBuilder bob; bob << "$duration" << elem.count(); bob << "$units" << "ns"; out << bob.obj(); },
            [&](const Microseconds& elem) { BSONObjBuilder bob; bob << "$duration" << elem.count(); bob << "$units" << "\xce\xbcs"; out << bob.obj(); },
            [&](const Milliseconds& elem) { BSONObjBuilder bob; bob << "$duration" << elem.count(); bob << "$units" << "ms"; out << bob.obj(); },
            [&](const Seconds& elem) { BSONObjBuilder bob; bob << "$duration" << elem.count(); bob << "$units" << "s"; out << bob.obj(); },
            [&](const Minutes& elem) { BSONObjBuilder bob; bob << "$duration" << elem.count(); bob << "$units" << "min"; out << bob.obj(); },
            [&](const Hours& elem) { BSONObjBuilder bob; bob << "$duration" << elem.count(); bob << "$units" << "hr"; out << bob.obj(); },
            [&](const boost::posix_time::ptime& elem) { std::ostringstream os; os << elem; out << os.str(); },
        });
    }

    void toString(std::ostream& out) const {
        visit(overloaded {
            [&](const auto& elem) { out << elem; },
            [&](const Timestamp& elem) { out << elem.toString(); },
        });
    }

    std::string toString() const {
        std::ostringstream out;
        toString(out);
        return out.str();
    }
};

using Messages = VariantContainer<StringData,
                                  std::string,
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
                                  bool>;

// FIXME: convert the above typedef into the below sub-struct,
// so that for types like BSONObj, BSONElement, and other non-POD types, take an unowned copy/shallow copy/pointer/optional reference
// By defining specific operator<< inside the Messages class for them.
// The rest can fall back to the value_type one defined in the parent class.

// For the unowned non-PODs, write a template class Unowned<Foo>, which stores a const pointer to Foo but casts to a const Foo& so it looks and smells like a Foo.
// Then you can add to the VariantContainer template list Unowned<BSONObj>, and it'll store a pointer to the BSONObj, but all the typing will be done as if it was actually a BSONObj.
// So probably not necessary to convert this to a sub-class (as below).
// Surely there is a boost:: or std:: for this Unowned<> concept already?

// Need to also do something about the fucking Duration<Period> types.

//struct Messages : public VariantContainer<StringData,
//                                char,
//                                int,
//                                ExitCode,
//                                long,
//                                unsigned long,
//                                unsigned,
//                                unsigned short,
//                                double,
//                                const void*,
//                                long long,
//                                unsigned long long,
//                                bool
//                               > {
//};
//
//template <typename X>
//friend Messages& operator<<(Messages& messages, const X& x) {
//    messages.append(x);
//    //objects.emplace_back(x);
//    return *this;
//}


}  // namespace logger
}  // namespace mongo
