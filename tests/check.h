#ifndef WINACP_TESTS_CHECK_H
#define WINACP_TESTS_CHECK_H

#include <cstdio>

// Minimal test assertions, so that the tests have no external dependencies.
//
// CHECK records a failure and continues, so that a single run reports every failure. main()
// returns CHECK_RESULT(), whose value is the exit status evaluated by CTest.

inline int &checkFailures() {
    static int failures = 0;
    return failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++checkFailures();                                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                       \
        }                                                                                          \
    } while (false)

#define CHECK_RESULT()                                                                             \
    (std::printf(checkFailures() ? "%d FAILED\n" : "all passed\n", checkFailures()),               \
     checkFailures() ? 1 : 0)

#endif // WINACP_TESTS_CHECK_H
