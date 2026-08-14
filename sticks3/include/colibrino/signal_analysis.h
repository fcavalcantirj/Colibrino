#pragma once

#include <stddef.h>
#include <stdint.h>

namespace colibrino {

/// Allocation-free streaming mean and sample variance using Welford's method.
class RunningStats {
 public:
  void clear();
  void add(float value);
  size_t count() const { return count_; }
  float mean() const { return mean_; }
  float variance() const;
  float standardDeviation() const;

 private:
  size_t count_ = 0;
  float mean_ = 0.0f;
  float m2_ = 0.0f;
};

/// Calibration evidence for distinguishing open-eye and closed-eye signals.
struct SeparationResult {
  /// True when the two means differ, regardless of acceptance thresholds.
  bool usable = false;
  /// True only after sample-count, sigma, and relative-change gates pass.
  bool passes_signal_gate = false;
  float open_mean = 0.0f;
  float closed_mean = 0.0f;
  float separation_sigma = 0.0f;
  float relative_change = 0.0f;
  /// Midpoint before hysteresis is applied by BlinkDetector.
  float threshold = 0.0f;
  /// Learned polarity; false means a closed eye produces the lower signal.
  bool closed_is_higher = false;
};

/// Compares open and closed baselines without assuming signal polarity.
///
/// `minimum_sigma` uses the difference divided by pooled standard deviation.
/// `minimum_relative_change` uses the larger absolute mean as denominator with
/// a small floor so near-zero signals cannot create an infinite-looking ratio.
SeparationResult analyzeSeparation(const RunningStats& open,
                                   const RunningStats& closed,
                                   size_t minimum_samples = 20,
                                   float minimum_sigma = 4.0f,
                                   float minimum_relative_change = 0.15f);

/// Hysteretic, polarity-independent detector that emits discrete blink events.
///
/// An event is emitted when a closed interval ends after 40-650 ms and at least
/// 250 ms after the previous accepted event. It never emits while calibration
/// is below the signal gate.
class BlinkDetector {
 public:
  /// Copies accepted calibration and derives 12 percent hysteresis thresholds.
  void configure(const SeparationResult& calibration);
  /// Clears temporal state while preserving calibration thresholds.
  void reset();
  /// Returns true only on the open transition that completes a valid blink.
  bool update(uint32_t now_ms, float signal_value);
  bool configured() const { return configured_; }

 private:
  bool isClosed(float value, bool currently_closed) const;

  bool configured_ = false;
  bool closed_is_higher_ = false;
  bool closed_ = false;
  float enter_threshold_ = 0.0f;
  float exit_threshold_ = 0.0f;
  uint32_t closed_since_ms_ = 0;
  uint32_t last_blink_ms_ = 0;
};

/// Human-guided capture stages displayed by the StickS3 application.
enum class FeasibilityStage : uint8_t {
  kIdle,
  kPrepareOpen,
  kCaptureOpen,
  kPrepareClosed,
  kCaptureClosed,
  kPrepareBlinks,
  kCaptureBlinks,
  kResult,
};

/// Durations and minimum blink count for one guided feasibility session.
struct FeasibilityConfig {
  uint32_t preparation_ms = 2000;
  uint32_t open_capture_ms = 3000;
  uint32_t closed_capture_ms = 3000;
  uint32_t blink_capture_ms = 12000;
  uint8_t minimum_blinks = 2;
};

/// Raw counts and derived evidence retained at the result stage.
struct FeasibilityResult {
  SeparationResult separation{};
  uint32_t open_frames = 0;
  uint32_t closed_frames = 0;
  uint32_t blink_frames = 0;
  uint8_t detected_blinks = 0;
  bool passes = false;
};

/// State machine that gathers open, closed, and deliberate-blink evidence.
///
/// `tick` advances time-based stages; `observe` only records valid samples in
/// capture stages. The firmware-level pass is a minimum feasibility signal,
/// not the stricter final TCRT5000 purchase decision documented in PORT_PLAN.
class BlinkFeasibilityProtocol {
 public:
  explicit BlinkFeasibilityProtocol(FeasibilityConfig config = {});

  /// Starts a new session and discards all prior evidence.
  void start(uint32_t now_ms);
  /// Returns to idle and invalidates the prior session result.
  void cancel();
  /// Advances the guided timeline using a monotonic millisecond clock.
  void tick(uint32_t now_ms);
  /// Records one sensor-neutral sample when the active stage consumes it.
  void observe(uint32_t now_ms, bool valid, float signal_value);

  FeasibilityStage stage() const { return stage_; }
  uint32_t stageElapsedMs(uint32_t now_ms) const;
  const FeasibilityResult& result() const { return result_; }
  const BlinkDetector& detector() const { return detector_; }
  BlinkDetector makeDetector() const { return detector_; }

 private:
  void advance(FeasibilityStage next, uint32_t now_ms);
  void finalizeSeparation();

  FeasibilityConfig config_;
  FeasibilityStage stage_ = FeasibilityStage::kIdle;
  uint32_t stage_started_ms_ = 0;
  RunningStats open_{};
  RunningStats closed_{};
  BlinkDetector detector_{};
  FeasibilityResult result_{};
};

const char* feasibilityStageName(FeasibilityStage stage);

}  // namespace colibrino
