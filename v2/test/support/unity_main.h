/*
 * Shared Unity boilerplate for every v2 test executable.
 *
 *   #include "support/unity_main.h"
 *   static void test_x(void) { ... }
 *   CV2_UNITY_MAIN(RUN_TEST(test_x); RUN_TEST(test_y);)
 *
 * setUp/tearDown are intentionally empty: every test builds its own state
 * on the stack so no fixture leaks between cases.
 */
#ifndef COLIBRINO_V2_TEST_UNITY_MAIN_H
#define COLIBRINO_V2_TEST_UNITY_MAIN_H

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

#define CV2_UNITY_MAIN(body) \
  int main(void) {           \
    UNITY_BEGIN();           \
    body                     \
    return UNITY_END();      \
  }

#endif /* COLIBRINO_V2_TEST_UNITY_MAIN_H */
