/*
 * blink-dsp stage 0: per-axis EMA baseline residual channel.
 *
 * Bit-for-bit the sticks3 ImuBlinkDetector baseline: the first sample seeds
 * the baseline and is not judged; afterwards alpha = dt / (tau + dt) with dt
 * clamped to 1..50 ms so a USB or display stall cannot jump the adaptive
 * baseline. Operation order is kept identical to v1 (and the build uses
 * -ffp-contract=off) so the differential test can demand exact equality.
 */
#include <math.h>

#include "colibrino/v2/blink_dsp.h"

void cv2_blink_channel_imu_reset(cv2_blink_channel_imu_state_t *st) {
  if (st == NULL) {
    return;
  }
  st->initialized = false;
  st->baseline_dps[0] = 0.0f;
  st->baseline_dps[1] = 0.0f;
  st->baseline_dps[2] = 0.0f;
  st->previous_ms = 0u;
}

static float magnitude(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}

cv2_blink_channel_out_t cv2_blink_channel_imu_update(
    cv2_blink_channel_imu_state_t *st, const cv2_blink_dsp_config_t *cfg,
    cv2_ms_t t_ms, const float gyro_dps[3]) {
  cv2_blink_channel_out_t out;
  out.valid = false;
  out.residual_dps = 0.0f;
  out.head_rate_dps = 0.0f;
  if (st == NULL || cfg == NULL || gyro_dps == NULL) {
    return out;
  }
  if (!st->initialized) {
    st->baseline_dps[0] = gyro_dps[0];
    st->baseline_dps[1] = gyro_dps[1];
    st->baseline_dps[2] = gyro_dps[2];
    st->initialized = true;
    st->previous_ms = t_ms;
    return out;
  }

  const uint32_t elapsed_ms = t_ms - st->previous_ms;
  st->previous_ms = t_ms;
  uint32_t bounded = elapsed_ms;
  if (bounded > 50u) {
    bounded = 50u;
  }
  if (bounded < 1u) {
    bounded = 1u;
  }
  const float bounded_dt_ms = (float)bounded;
  const float alpha =
      bounded_dt_ms / (cfg->baseline_time_constant_ms + bounded_dt_ms);
  st->baseline_dps[0] += alpha * (gyro_dps[0] - st->baseline_dps[0]);
  st->baseline_dps[1] += alpha * (gyro_dps[1] - st->baseline_dps[1]);
  st->baseline_dps[2] += alpha * (gyro_dps[2] - st->baseline_dps[2]);

  const float rx = gyro_dps[0] - st->baseline_dps[0];
  const float ry = gyro_dps[1] - st->baseline_dps[1];
  const float rz = gyro_dps[2] - st->baseline_dps[2];
  out.valid = true;
  out.residual_dps = magnitude(rx, ry, rz);
  out.head_rate_dps = magnitude(gyro_dps[0], gyro_dps[1], gyro_dps[2]);
  return out;
}
