#pragma once

#include <cstdint>

namespace colibrino {

// Why a boot arrived. Cold resets (power-on, external reset pin) start a fresh
// attempt budget; every other reset (software, panic, watchdog, brownout)
// counts toward the budget because it may be the same image failing again.
enum class BootResetClass : uint8_t { kColdOrExternal, kWarm };

enum class BootHealthAction : uint8_t { kNone, kConfirmImage, kRollbackImage };

struct BootHealthConfig {
  // Boots that may fail to reach a healthy state before the next boot asks
  // for a rollback to the previously working image.
  uint32_t max_unhealthy_boots = 3;
  // Loop time after setup() completed before the image confirms itself.
  uint32_t healthy_after_ms = 10000;
  // Wall time from boot after which an unconfirmed image rolls back; covers
  // hangs that never reach the loop (the deadline is armed by the adapter).
  uint32_t deadline_ms = 45000;
};

// Portable decision logic for OTA image confirmation and self-rollback. It
// deliberately contains no IDF/Arduino calls: the board adapter persists the
// attempt counter (RTC memory), asks the bootloader API whether a rollback
// target exists, and performs the confirm/rollback side effects.
class BootHealthPolicy {
 public:
  explicit BootHealthPolicy(BootHealthConfig config = BootHealthConfig{});

  // Boot start. `persisted_attempts` is the count of consecutive unhealthy
  // boots recorded by earlier boots (0 when unknown). Returns the attempt
  // number of this boot, which the adapter persists before anything risky.
  uint32_t begin(BootResetClass reset, uint32_t persisted_attempts);

  // True when this boot has exhausted the attempt budget and should return to
  // the previous image immediately (if the adapter finds a rollback target).
  bool shouldRollbackAtBoot() const;

  void noteSetupComplete(uint32_t now_ms);

  // Loop tick. Returns kConfirmImage exactly once, when the healthy window
  // has elapsed after setup completion; kNone otherwise.
  BootHealthAction update(uint32_t now_ms);

  // Deadline tick: true while the image is neither healthy nor confirmed.
  bool shouldRollbackAtDeadline() const;

  // External confirmation (for example an authenticated OTA starting proves
  // the image is serviceable). Suppresses later confirm/rollback actions.
  void noteImageConfirmed();

  uint32_t attempts() const { return attempts_; }
  bool setupComplete() const { return setup_complete_; }
  bool healthy() const { return healthy_; }
  bool confirmed() const { return confirmed_; }
  const BootHealthConfig& config() const { return config_; }

 private:
  BootHealthConfig config_;
  uint32_t attempts_ = 0;
  bool setup_complete_ = false;
  uint32_t setup_complete_ms_ = 0;
  bool healthy_ = false;
  bool confirmed_ = false;
};

}  // namespace colibrino
