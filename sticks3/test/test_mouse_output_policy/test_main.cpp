#include <unity.h>

#include "colibrino/mouse_output_policy.h"

namespace {

using colibrino::DeliberateHoldGate;
using colibrino::MouseLockReason;
using colibrino::MouseOutputPolicy;
using colibrino::MouseTransport;

void makeReady(MouseOutputPolicy& policy, bool usb, bool ble) {
  policy.setMouseMode(true);
  policy.setCalibrated(true);
  policy.setImuFresh(true);
  policy.setTransportReadiness(usb, ble);
  MouseLockReason ignored;
  policy.takeReleaseRequest(ignored);
}

void test_boot_is_locked_and_transportless() {
  MouseOutputPolicy policy;
  TEST_ASSERT_FALSE(policy.armed());
  TEST_ASSERT_FALSE(policy.canReport());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MouseTransport::kNone),
                        static_cast<int>(policy.selectedTransport()));
}

void test_connection_without_secure_bond_cannot_arm() {
  MouseOutputPolicy policy;
  policy.setMouseMode(true);
  policy.setCalibrated(true);
  policy.setImuFresh(true);
  policy.setTransportReadiness(false, false);
  TEST_ASSERT_FALSE(policy.requestArm());
  TEST_ASSERT_FALSE(policy.routeMotion(20, 10, 100).ready);
}

void test_mode_entry_hold_must_be_released() {
  DeliberateHoldGate gate;
  gate.blockUntilRelease();
  TEST_ASSERT_FALSE(gate.trigger(true, true));
  gate.observe(false);
  TEST_ASSERT_TRUE(gate.trigger(true, true));
  TEST_ASSERT_FALSE(gate.trigger(true, true));
}

void test_reports_require_every_safety_gate() {
  MouseOutputPolicy policy;
  policy.setTransportReadiness(true, false);
  TEST_ASSERT_FALSE(policy.requestArm());
  policy.setMouseMode(true);
  TEST_ASSERT_FALSE(policy.requestArm());
  policy.setCalibrated(true);
  TEST_ASSERT_FALSE(policy.requestArm());
  policy.setImuFresh(true);
  TEST_ASSERT_TRUE(policy.requestArm());
  const auto report = policy.routeMotion(4, -3, 10);
  TEST_ASSERT_TRUE(report.ready);
  TEST_ASSERT_EQUAL_INT8(4, report.x);
  TEST_ASSERT_EQUAL_INT8(-3, report.y);
}

void test_usb_is_the_only_selected_transport_when_both_ready() {
  MouseOutputPolicy policy;
  makeReady(policy, true, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MouseTransport::kUsb),
                        static_cast<int>(policy.selectedTransport()));
  TEST_ASSERT_TRUE(policy.requestArm());
  const auto report = policy.routeMotion(2, 3, 0);
  TEST_ASSERT_TRUE(report.ready);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MouseTransport::kUsb),
                        static_cast<int>(report.transport));
}

void test_transport_loss_locks_once_and_reconnect_stays_locked() {
  MouseOutputPolicy policy;
  makeReady(policy, false, true);
  TEST_ASSERT_TRUE(policy.requestArm());
  policy.setTransportReadiness(false, false);
  TEST_ASSERT_FALSE(policy.armed());
  MouseLockReason reason;
  TEST_ASSERT_TRUE(policy.takeReleaseRequest(reason));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MouseLockReason::kTransportTopology),
                        static_cast<int>(reason));
  TEST_ASSERT_FALSE(policy.takeReleaseRequest(reason));
  policy.setTransportReadiness(false, true);
  TEST_ASSERT_FALSE(policy.armed());
  TEST_ASSERT_FALSE(policy.canReport());
}

void test_mode_ota_auth_report_and_imu_faults_lock() {
  const MouseLockReason reasons[] = {
      MouseLockReason::kModeExit,       MouseLockReason::kOta,
      MouseLockReason::kAuthentication, MouseLockReason::kReportFailure,
      MouseLockReason::kImuTimeout,     MouseLockReason::kInvalidTiming,
  };
  for (const MouseLockReason expected : reasons) {
    MouseOutputPolicy policy;
    makeReady(policy, true, false);
    TEST_ASSERT_TRUE(policy.requestArm());
    if (expected == MouseLockReason::kModeExit) {
      policy.setMouseMode(false);
    } else if (expected == MouseLockReason::kAuthentication) {
      policy.noteAuthenticationFailure();
    } else if (expected == MouseLockReason::kReportFailure) {
      policy.noteReportFailure();
    } else if (expected == MouseLockReason::kImuTimeout) {
      policy.setImuFresh(false);
    } else if (expected == MouseLockReason::kInvalidTiming) {
      policy.forceLock(expected);
    } else {
      policy.forceLock(expected);
    }
    TEST_ASSERT_FALSE(policy.armed());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
                          static_cast<int>(policy.lastLockReason()));
    TEST_ASSERT_FALSE(policy.routeMotion(10, 10, 20).ready);
  }
}

void test_usb_topology_switch_locks_before_ble_fallback() {
  MouseOutputPolicy policy;
  makeReady(policy, true, true);
  TEST_ASSERT_TRUE(policy.requestArm());
  policy.setTransportReadiness(false, true);
  TEST_ASSERT_FALSE(policy.armed());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MouseTransport::kBle),
                        static_cast<int>(policy.selectedTransport()));
}

void test_ble_accumulator_is_bounded_and_drops_overflow() {
  MouseOutputPolicy policy;
  makeReady(policy, false, true);
  TEST_ASSERT_TRUE(policy.requestArm());
  TEST_ASSERT_FALSE(policy.routeMotion(60, -60, 100).ready);
  TEST_ASSERT_FALSE(policy.routeMotion(60, -60, 104).ready);
  const auto bounded = policy.routeMotion(60, -60, 108);
  TEST_ASSERT_TRUE(bounded.ready);
  TEST_ASSERT_EQUAL_INT8(60, bounded.x);
  TEST_ASSERT_EQUAL_INT8(-60, bounded.y);
  TEST_ASSERT_FALSE(policy.routeMotion(0, 0, 116).ready);
}

void test_lock_clears_pending_ble_motion() {
  MouseOutputPolicy policy;
  makeReady(policy, false, true);
  TEST_ASSERT_TRUE(policy.requestArm());
  TEST_ASSERT_FALSE(policy.routeMotion(25, 10, 100).ready);
  policy.forceLock(MouseLockReason::kPhysical);
  TEST_ASSERT_TRUE(policy.requestArm());
  TEST_ASSERT_FALSE(policy.routeMotion(0, 0, 110).ready);
}

void test_ble_cadence_is_wraparound_safe() {
  MouseOutputPolicy policy;
  makeReady(policy, false, true);
  TEST_ASSERT_TRUE(policy.requestArm());
  TEST_ASSERT_FALSE(policy.routeMotion(5, 0, UINT32_MAX - 3).ready);
  const auto report = policy.routeMotion(5, 0, 4);
  TEST_ASSERT_TRUE(report.ready);
  TEST_ASSERT_EQUAL_INT8(10, report.x);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_is_locked_and_transportless);
  RUN_TEST(test_connection_without_secure_bond_cannot_arm);
  RUN_TEST(test_mode_entry_hold_must_be_released);
  RUN_TEST(test_reports_require_every_safety_gate);
  RUN_TEST(test_usb_is_the_only_selected_transport_when_both_ready);
  RUN_TEST(test_transport_loss_locks_once_and_reconnect_stays_locked);
  RUN_TEST(test_mode_ota_auth_report_and_imu_faults_lock);
  RUN_TEST(test_usb_topology_switch_locks_before_ble_fallback);
  RUN_TEST(test_ble_accumulator_is_bounded_and_drops_overflow);
  RUN_TEST(test_lock_clears_pending_ble_motion);
  RUN_TEST(test_ble_cadence_is_wraparound_safe);
  return UNITY_END();
}
