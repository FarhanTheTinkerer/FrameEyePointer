#include "TestHarness.h"

int main() {
    int run = 0;
    for (const auto& c : test::registry()) {
        int before = test::failures();
        c.fn();
        ++run;
        std::printf("%s %s\n", test::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, test::failures());
    return test::failures() == 0 ? 0 : 1;
}
