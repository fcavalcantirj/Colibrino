/*
 * Arbiter test fixture: a safe context, default config, one healthy blink-code
 * producer and a fresh CLICK event. Every negative test starts from this
 * known-good state and breaks exactly one predicate.
 */
#ifndef COLIBRINO_V2_TEST_INTENT_FIXTURE_H
#define COLIBRINO_V2_TEST_INTENT_FIXTURE_H

#include <string.h>

#include "colibrino/v2/access_intent.h"

typedef struct {
  cv2_intent_state_t st;
  cv2_intent_context_t ctx;
  cv2_intent_config_t cfg;
} intent_fixture_t;

static inline void fixture_safe_context(cv2_intent_context_t *ctx) {
  ctx->armed = true;
  ctx->transport_connected = true;
  ctx->battery_ok = true;
  ctx->queue_fault = false;
  ctx->calibrated = true;
  ctx->enabled_producers_mask =
      (uint8_t)(CV2_PRODUCER_BIT(CV2_PRODUCER_BLINK_CODE) |
                CV2_PRODUCER_BIT(CV2_PRODUCER_IMU_MOTION));
}

/* State initialised and the blink-code producer heartbeat at heartbeat_ms. */
static inline void fixture_init(intent_fixture_t *f, cv2_ms_t heartbeat_ms) {
  memset(f, 0, sizeof *f);
  cv2_intent_init(&f->st);
  cv2_intent_config_defaults(&f->cfg);
  fixture_safe_context(&f->ctx);
  cv2_intent_heartbeat(&f->st, CV2_PRODUCER_BLINK_CODE, heartbeat_ms, 0u);
}

static inline cv2_intent_event_t fixture_click(uint32_t seq, cv2_ms_t t_ms) {
  cv2_intent_event_t ev;
  memset(&ev, 0, sizeof ev);
  ev.hdr.magic = CV2_EVENT_MAGIC;
  ev.hdr.version = CV2_EVENT_VERSION;
  ev.hdr.kind = CV2_INTENT_CLICK;
  ev.hdr.producer_id = CV2_PRODUCER_BLINK_CODE;
  ev.hdr.size = CV2_EVENT_SIZE;
  ev.hdr.seq = seq;
  ev.hdr.t_ms = t_ms;
  ev.ttl_ms = 250u;
  ev.confidence = 255u;
  return ev;
}

static inline cv2_action_t fixture_arbitrate(intent_fixture_t *f,
                                             const cv2_intent_event_t *ev,
                                             cv2_ms_t now_ms) {
  return cv2_intent_arbitrate(&f->st, &f->ctx, ev, now_ms, &f->cfg);
}

/* Both macros evaluate their action expression exactly once. */
#define ASSERT_REJECTED(expr, expected_fault)                            \
  do {                                                                   \
    const cv2_action_t a_ = (expr);                                      \
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CV2_INTENT_NONE, a_.kind);          \
    TEST_ASSERT_EQUAL_UINT8(1u, a_.release_all);                         \
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(expected_fault), a_.fault);        \
    TEST_ASSERT_EQUAL_UINT32(0u, a_.granted_seq);                        \
  } while (0)

#define ASSERT_GRANTED_CLICK(expr, expected_seq)                         \
  do {                                                                   \
    const cv2_action_t a_ = (expr);                                      \
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CV2_INTENT_CLICK, a_.kind);         \
    TEST_ASSERT_EQUAL_UINT8(0u, a_.release_all);                         \
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CV2_FAULT_NONE, a_.fault);          \
    TEST_ASSERT_EQUAL_UINT32((expected_seq), a_.granted_seq);            \
  } while (0)

#endif /* COLIBRINO_V2_TEST_INTENT_FIXTURE_H */
