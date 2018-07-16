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

//class RecordId;

namespace logger {

//namespace {

template <class Output>
class Appenderer {
public:
    Appenderer(Output& out) : _out(out) {}

    //template <typename T>
    //void operator()(const T& x) {
    //    _out << x;
    //}

    //void operator()(const int& x) {
    //    _out << x;
    //}

    void operator()(const Timestamp& x) {
        _out << x.toString();
    }

    void operator()(const unsigned long& x) {
        // blahhhhhhhhh
        _out << static_cast<long>(x);
    }

    void operator()(const unsigned long long& x) {
        // blahhhhhhhhh
        _out << static_cast<long long>(x);
    }

#define _FOO(XXX) void operator()(const XXX& x) { _out << x; }

_FOO(std::string)
_FOO(StringData)
_FOO(char)
_FOO(int)
_FOO(ExitCode)
_FOO(long)
//_FOO(unsigned long)
_FOO(unsigned)
_FOO(unsigned short)
_FOO(double)
_FOO(void*)
_FOO(long long)
//_FOO(unsigned long long)
//_FOO(Timestamp)
_FOO(bool)

private:
    Output& _out;
};

// The basic approach from https://gieseanw.wordpress.com/2017/05/03/a-true-heterogeneous-container-in-c/
template<class... T>
struct VariantContainer {
    //template<class V>
    //void visit(V&& visitor) {
    //    for (auto& object : objects) {
    //        //std::visit(visitor, object);
    //        visitor(object);
    //    }
    //}

    template<class V>
    void visit(V&& visitor) const {
        for (const auto& object : objects) {
            std::visit(visitor, object);
            //visitor(static_cast<value_type>(object));
        }
    }

    void clear() {
        objects.clear();
    }

    using value_type = std::variant<T...>;
    std::vector<value_type> objects;

    //template <typename X>
    //template <typename... T>
    VariantContainer<T...>& operator<<(value_type x) {
        objects.emplace_back(x);
        return *this;
    }

    template <typename X>
    VariantContainer<T...>& operator<<(const X& x) {
        std::ostringstream os;
        os << x;
        objects.emplace_back(os.str());
        return *this;
    }

    //template <typename X>
    //void append(const X& x) {
    //    objects.emplace_back(x);
    //}

    void toBSONArray(BSONArrayBuilder& bab) const {
        visit(Appenderer(bab));

        //visit([&](const auto& elem) {
        //    //bab.append(elem);
        //    bab << elem;
        //});
    }

    void toString(std::ostream& os, bool& hadEOL) const {
        visit(Appenderer{os});
        //visit([&](const auto& elem) {
        //    //if (elem.type() == mongo::String) {
        //    //    // avoid being wrapped in \" chars
        //    //    const auto& s = elem.valuestr();
        //    //    hadEOL = StringData(s).endsWith("\n");
        //    //    os << s;
        //    //} else {
        //    //    const auto& s = elem.toString(false, true);
        //    //    hadEOL = StringData(s).endsWith("\n");
        //    //    os << s;
        //    //}
        //    os << elem;
        //});
    }

};

//template <class... T, typename X>
//friend VariantContainer<class... T>& operator<<(const X& x) {
//    objects.emplace_back(x);
//    return *this;
//}

//}

typedef struct VariantContainer<StringData,
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
                                bool//,
                                //const RecordId*
                               > Messages;

// FIXME: convert the above typedef into the below sub-struct,
// so that for types like BSONObj, BSONElement, and other non-POD types, take an unowned copy/shallow copy/pointer/optional reference
// By defining specific operator<< inside the Messages class for them.
// The rest can fall back to the value_type one defined in the parent class.

// For the unowned non-PODs, write a template class Unowned<Foo>, which stores a const pointer to Foo but casts to a const Foo& so it looks and smells like a Foo.
// Then you can add to the VariantContainer template list Unowned<BSONObj>, and it'll store a pointer to the BSONObj, but all the typing will be done as if it was actually a BSONObj.
// Surely there is a boost:: or std:: for this concept already?

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
