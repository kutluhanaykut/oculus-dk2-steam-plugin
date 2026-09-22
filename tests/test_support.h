#pragma once

#include <cmath>
#include <sstream>
#include <string>

namespace dk2vr::tests {

extern int failures;
extern int checks;

void reportFailure(const char* file, int line, const std::string& message);

inline bool nearlyEqual(const double left, const double right, const double tolerance = 1e-6)
{
    return std::abs(left - right) <= tolerance;
}

} // namespace dk2vr::tests

#define CHECK(condition)                                                          \
    do {                                                                          \
        ++::dk2vr::tests::checks;                                                 \
        if (!(condition)) {                                                       \
            ::dk2vr::tests::reportFailure(__FILE__, __LINE__, #condition);        \
        }                                                                         \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                   \
    do {                                                                          \
        ++::dk2vr::tests::checks;                                                 \
        const double checkActual = static_cast<double>(actual);                   \
        const double checkExpected = static_cast<double>(expected);               \
        if (!::dk2vr::tests::nearlyEqual(checkActual, checkExpected, tolerance)) { \
            std::ostringstream message;                                           \
            message << #actual " == " #expected " (got " << checkActual           \
                    << ", expected " << checkExpected << ")";                     \
            ::dk2vr::tests::reportFailure(__FILE__, __LINE__, message.str());     \
        }                                                                         \
    } while (false)
