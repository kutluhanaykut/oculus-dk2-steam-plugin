// Unit tests for the parts of the driver that can run without a DK2 or a
// SteamVR installation: packet decoding, sensor fusion, display layout and
// lens distortion.
#include "test_support.h"

#include <cstdio>

namespace dk2vr::tests {

int failures = 0;
int checks = 0;

void reportFailure(const char* file, const int line, const std::string& message)
{
    ++failures;
    std::fprintf(stderr, "FAIL %s:%d\n  %s\n", file, line, message.c_str());
}

} // namespace dk2vr::tests

namespace dk2vr::tests {
void runImuPacketTests();
void runOrientationTests();
void runDisplayGeometryTests();
void runDistortionTests();
} // namespace dk2vr::tests

int main()
{
    using namespace dk2vr::tests;

    runImuPacketTests();
    runOrientationTests();
    runDisplayGeometryTests();
    runDistortionTests();

    if (failures == 0) {
        std::printf("All %d checks passed.\n", checks);
        return 0;
    }
    std::fprintf(stderr, "\n%d of %d checks failed.\n", failures, checks);
    return 1;
}
