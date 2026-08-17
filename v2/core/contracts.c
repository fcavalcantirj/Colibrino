/*
 * Compile-time contract assertions. This translation unit contains only
 * _Static_assert; the same facts are re-checked at runtime by
 * test/contracts_static.c so a future compiler/ABI change fails loudly.
 */
#include <stddef.h>

#include "colibrino/v2/access_intent.h"
#include "colibrino/v2/blink_code.h"
#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/common.h"
#include "colibrino/v2/feel_defaults.h"
#include "colibrino/v2/imu_motion.h"
#include "colibrino/v2/profile.h"

_Static_assert(sizeof(cv2_event_hdr_t) == CV2_EVENT_HDR_SIZE, "hdr size");
_Static_assert(offsetof(cv2_event_hdr_t, magic) == 0, "hdr.magic offset");
_Static_assert(offsetof(cv2_event_hdr_t, version) == 2, "hdr.version offset");
_Static_assert(offsetof(cv2_event_hdr_t, kind) == 3, "hdr.kind offset");
_Static_assert(offsetof(cv2_event_hdr_t, producer_id) == 4,
               "hdr.producer_id offset");
_Static_assert(offsetof(cv2_event_hdr_t, size) == 6, "hdr.size offset");
_Static_assert(offsetof(cv2_event_hdr_t, seq) == 8, "hdr.seq offset");
_Static_assert(offsetof(cv2_event_hdr_t, t_ms) == 12, "hdr.t_ms offset");

_Static_assert(sizeof(cv2_intent_event_t) == CV2_EVENT_SIZE, "intent size");
_Static_assert(sizeof(cv2_blink_event_t) == CV2_EVENT_SIZE, "blink size");
_Static_assert(sizeof(cv2_gesture_event_t) == CV2_EVENT_SIZE, "gesture size");
_Static_assert(offsetof(cv2_intent_event_t, ttl_ms) == CV2_EVENT_HDR_SIZE,
               "intent body follows header");
_Static_assert(offsetof(cv2_blink_event_t, ttl_ms) == CV2_EVENT_HDR_SIZE,
               "blink body follows header");
_Static_assert(offsetof(cv2_gesture_event_t, ttl_ms) == CV2_EVENT_HDR_SIZE,
               "gesture body follows header");

_Static_assert(sizeof(cv2_imu_sample_t) == CV2_IMU_SAMPLE_SIZE, "sample size");
_Static_assert(sizeof(cv2_action_t) == CV2_ACTION_SIZE, "action size");

_Static_assert(sizeof(cv2_blink_dsp_config_t) == 32, "dsp config size");
_Static_assert(sizeof(cv2_blink_code_config_t) == 20, "code config size");
_Static_assert(sizeof(cv2_intent_config_t) == 8, "intent config size");
_Static_assert(sizeof(cv2_motion_config_t) == 20, "motion config size");
_Static_assert(sizeof(cv2_profile_t) == CV2_PROFILE_SIZE, "profile size");
_Static_assert(offsetof(cv2_profile_t, crc32) == CV2_PROFILE_SIZE - 4u,
               "profile crc is last");

_Static_assert(CV2_PRODUCER_COUNT == 7, "producer count");
_Static_assert(CV2_PRODUCER_COUNT <= 8, "mask is uint8_t");
_Static_assert(CV2_MAX_TTL_MS < 0x7FFFFFFFu, "ttl horizon below 2^31");
_Static_assert(sizeof(cv2_ms_t) == 4, "ms is 32-bit");
