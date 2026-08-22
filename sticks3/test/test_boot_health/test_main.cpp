#include <unity.h>

#include <cstdint>

#include "colibrino/boot_health.h"

namespace {

using colibrino::BootHealthAction;
using colibrino::BootHealthConfig;
using colibrino::BootHealthPolicy;
using colibrino::BootResetClass;

void test_cold_boot_starts_a_fresh_budget() {
  BootHealthPolicy policy;
  TEST_ASSERT_EQUAL_UINT32(1, policy.begin(BootResetClass::kColdOrExternal, 7));
  TEST_ASSERT_FALSE(policy.shouldRollbackAtBoot());
  TEST_ASSERT_FALSE(policy.healthy());
  TEST_ASSERT_FALSE(policy.confirmed());
}

void test_warm_boots_accumulate_until_the_budget_is_exhausted() {
  BootHealthPolicy policy;
  TEST_ASSERT_EQUAL_UINT32(1, policy.begin(BootResetClass::kWarm, 0));
  TEST_ASSERT_FALSE(policy.shouldRollbackAtBoot());
  TEST_ASSERT_EQUAL_UINT32(2, policy.begin(BootResetClass::kWarm, 1));
  TEST_ASSERT_FALSE(policy.shouldRollbackAtBoot());
  TEST_ASSERT_EQUAL_UINT32(3, policy.begin(BootResetClass::kWarm, 2));
  TEST_ASSERT_FALSE(policy.shouldRollbackAtBoot());
  TEST_ASSERT_EQUAL_UINT32(4, policy.begin(BootResetClass::kWarm, 3));
  TEST_ASSERT_TRUE(policy.shouldRollbackAtBoot());
  // A saturated persisted counter never wraps back into the budget.
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                           policy.begin(BootResetClass::kWarm, UINT32_MAX));
  TEST_ASSERT_TRUE(policy.shouldRollbackAtBoot());
}

void test_update_confirms_exactly_once_after_the_healthy_window() {
  BootHealthPolicy policy;
  policy.begin(BootResetClass::kWarm, 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kNone),
                        static_cast<int>(policy.update(5000)));
  policy.noteSetupComplete(1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kNone),
                        static_cast<int>(policy.update(10999)));
  TEST_ASSERT_FALSE(policy.healthy());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kConfirmImage),
                        static_cast<int>(policy.update(11000)));
  TEST_ASSERT_TRUE(policy.healthy());
  TEST_ASSERT_TRUE(policy.confirmed());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kNone),
                        static_cast<int>(policy.update(20000)));
}

void test_deadline_rolls_back_only_while_unhealthy() {
  BootHealthPolicy policy;
  policy.begin(BootResetClass::kWarm, 0);
  TEST_ASSERT_TRUE(policy.shouldRollbackAtDeadline());
  policy.noteSetupComplete(0);
  TEST_ASSERT_TRUE(policy.shouldRollbackAtDeadline());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kConfirmImage),
                        static_cast<int>(policy.update(10000)));
  TEST_ASSERT_FALSE(policy.shouldRollbackAtDeadline());
}

void test_external_confirmation_suppresses_confirm_and_deadline() {
  BootHealthPolicy policy;
  policy.begin(BootResetClass::kWarm, 2);
  policy.noteImageConfirmed();
  TEST_ASSERT_TRUE(policy.confirmed());
  TEST_ASSERT_FALSE(policy.shouldRollbackAtDeadline());
  policy.noteSetupComplete(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kNone),
                        static_cast<int>(policy.update(60000)));
}

void test_healthy_window_is_rollover_safe() {
  BootHealthPolicy policy;
  policy.begin(BootResetClass::kWarm, 0);
  policy.noteSetupComplete(UINT32_MAX - 500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kNone),
                        static_cast<int>(policy.update(UINT32_MAX)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kNone),
                        static_cast<int>(policy.update(9498)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kConfirmImage),
                        static_cast<int>(policy.update(9499)));
}

void test_configuration_controls_budget_and_window() {
  BootHealthConfig config;
  config.max_unhealthy_boots = 1;
  config.healthy_after_ms = 0;
  BootHealthPolicy policy(config);
  TEST_ASSERT_EQUAL_UINT32(2, policy.begin(BootResetClass::kWarm, 1));
  TEST_ASSERT_TRUE(policy.shouldRollbackAtBoot());
  policy.begin(BootResetClass::kColdOrExternal, 9);
  TEST_ASSERT_FALSE(policy.shouldRollbackAtBoot());
  policy.noteSetupComplete(42);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kConfirmImage),
                        static_cast<int>(policy.update(42)));
}

void test_begin_clears_a_previous_confirmation() {
  BootHealthPolicy policy;
  policy.begin(BootResetClass::kWarm, 0);
  policy.noteImageConfirmed();
  TEST_ASSERT_FALSE(policy.shouldRollbackAtDeadline());
  policy.begin(BootResetClass::kWarm, 0);
  TEST_ASSERT_FALSE(policy.confirmed());
  TEST_ASSERT_FALSE(policy.healthy());
  TEST_ASSERT_TRUE(policy.shouldRollbackAtDeadline());
}

void test_default_configuration_confirms_before_the_deadline() {
  const BootHealthConfig config;
  TEST_ASSERT_TRUE(config.healthy_after_ms < config.deadline_ms);
  TEST_ASSERT_TRUE(config.max_unhealthy_boots >= 1);
  BootHealthPolicy policy;
  policy.begin(BootResetClass::kWarm, 0);
  // Setup finishing just under the deadline is the worst case the default
  // window must still tolerate: setup + healthy window stays before deadline.
  policy.noteSetupComplete(config.deadline_ms - config.healthy_after_ms - 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BootHealthAction::kConfirmImage),
                        static_cast<int>(policy.update(config.deadline_ms - 1)));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cold_boot_starts_a_fresh_budget);
  RUN_TEST(test_warm_boots_accumulate_until_the_budget_is_exhausted);
  RUN_TEST(test_update_confirms_exactly_once_after_the_healthy_window);
  RUN_TEST(test_deadline_rolls_back_only_while_unhealthy);
  RUN_TEST(test_external_confirmation_suppresses_confirm_and_deadline);
  RUN_TEST(test_healthy_window_is_rollover_safe);
  RUN_TEST(test_configuration_controls_budget_and_window);
  RUN_TEST(test_begin_clears_a_previous_confirmation);
  RUN_TEST(test_default_configuration_confirms_before_the_deadline);
  return UNITY_END();
}
