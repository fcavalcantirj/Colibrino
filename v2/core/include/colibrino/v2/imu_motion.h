/*
 * Colibrino v2 - imu-motion contract (CONTRACT-ONLY this round).
 *
 * Types and prototypes only; there is no implementation in the library yet.
 * Implementation deferred; a differential test against the sticks3
 * MotionController (rate-based mapping, Welford bias calibrator) is planned
 * before any of these prototypes gets a body. Callers must not link against
 * them until then.
 */
#ifndef COLIBRINO_V2_IMU_MOTION_H
#define COLIBRINO_V2_IMU_MOTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Padding-free (20 bytes); carried by value in the profile blob. */
typedef struct {
  uint8_t horizontal_axis; /* source gyro axis 0=X 1=Y 2=Z */
  int8_t horizontal_sign;  /* exactly +1 or -1 after a mount test */
  uint8_t vertical_axis;
  int8_t vertical_sign;
  float deadzone_dps;      /* smooth deadzone radius */
  float pixels_per_degree; /* gain while integrating rate over time */
  float low_pass_alpha;    /* per-sample EMA weight in [0, 1] */
  int8_t max_report;       /* absolute bound of one relative HID report */
  uint8_t reserved[3];
} cv2_motion_config_t;

typedef struct {
  float bias_dps[3];
  float horizontal_filtered;
  float vertical_filtered;
  float horizontal_accumulator;
  float vertical_accumulator;
} cv2_motion_state_t;

/* Streaming stationary-sample bias estimator (Welford, no sample buffer). */
typedef struct {
  uint32_t count;
  float mean[3];
  float m2[3];
} cv2_motion_calibrator_t;

typedef struct {
  int8_t dx;
  int8_t dy;
} cv2_pointer_delta_t;

/* Implementation deferred (contract-only). */
void cv2_motion_init(cv2_motion_state_t *st, const cv2_motion_config_t *cfg);
/* dt_s <= 0 or > 0.2 s must return {0,0} and preserve accumulators. */
cv2_pointer_delta_t cv2_motion_update(cv2_motion_state_t *st,
                                      const cv2_motion_config_t *cfg,
                                      const float gyro_dps[3], float dt_s);
void cv2_motion_calibrator_reset(cv2_motion_calibrator_t *cal);
void cv2_motion_calibrator_add(cv2_motion_calibrator_t *cal,
                               const float gyro_dps[3]);
/* Writes bias only when >= 50 samples and every axis stddev <= max. */
bool cv2_motion_calibrator_finish(const cv2_motion_calibrator_t *cal,
                                  float bias_dps_out[3],
                                  float maximum_stddev_dps);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_IMU_MOTION_H */
