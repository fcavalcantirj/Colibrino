#include "colibrino/signal_analysis.h"

#include <algorithm>
#include <cmath>

namespace colibrino {

void RunningStats::clear() {
  count_ = 0;
  mean_ = 0.0f;
  m2_ = 0.0f;
}

void RunningStats::add(float value) {
  // Welford's recurrence avoids the catastrophic cancellation of E[x^2]-E[x]^2
  // and does not retain the capture window in RAM.
  ++count_;
  const float delta = value - mean_;
  mean_ += delta / static_cast<float>(count_);
  const float delta2 = value - mean_;
  m2_ += delta * delta2;
}

float RunningStats::variance() const {
  return count_ > 1 ? m2_ / static_cast<float>(count_ - 1) : 0.0f;
}

float RunningStats::standardDeviation() const {
  return std::sqrt(variance());
}

SeparationResult analyzeSeparation(const RunningStats& open,
                                   const RunningStats& closed,
                                   size_t minimum_samples,
                                   float minimum_sigma,
                                   float minimum_relative_change) {
  SeparationResult result;
  result.open_mean = open.mean();
  result.closed_mean = closed.mean();
  result.threshold = (result.open_mean + result.closed_mean) * 0.5f;
  result.closed_is_higher = result.closed_mean > result.open_mean;

  if (open.count() < minimum_samples || closed.count() < minimum_samples) {
    // Means are still returned for diagnostics, but sparse captures can never
    // unlock runtime blink detection.
    return result;
  }

  const float difference = std::fabs(result.closed_mean - result.open_mean);
  const float pooled_deviation =
      std::sqrt((open.variance() + closed.variance()) * 0.5f);
  result.separation_sigma =
      difference / std::max(0.0001f, pooled_deviation);
  result.relative_change =
      difference /
      std::max(0.05f, std::max(std::fabs(result.open_mean),
                              std::fabs(result.closed_mean)));
  result.usable = difference > 0.0001f;
  result.passes_signal_gate =
      result.usable && result.separation_sigma >= minimum_sigma &&
      result.relative_change >= minimum_relative_change;
  return result;
}

void BlinkDetector::configure(const SeparationResult& calibration) {
  configured_ = calibration.passes_signal_gate;
  closed_is_higher_ = calibration.closed_is_higher;
  closed_ = false;
  closed_since_ms_ = 0;
  last_blink_ms_ = 0;

  // Separate enter/exit thresholds prevent noise near the midpoint from
  // manufacturing rapid close/open transitions.
  const float difference =
      std::fabs(calibration.closed_mean - calibration.open_mean);
  const float hysteresis = difference * 0.12f;
  if (closed_is_higher_) {
    enter_threshold_ = calibration.threshold + hysteresis;
    exit_threshold_ = calibration.threshold - hysteresis;
  } else {
    enter_threshold_ = calibration.threshold - hysteresis;
    exit_threshold_ = calibration.threshold + hysteresis;
  }
}

void BlinkDetector::reset() {
  closed_ = false;
  closed_since_ms_ = 0;
  last_blink_ms_ = 0;
}

bool BlinkDetector::update(uint32_t now_ms, float signal_value) {
  if (!configured_) {
    return false;
  }

  const bool next_closed = isClosed(signal_value, closed_);
  if (!closed_ && next_closed) {
    closed_ = true;
    closed_since_ms_ = now_ms;
    return false;
  }

  if (closed_ && !next_closed) {
    closed_ = false;
    const uint32_t duration = now_ms - closed_since_ms_;
    // Reject electrical glitches and long closures that are unlikely to be a
    // deliberate human blink. The refractory gate prevents double clicks.
    const bool duration_ok = duration >= 40 && duration <= 650;
    const bool refractory_ok =
        last_blink_ms_ == 0 || now_ms - last_blink_ms_ >= 250;
    if (duration_ok && refractory_ok) {
      last_blink_ms_ = now_ms;
      return true;
    }
  }
  return false;
}

bool BlinkDetector::isClosed(float value, bool currently_closed) const {
  const float threshold = currently_closed ? exit_threshold_ : enter_threshold_;
  return closed_is_higher_ ? value >= threshold : value <= threshold;
}

BlinkFeasibilityProtocol::BlinkFeasibilityProtocol(FeasibilityConfig config)
    : config_(config) {}

void BlinkFeasibilityProtocol::start(uint32_t now_ms) {
  open_.clear();
  closed_.clear();
  detector_ = {};
  result_ = {};
  advance(FeasibilityStage::kPrepareOpen, now_ms);
}

void BlinkFeasibilityProtocol::cancel() {
  stage_ = FeasibilityStage::kIdle;
  result_ = {};
  open_.clear();
  closed_.clear();
  detector_ = {};
}

void BlinkFeasibilityProtocol::tick(uint32_t now_ms) {
  // Each transition records `now_ms`; delayed loops do not retroactively run
  // multiple capture stages with the same sample.
  const uint32_t elapsed = stageElapsedMs(now_ms);
  switch (stage_) {
    case FeasibilityStage::kPrepareOpen:
      if (elapsed >= config_.preparation_ms) {
        advance(FeasibilityStage::kCaptureOpen, now_ms);
      }
      break;
    case FeasibilityStage::kCaptureOpen:
      if (elapsed >= config_.open_capture_ms) {
        advance(FeasibilityStage::kPrepareClosed, now_ms);
      }
      break;
    case FeasibilityStage::kPrepareClosed:
      if (elapsed >= config_.preparation_ms) {
        advance(FeasibilityStage::kCaptureClosed, now_ms);
      }
      break;
    case FeasibilityStage::kCaptureClosed:
      if (elapsed >= config_.closed_capture_ms) {
        finalizeSeparation();
        advance(FeasibilityStage::kPrepareBlinks, now_ms);
      }
      break;
    case FeasibilityStage::kPrepareBlinks:
      if (elapsed >= config_.preparation_ms) {
        detector_.reset();
        advance(FeasibilityStage::kCaptureBlinks, now_ms);
      }
      break;
    case FeasibilityStage::kCaptureBlinks:
      if (elapsed >= config_.blink_capture_ms) {
        result_.passes = result_.separation.passes_signal_gate &&
                         result_.detected_blinks >= config_.minimum_blinks;
        advance(FeasibilityStage::kResult, now_ms);
      }
      break;
    default:
      break;
  }
}

void BlinkFeasibilityProtocol::observe(uint32_t now_ms, bool valid,
                                       float signal_value) {
  // Invalid frames remain absent from statistics. Frame counts in the result
  // therefore provide direct evidence of how much usable data each stage saw.
  if (!valid) {
    return;
  }
  switch (stage_) {
    case FeasibilityStage::kCaptureOpen:
      open_.add(signal_value);
      ++result_.open_frames;
      break;
    case FeasibilityStage::kCaptureClosed:
      closed_.add(signal_value);
      ++result_.closed_frames;
      break;
    case FeasibilityStage::kCaptureBlinks:
      ++result_.blink_frames;
      if (detector_.update(now_ms, signal_value) &&
          result_.detected_blinks < UINT8_MAX) {
        ++result_.detected_blinks;
      }
      break;
    default:
      break;
  }
}

uint32_t BlinkFeasibilityProtocol::stageElapsedMs(uint32_t now_ms) const {
  return now_ms - stage_started_ms_;
}

void BlinkFeasibilityProtocol::advance(FeasibilityStage next,
                                       uint32_t now_ms) {
  stage_ = next;
  stage_started_ms_ = now_ms;
}

void BlinkFeasibilityProtocol::finalizeSeparation() {
  // Detector configuration may intentionally remain disabled when calibration
  // fails. The subsequent blink stage still gathers a frame count for diagnosis.
  result_.separation = analyzeSeparation(open_, closed_);
  detector_.configure(result_.separation);
}

const char* feasibilityStageName(FeasibilityStage stage) {
  switch (stage) {
    case FeasibilityStage::kIdle:
      return "IDLE";
    case FeasibilityStage::kPrepareOpen:
      return "PREPARE_OPEN";
    case FeasibilityStage::kCaptureOpen:
      return "KEEP_EYE_OPEN";
    case FeasibilityStage::kPrepareClosed:
      return "PREPARE_CLOSED";
    case FeasibilityStage::kCaptureClosed:
      return "KEEP_EYE_CLOSED";
    case FeasibilityStage::kPrepareBlinks:
      return "PREPARE_BLINKS";
    case FeasibilityStage::kCaptureBlinks:
      return "BLINK_REPEATEDLY";
    case FeasibilityStage::kResult:
      return "RESULT";
  }
  return "UNKNOWN";
}

}  // namespace colibrino
