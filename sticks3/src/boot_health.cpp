#include "colibrino/boot_health.h"

namespace colibrino {

BootHealthPolicy::BootHealthPolicy(BootHealthConfig config) : config_(config) {}

uint32_t BootHealthPolicy::begin(BootResetClass reset,
                                 uint32_t persisted_attempts) {
  if (reset == BootResetClass::kColdOrExternal) {
    attempts_ = 1;
  } else if (persisted_attempts == UINT32_MAX) {
    attempts_ = UINT32_MAX;
  } else {
    attempts_ = persisted_attempts + 1;
  }
  setup_complete_ = false;
  setup_complete_ms_ = 0;
  healthy_ = false;
  confirmed_ = false;
  return attempts_;
}

bool BootHealthPolicy::shouldRollbackAtBoot() const {
  return attempts_ > config_.max_unhealthy_boots;
}

void BootHealthPolicy::noteSetupComplete(uint32_t now_ms) {
  setup_complete_ = true;
  setup_complete_ms_ = now_ms;
}

BootHealthAction BootHealthPolicy::update(uint32_t now_ms) {
  if (!setup_complete_ || healthy_ || confirmed_) {
    return BootHealthAction::kNone;
  }
  // Unsigned subtraction keeps the window correct across millis() rollover.
  if (now_ms - setup_complete_ms_ < config_.healthy_after_ms) {
    return BootHealthAction::kNone;
  }
  healthy_ = true;
  confirmed_ = true;
  return BootHealthAction::kConfirmImage;
}

bool BootHealthPolicy::shouldRollbackAtDeadline() const {
  return !healthy_ && !confirmed_;
}

void BootHealthPolicy::noteImageConfirmed() {
  healthy_ = true;
  confirmed_ = true;
}

}  // namespace colibrino
