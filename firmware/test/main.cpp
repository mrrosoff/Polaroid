#include <unity.h>

/*
 * One suite, one file per header under test. PlatformIO builds a test folder
 * as a single binary, so each file exposes a runner instead of its own main().
 */
void runManifestTests();
void runBatteryCurveTests();
void runOverlayTests();
void runStatusCardTests();
void runConfigTests();

int main(int, char**) {
    UNITY_BEGIN();

    runManifestTests();
    runBatteryCurveTests();
    runOverlayTests();
    runStatusCardTests();
    runConfigTests();

    return UNITY_END();
}
