/*
 * Colibrino v2 - AccessIntent arbiter contract.
 *
 * The arbiter is the only path from an intent event to a host action. Owner
 * safety rules: bare words never click; any timeout / disconnect releases all
 * buttons; wake-word + command-window + cooldown are enforced. Every rejected
 * input yields kind NONE, release_all = 1 and an exact fault id, so a host
 * can never hold a button on a stale or unauthorized source.
 *
 * Pure: no globals, no clock read (now_ms is a parameter), no allocation.
 */
#ifndef COLIBRINO_V2_ACCESS_INTENT_H
#define COLIBRINO_V2_ACCESS_INTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colibrino/v2/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Intent event kinds (header.kind). 0 is never valid on the wire. */
enum cv2_intent_kind {
  CV2_INTENT_NONE = 0,
  CV2_INTENT_CLICK = 1,
  CV2_INTENT_POINTER_MOVE = 2,
  CV2_INTENT_KIND_MAX = CV2_INTENT_POINTER_MOVE
};

/* 24-byte intent event: header + 8-byte body. */
typedef struct {
  cv2_event_hdr_t hdr;
  uint16_t ttl_ms;    /* expiry = hdr.t_ms + ttl_ms; <= CV2_MAX_TTL_MS */
  uint8_t confidence; /* 0..255; must reach cfg.min_confidence */
  uint8_t flags;      /* reserved, 0 */
  int16_t dx;         /* POINTER_MOVE only */
  int16_t dy;
} cv2_intent_event_t;

/* Host-side facts the arbiter must never infer on its own. */
typedef struct {
  bool armed;               /* the person has armed pointer/click output */
  bool transport_connected; /* USB HID / BLE link is up */
  bool battery_ok;
  bool queue_fault;         /* event queue overflowed / dropped (latches) */
  bool calibrated;          /* gyro bias accepted */
  uint8_t enabled_producers_mask; /* CV2_PRODUCER_BIT(id) per producer */
} cv2_intent_context_t;

/* Padding-free (8 bytes) so the profile blob carries it by value. */
typedef struct {
  uint16_t producer_timeout_ms; /* heartbeat silence that makes a producer unhealthy */
  uint16_t cooldown_ms;         /* minimum spacing between granted clicks */
  uint16_t max_event_age_ms;    /* now - t_ms above this = stale */
  uint8_t min_confidence;       /* > 0 */
  uint8_t reserved;
} cv2_intent_config_t;

/* Memory horizon of every timestamp the arbiter stores (heartbeats, last
 * ordered event, last granted click, last queue fault). CV2_MS_DIFF is only
 * meaningful while two stamps are < 2^31 ms apart, so each arbitrate call
 * first ages its state: a stored stamp further in the past than this (or
 * whose signed distance has already inverted, which reads as "future") is
 * pinned at exactly this age. The fact it encodes survives - "older than any
 * configured window" - because the horizon is above the largest uint16
 * window a profile can carry, so a timed-out producer or a latched queue
 * fault stays timed out / latched forever and a cooldown or freshness bound
 * is never shortened. Requires the arbiter to be called at least once every
 * 2^31 - 65536 ms (~24.8 days); a host tick trivially satisfies that. */
#define CV2_INTENT_STATE_HORIZON_MS 65536u

typedef struct {
  cv2_ms_t last_seen_ms[CV2_PRODUCER_COUNT]; /* last heartbeat or valid header */
  uint32_t last_seq[CV2_PRODUCER_COUNT];
  cv2_ms_t last_t_ms[CV2_PRODUCER_COUNT];
  bool seen[CV2_PRODUCER_COUNT];    /* last_seen_ms is valid */
  bool ordered[CV2_PRODUCER_COUNT]; /* last_seq / last_t_ms are valid */
  uint8_t fault_flags[CV2_PRODUCER_COUNT]; /* producer self-reported faults */
  cv2_ms_t last_action_ms;  /* last granted click */
  bool have_action;         /* last_action_ms is valid */
  bool holding;             /* a grant is outstanding on the host side */
  bool prev_ctx_ok;         /* context was safe at the previous evaluation */
  bool queue_latched;       /* queue fault seen; clears after one cooldown */
  cv2_ms_t queue_fault_ms;  /* last time ctx.queue_fault was observed true */
} cv2_intent_state_t;

/* Fault ids are fixed values (logged and asserted by tests). */
enum cv2_intent_fault {
  CV2_FAULT_NONE = 0,
  CV2_FAULT_MALFORMED = 1,
  CV2_FAULT_PRODUCER_UNKNOWN = 2,
  CV2_FAULT_PRODUCER_DISABLED = 3,
  CV2_FAULT_STALE = 4,
  CV2_FAULT_EXPIRED = 5,
  CV2_FAULT_DUPLICATE = 6,
  CV2_FAULT_LOW_CONFIDENCE = 7,
  CV2_FAULT_UNARMED = 8,
  CV2_FAULT_PRODUCER_UNHEALTHY = 9,
  CV2_FAULT_QUEUE_FAULT = 10,
  CV2_FAULT_DISCONNECTED = 11,
  CV2_FAULT_LOW_BATTERY = 12,
  CV2_FAULT_COOLDOWN = 13,
  CV2_FAULT_UNCALIBRATED = 14
};

/* Result of one arbitration. In-memory only, never on the wire (12 bytes). */
typedef struct {
  uint8_t kind;        /* enum cv2_intent_kind; NONE on any fault */
  uint8_t release_all; /* 1 = host must release every button now */
  uint8_t fault;       /* enum cv2_intent_fault */
  uint8_t reserved;
  int16_t dx;          /* granted POINTER_MOVE delta */
  int16_t dy;
  uint32_t granted_seq; /* hdr.seq of the granted event, else 0 */
} cv2_action_t;

#define CV2_ACTION_SIZE 12u

void cv2_intent_config_defaults(cv2_intent_config_t *cfg);
void cv2_intent_init(cv2_intent_state_t *st);

/* Producer liveness. Out-of-range producer_id is ignored. fault_flags != 0
 * marks the producer unhealthy until a later heartbeat clears it. */
void cv2_intent_heartbeat(cv2_intent_state_t *st, uint16_t producer_id,
                          cv2_ms_t now_ms, uint8_t fault_flags);

/* Evaluate one event (or a tick when ev == NULL) at now_ms.
 *
 * Every call first ages the stored timestamps to CV2_INTENT_STATE_HORIZON_MS
 * (see above). Order: context faults (queue latched -> disconnected ->
 * battery -> unarmed -> uncalibrated) -> header validity (MALFORMED /
 * PRODUCER_UNKNOWN) -> producer disabled -> duplicate -> stale -> expired ->
 * producer health -> confidence -> cooldown -> grant. A valid header
 * refreshes the producer's heartbeat even when the event is rejected later.
 * A tick evaluates only context + producer health and sets release_all
 * whenever the system is in an unsafe state (idempotent). */
cv2_action_t cv2_intent_arbitrate(cv2_intent_state_t *st,
                                  const cv2_intent_context_t *ctx,
                                  const cv2_intent_event_t *ev, cv2_ms_t now_ms,
                                  const cv2_intent_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_ACCESS_INTENT_H */
