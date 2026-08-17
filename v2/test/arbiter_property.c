/*
 * Property oracle: a xorshift32 fuzz over events x contexts x time checks the
 * arbiter's safety invariants on every step:
 *   - action.kind != NONE  =>  the event was non-NULL and every predicate the
 *     grant depends on held (context safe, header valid, producer enabled and
 *     alive, fresh, unexpired, confident, out of cooldown) and
 *     granted_seq == hdr.seq;
 *   - action.fault != NONE  =>  release_all == 1 and kind == NONE;
 *   - the same seed replays to the identical action stream.
 * The model tracks liveness the way a host would (heartbeats it issued plus
 * valid headers it delivered), independently of the arbiter's state struct.
 */
#include <string.h>

#include "colibrino/v2/access_intent.h"
#include "support/intent_fixture.h"
#include "support/unity_main.h"

typedef struct {
  uint32_t s;
} rng_t;

static uint32_t rng_next(rng_t *r) {
  uint32_t x = r->s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  r->s = x;
  return x;
}

static uint32_t rng_below(rng_t *r, uint32_t n) { return rng_next(r) % n; }

typedef struct {
  bool seen[CV2_PRODUCER_COUNT];
  cv2_ms_t last_seen[CV2_PRODUCER_COUNT];
  uint8_t flags[CV2_PRODUCER_COUNT];
  bool have_grant;
  cv2_ms_t last_grant_ms;
  bool queue_pending; /* a queue fault seen and not yet cleared+cooled */
  cv2_ms_t last_queue_fault_ms;
} model_t;

static void model_init(model_t *m) { memset(m, 0, sizeof *m); }

static uint32_t hash_action(uint32_t h, const cv2_action_t *a) {
  h ^= a->kind;
  h *= 16777619u;
  h ^= a->release_all;
  h *= 16777619u;
  h ^= a->fault;
  h *= 16777619u;
  h ^= (uint32_t)(uint16_t)a->dx;
  h *= 16777619u;
  h ^= (uint32_t)(uint16_t)a->dy;
  h *= 16777619u;
  h ^= a->granted_seq;
  h *= 16777619u;
  return h;
}

static void random_context(rng_t *r, cv2_intent_context_t *ctx) {
  /* Bias towards safe contexts so grants actually happen. */
  ctx->armed = rng_below(r, 8u) != 0u;
  ctx->transport_connected = rng_below(r, 10u) != 0u;
  ctx->battery_ok = rng_below(r, 10u) != 0u;
  ctx->calibrated = rng_below(r, 8u) != 0u;
  ctx->queue_fault = rng_below(r, 40u) == 0u;
  ctx->enabled_producers_mask =
      (rng_below(r, 4u) == 0u) ? (uint8_t)rng_below(r, 256u)
                               : CV2_PRODUCER_KNOWN_MASK;
}

static void random_event(rng_t *r, cv2_ms_t now, cv2_intent_event_t *ev,
                         uint32_t *next_seq) {
  memset(ev, 0, sizeof *ev);
  ev->hdr.magic = (rng_below(r, 30u) == 0u) ? (uint16_t)rng_next(r)
                                             : (uint16_t)CV2_EVENT_MAGIC;
  ev->hdr.version = (rng_below(r, 30u) == 0u) ? (uint8_t)rng_next(r)
                                               : (uint8_t)CV2_EVENT_VERSION;
  ev->hdr.kind = (rng_below(r, 20u) == 0u) ? (uint8_t)rng_below(r, 5u)
                                            : (uint8_t)CV2_INTENT_CLICK;
  ev->hdr.producer_id = (rng_below(r, 10u) == 0u)
                            ? (uint16_t)rng_below(r, 10u)
                            : (uint16_t)CV2_PRODUCER_BLINK_CODE;
  ev->hdr.size = (rng_below(r, 30u) == 0u) ? (uint16_t)rng_below(r, 40u)
                                            : (uint16_t)CV2_EVENT_SIZE;
  /* Mostly monotonic sequences; sometimes a replay or a jump backwards. */
  if (rng_below(r, 8u) == 0u) {
    ev->hdr.seq = *next_seq - rng_below(r, 3u);
  } else {
    ev->hdr.seq = (*next_seq)++;
  }
  ev->hdr.t_ms = now - rng_below(r, 160u) + rng_below(r, 4u);
  ev->ttl_ms = (rng_below(r, 20u) == 0u) ? (uint16_t)rng_next(r)
                                          : (uint16_t)rng_below(r, 400u);
  ev->confidence = (uint8_t)rng_below(r, 256u);
  ev->dx = (int16_t)(rng_below(r, 21u) - 10u);
  ev->dy = (int16_t)(rng_below(r, 21u) - 10u);
}

/* One fuzz run; returns the action-stream hash and counts grants so the
 * invariants are provably non-vacuous. steps = number of arbitrate calls. */
static uint32_t fuzz_run(uint32_t seed, uint32_t steps, uint32_t *grants) {
  rng_t r = {seed};
  cv2_intent_state_t st;
  cv2_intent_config_t cfg;
  cv2_intent_context_t ctx;
  model_t m;
  cv2_ms_t now = seed; /* arbitrary start; wraps are covered elsewhere */
  uint32_t next_seq = 1u;
  uint32_t h = 2166136261u;
  uint32_t granted = 0u;

  cv2_intent_init(&st);
  cv2_intent_config_defaults(&cfg);
  model_init(&m);
  fixture_safe_context(&ctx);

  for (uint32_t i = 0; i < steps; ++i) {
    now += rng_below(&r, 300u);
    if (rng_below(&r, 5u) == 0u) {
      random_context(&r, &ctx);
    }
    if (rng_below(&r, 3u) == 0u) {
      const uint16_t pid = (uint16_t)rng_below(&r, 8u);
      const uint8_t flags = (rng_below(&r, 12u) == 0u) ? 1u : 0u;
      cv2_intent_heartbeat(&st, pid, now, flags);
      if (pid < CV2_PRODUCER_COUNT) {
        m.seen[pid] = true;
        m.last_seen[pid] = now;
        m.flags[pid] = flags;
      }
    }

    /* Model the queue latch as the host sees it: latch on a fault, clear
     * once the fault is gone AND one cooldown has elapsed since it was last
     * seen (same order the arbiter applies at the top of a call). */
    if (ctx.queue_fault) {
      m.queue_pending = true;
      m.last_queue_fault_ms = now;
    } else if (m.queue_pending &&
               CV2_MS_DIFF(now, m.last_queue_fault_ms) >= (int32_t)cfg.cooldown_ms) {
      m.queue_pending = false;
    }

    cv2_intent_event_t ev;
    const bool tick = rng_below(&r, 6u) == 0u;
    if (!tick) {
      random_event(&r, now, &ev, &next_seq);
    }
    const cv2_action_t a =
        cv2_intent_arbitrate(&st, &ctx, tick ? NULL : &ev, now, &cfg);
    h = hash_action(h, &a);

    /* fault => release_all and NONE. no fault => no release. */
    if (a.fault != (uint8_t)CV2_FAULT_NONE) {
      TEST_ASSERT_EQUAL_UINT8(1u, a.release_all);
      TEST_ASSERT_EQUAL_UINT8((uint8_t)CV2_INTENT_NONE, a.kind);
      TEST_ASSERT_EQUAL_UINT32(0u, a.granted_seq);
    } else {
      TEST_ASSERT_EQUAL_UINT8(0u, a.release_all);
    }

    const bool ctx_ok = ctx.armed && ctx.transport_connected &&
                        ctx.battery_ok && ctx.calibrated && !ctx.queue_fault;

    if (a.kind != (uint8_t)CV2_INTENT_NONE) {
      ++granted;
      TEST_ASSERT_FALSE_MESSAGE(tick, "grant on a tick");
      TEST_ASSERT_TRUE_MESSAGE(ctx_ok, "grant in unsafe context");
      TEST_ASSERT_FALSE_MESSAGE(m.queue_pending, "grant inside queue-fault latch");
      TEST_ASSERT_EQUAL_INT(CV2_HDR_OK,
                            cv2_header_validate(&ev.hdr, CV2_EVENT_SIZE,
                                                CV2_INTENT_KIND_MAX,
                                                ev.ttl_ms));
      TEST_ASSERT_TRUE(ev.hdr.producer_id >= 1u &&
                       ev.hdr.producer_id < CV2_PRODUCER_COUNT);
      TEST_ASSERT_TRUE_MESSAGE(
          (ctx.enabled_producers_mask & CV2_PRODUCER_BIT(ev.hdr.producer_id)) != 0u,
          "grant from disabled producer");
      TEST_ASSERT_TRUE_MESSAGE(ev.confidence >= cfg.min_confidence,
                               "grant below confidence floor");
      const int32_t age = CV2_MS_DIFF(now, ev.hdr.t_ms);
      TEST_ASSERT_TRUE_MESSAGE(age >= 0 && age <= (int32_t)cfg.max_event_age_ms,
                               "grant of a stale event");
      TEST_ASSERT_FALSE_MESSAGE(
          CV2_MS_AFTER(now, ev.hdr.t_ms + (uint32_t)ev.ttl_ms),
          "grant of an expired event");
      TEST_ASSERT_EQUAL_UINT8(ev.hdr.kind, a.kind);
      TEST_ASSERT_EQUAL_UINT32(ev.hdr.seq, a.granted_seq);
      /* Liveness as the host knows it, before this event refreshed it. */
      const uint16_t pid = ev.hdr.producer_id;
      TEST_ASSERT_TRUE_MESSAGE(m.seen[pid], "grant from unannounced producer");
      TEST_ASSERT_EQUAL_UINT8(0u, m.flags[pid]);
      TEST_ASSERT_TRUE_MESSAGE(
          CV2_MS_DIFF(now, m.last_seen[pid]) <= (int32_t)cfg.producer_timeout_ms,
          "grant from timed-out producer");
      if (a.kind == (uint8_t)CV2_INTENT_CLICK) {
        TEST_ASSERT_TRUE_MESSAGE(
            !m.have_grant ||
                CV2_MS_DIFF(now, m.last_grant_ms) >= (int32_t)cfg.cooldown_ms,
            "click inside cooldown");
        m.have_grant = true;
        m.last_grant_ms = now;
      }
    }

    /* Mirror the heartbeat refresh a valid header performs (only when the
     * arbiter got past the context checks and looked at the header). */
    if (!tick && ctx_ok && !m.queue_pending) {
      if (cv2_header_validate(&ev.hdr, CV2_EVENT_SIZE, CV2_INTENT_KIND_MAX,
                              ev.ttl_ms) == CV2_HDR_OK &&
          ev.hdr.producer_id != CV2_PRODUCER_NONE) {
        m.seen[ev.hdr.producer_id] = true;
        m.last_seen[ev.hdr.producer_id] = now;
      }
    }
  }
  if (grants != NULL) {
    *grants = granted;
  }
  return h;
}

static void test_fuzz_invariants_seed_1(void) {
  uint32_t grants = 0u;
  (void)fuzz_run(0x9E3779B9u, 200000u, &grants);
  TEST_ASSERT_GREATER_THAN_UINT32(100u, grants);
}

static void test_fuzz_invariants_seed_2(void) {
  uint32_t grants = 0u;
  (void)fuzz_run(0x12345678u, 200000u, &grants);
  TEST_ASSERT_GREATER_THAN_UINT32(100u, grants);
}

static void test_fuzz_invariants_many_short_seeds(void) {
  uint32_t total = 0u;
  for (uint32_t seed = 1u; seed <= 64u; ++seed) {
    uint32_t grants = 0u;
    (void)fuzz_run(seed * 2654435761u, 4000u, &grants);
    total += grants;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(64u, total);
}

static void test_deterministic_replay(void) {
  const uint32_t a = fuzz_run(0xC0FFEEu, 50000u, NULL);
  const uint32_t b = fuzz_run(0xC0FFEEu, 50000u, NULL);
  TEST_ASSERT_EQUAL_HEX32(a, b);
  const uint32_t c = fuzz_run(0xC0FFEFu, 50000u, NULL);
  TEST_ASSERT_NOT_EQUAL(a, c);
}

static void test_fuzz_actually_grants(void) {
  /* Guard against a vacuous property: with a safe context, a live producer
   * and clean events the fuzz must grant clicks. */
  rng_t r = {77u};
  cv2_intent_state_t st;
  cv2_intent_config_t cfg;
  cv2_intent_context_t ctx;
  cv2_ms_t now = 0u;
  uint32_t grants = 0u;
  cv2_intent_init(&st);
  cv2_intent_config_defaults(&cfg);
  fixture_safe_context(&ctx);
  for (uint32_t i = 0; i < 2000u; ++i) {
    now += 100u + rng_below(&r, 50u);
    cv2_intent_heartbeat(&st, CV2_PRODUCER_BLINK_CODE, now, 0u);
    const cv2_intent_event_t ev = fixture_click(i + 1u, now);
    const cv2_action_t a = cv2_intent_arbitrate(&st, &ctx, &ev, now, &cfg);
    if (a.kind == (uint8_t)CV2_INTENT_CLICK) {
      ++grants;
    } else {
      TEST_ASSERT_EQUAL_UINT8((uint8_t)CV2_FAULT_COOLDOWN, a.fault);
    }
  }
  TEST_ASSERT_GREATER_THAN_UINT32(50u, grants);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_fuzz_invariants_seed_1);
  RUN_TEST(test_fuzz_invariants_seed_2);
  RUN_TEST(test_fuzz_invariants_many_short_seeds);
  RUN_TEST(test_deterministic_replay);
  RUN_TEST(test_fuzz_actually_grants);
)
