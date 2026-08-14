#pragma once

#include <stddef.h>
#include <stdint.h>

namespace colibrino {

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

struct SeparationResult {
  bool usable = false;
  bool passes_signal_gate = false;
  float open_mean = 0.0f;
  float closed_mean = 0.0f;
  float separation_sigma = 0.0f;
  float relative_change = 0.0f;
  float threshold = 0.0f;
  bool closed_is_higher = false;
};

SeparationResult analyzeSeparation(const RunningStats& open,
                                   const RunningStats& closed,
                                   size_t minimum_samples = 20,
                                   float minimum_sigma = 4.0f,
                                   float minimum_relative_change = 0.15f);

class BlinkDetector {
 public:
  void configure(const SeparationResult& calibration);
  void reset();
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

struct FeasibilityConfig {
  uint32_t preparation_ms = 2000;
  uint32_t open_capture_ms = 3000;
  uint32_t closed_capture_ms = 3000;
  uint32_t blink_capture_ms = 12000;
  uint8_t minimum_blinks = 2;
};

struct FeasibilityResult {
  SeparationResult separation{};
  uint32_t open_frames = 0;
  uint32_t closed_frames = 0;
  uint32_t blink_frames = 0;
  uint8_t detected_blinks = 0;
  bool passes = false;
};

class BlinkFeasibilityProtocol {
 public:
  explicit BlinkFeasibilityProtocol(FeasibilityConfig config = {});

  void start(uint32_t now_ms);
  void cancel();
  void tick(uint32_t now_ms);
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
