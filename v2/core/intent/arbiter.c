/*
 * AccessIntent arbiter.
 *
 * Fail-closed by construction: the only way out of cv2_intent_arbitrate with
 * kind != NONE is the final grant line, reached after every context, header,
 * ordering, freshness, health, confidence and cooldown predicate held. Every
 * other exit is a NONE action with release_all = 1 and the exact fault, so a
 * host that mirrors release_all can never keep a button pressed on a stale,
 * duplicated, unhealthy or unauthorized source.
 */
#include "colibrino/v2/access_intent.h"

#include "colibrino/v2/feel_defaults.h"

void cv2_intent_config_defaults(cv2_intent_config_t *cfg) {
  if (cfg == NULL) {
    return;
  }
  cfg->producer_timeout_ms = (uint16_t)CV2_FEEL_INTENT_PRODUCER_TIMEOUT_MS;
  cfg->cooldown_ms = (uint16_t)CV2_FEEL_INTENT_COOLDOWN_MS;
  cfg->max_event_age_ms = (uint16_t)CV2_FEEL_INTENT_MAX_EVENT_AGE_MS;
  cfg->min_confidence = (uint8_t)CV2_FEEL_INTENT_MIN_CONFIDENCE;
  cfg->reserved = 0u;
}

void cv2_intent_init(cv2_intent_state_t *st) {
  if (st == NULL) {
    return;
  }
  for (size_t i = 0; i < (size_t)CV2_PRODUCER_COUNT; ++i) {
    st->last_seen_ms[i] = 0u;
    st->last_seq[i] = 0u;
    st->last_t_ms[i] = 0u;
    st->seen[i] = false;
    st->ordered[i] = false;
    st->fault_flags[i] = 0u;
  }
  st->last_action_ms = 0u;
  st->have_action = false;
  st->holding = false;
  st->prev_ctx_ok = false;
  st->queue_latched = false;
  st->queue_fault_ms = 0u;
}

void cv2_intent_heartbeat(cv2_intent_state_t *st, uint16_t producer_id,
                          cv2_ms_t now_ms, uint8_t fault_flags) {
  if (st == NULL || producer_id >= (uint16_t)CV2_PRODUCER_COUNT) {
    return;
  }
  st->last_seen_ms[producer_id] = now_ms;
  st->seen[producer_id] = true;
  st->fault_flags[producer_id] = fault_flags;
}

static cv2_action_t fault_action(cv2_intent_state_t *st, uint8_t fault) {
  cv2_action_t a;
  a.kind = (uint8_t)CV2_INTENT_NONE;
  a.release_all = 1u;
  a.fault = fault;
  a.reserved = 0u;
  a.dx = 0;
  a.dy = 0;
  a.granted_seq = 0u;
  if (st != NULL) {
    st->holding = false;
  }
  return a;
}

static cv2_action_t no_action(void) {
  cv2_action_t a;
  a.kind = (uint8_t)CV2_INTENT_NONE;
  a.release_all = 0u;
  a.fault = (uint8_t)CV2_FAULT_NONE;
  a.reserved = 0u;
  a.dx = 0;
  a.dy = 0;
  a.granted_seq = 0u;
  return a;
}

/* Context faults in priority order. Also advances the queue latch: a queue
 * fault latches and clears only after ctx.queue_fault has been false for one
 * full cooldown, so a burst of drops cannot be followed by an instant click. */
static uint8_t context_fault(cv2_intent_state_t *st,
                             const cv2_intent_context_t *ctx, cv2_ms_t now_ms,
                             const cv2_intent_config_t *cfg) {
  if (ctx->queue_fault) {
    st->queue_latched = true;
    st->queue_fault_ms = now_ms;
  } else if (st->queue_latched &&
             CV2_MS_DIFF(now_ms, st->queue_fault_ms) >=
                 (int32_t)cfg->cooldown_ms) {
    st->queue_latched = false;
  }
  if (st->queue_latched) {
    return (uint8_t)CV2_FAULT_QUEUE_FAULT;
  }
  if (!ctx->transport_connected) {
    return (uint8_t)CV2_FAULT_DISCONNECTED;
  }
  if (!ctx->battery_ok) {
    return (uint8_t)CV2_FAULT_LOW_BATTERY;
  }
  if (!ctx->armed) {
    return (uint8_t)CV2_FAULT_UNARMED;
  }
  if (!ctx->calibrated) {
    return (uint8_t)CV2_FAULT_UNCALIBRATED;
  }
  return (uint8_t)CV2_FAULT_NONE;
}

static bool producer_unhealthy(const cv2_intent_state_t *st, uint16_t pid,
                               cv2_ms_t now_ms,
                               const cv2_intent_config_t *cfg) {
  if (!st->seen[pid]) {
    return true; /* a producer must announce itself before it may act */
  }
  if (st->fault_flags[pid] != 0u) {
    return true;
  }
  return CV2_MS_DIFF(now_ms, st->last_seen_ms[pid]) >
         (int32_t)cfg->producer_timeout_ms;
}

/* Tick: only enabled producers that have ever been seen are judged; a
 * silent one that timed out is a release condition (owner rule 2). */
static uint8_t tick_health_fault(const cv2_intent_state_t *st,
                                 const cv2_intent_context_t *ctx,
                                 cv2_ms_t now_ms,
                                 const cv2_intent_config_t *cfg) {
  for (uint16_t pid = 1u; pid < (uint16_t)CV2_PRODUCER_COUNT; ++pid) {
    const bool enabled = (ctx->enabled_producers_mask & CV2_PRODUCER_BIT(pid)) != 0u;
    if (!enabled || !st->seen[pid]) {
      continue;
    }
    if (producer_unhealthy(st, pid, now_ms, cfg)) {
      return (uint8_t)CV2_FAULT_PRODUCER_UNHEALTHY;
    }
  }
  return (uint8_t)CV2_FAULT_NONE;
}

cv2_action_t cv2_intent_arbitrate(cv2_intent_state_t *st,
                                  const cv2_intent_context_t *ctx,
                                  const cv2_intent_event_t *ev, cv2_ms_t now_ms,
                                  const cv2_intent_config_t *cfg) {
  uint8_t fault;
  cv2_hdr_status_t hs;
  uint16_t pid;
  bool was_seen;
  cv2_ms_t prev_seen_ms;
  int32_t age_ms;

  if (st == NULL || ctx == NULL || cfg == NULL) {
    return fault_action(st, (uint8_t)CV2_FAULT_MALFORMED);
  }

  /* 1. Context. Unsafe context: release, remember, and never look at the
   *    event (a disarmed device must not even refresh producer liveness). */
  fault = context_fault(st, ctx, now_ms, cfg);
  if (fault != (uint8_t)CV2_FAULT_NONE) {
    st->prev_ctx_ok = false;
    return fault_action(st, fault);
  }
  st->prev_ctx_ok = true;

  /* 2. Tick: context + health only. */
  if (ev == NULL) {
    fault = tick_health_fault(st, ctx, now_ms, cfg);
    if (fault != (uint8_t)CV2_FAULT_NONE) {
      return fault_action(st, fault);
    }
    return no_action();
  }

  /* 3. Header validity before any producer-indexed access. */
  hs = cv2_header_validate(&ev->hdr, (uint16_t)CV2_EVENT_SIZE,
                           (uint8_t)CV2_INTENT_KIND_MAX, ev->ttl_ms);
  if (hs == CV2_HDR_BAD_PRODUCER) {
    return fault_action(st, (uint8_t)CV2_FAULT_PRODUCER_UNKNOWN);
  }
  if (hs != CV2_HDR_OK) {
    return fault_action(st, (uint8_t)CV2_FAULT_MALFORMED);
  }
  pid = ev->hdr.producer_id;
  if (pid == (uint16_t)CV2_PRODUCER_NONE) {
    return fault_action(st, (uint8_t)CV2_FAULT_PRODUCER_UNKNOWN);
  }

  /* 4. A valid header is a heartbeat, whatever happens next. Health below is
   *    judged on the liveness known BEFORE this event. */
  was_seen = st->seen[pid];
  prev_seen_ms = st->last_seen_ms[pid];
  st->seen[pid] = true;
  st->last_seen_ms[pid] = now_ms;

  if ((ctx->enabled_producers_mask & CV2_PRODUCER_BIT(pid)) == 0u) {
    return fault_action(st, (uint8_t)CV2_FAULT_PRODUCER_DISABLED);
  }

  /* 5. Ordering: duplicate, then stale (out-of-order seq / non-advancing
   *    timestamp / older than the freshness bound / from the future). */
  if (st->ordered[pid] && ev->hdr.seq == st->last_seq[pid]) {
    return fault_action(st, (uint8_t)CV2_FAULT_DUPLICATE);
  }
  if (st->ordered[pid] && (CV2_SEQ_AFTER(st->last_seq[pid], ev->hdr.seq) ||
                           !CV2_MS_AFTER(ev->hdr.t_ms, st->last_t_ms[pid]))) {
    return fault_action(st, (uint8_t)CV2_FAULT_STALE);
  }
  age_ms = CV2_MS_DIFF(now_ms, ev->hdr.t_ms);
  if (age_ms < 0 || age_ms > (int32_t)cfg->max_event_age_ms) {
    return fault_action(st, (uint8_t)CV2_FAULT_STALE);
  }
  /* The event is the newest well-ordered one from this producer: remember it
   * even if a later predicate rejects it, so a retransmit is a DUPLICATE. */
  st->ordered[pid] = true;
  st->last_seq[pid] = ev->hdr.seq;
  st->last_t_ms[pid] = ev->hdr.t_ms;

  /* 6. Expiry on the producer's own horizon. */
  if (CV2_MS_AFTER(now_ms, ev->hdr.t_ms + (uint32_t)ev->ttl_ms)) {
    return fault_action(st, (uint8_t)CV2_FAULT_EXPIRED);
  }

  /* 7. Producer health as known before this event. */
  if (!was_seen || st->fault_flags[pid] != 0u ||
      CV2_MS_DIFF(now_ms, prev_seen_ms) > (int32_t)cfg->producer_timeout_ms) {
    return fault_action(st, (uint8_t)CV2_FAULT_PRODUCER_UNHEALTHY);
  }

  /* 8. Confidence floor. */
  if (ev->confidence < cfg->min_confidence) {
    return fault_action(st, (uint8_t)CV2_FAULT_LOW_CONFIDENCE);
  }

  /* 9. Click cooldown (pointer moves are rate-limited by their producer). */
  if (ev->hdr.kind == (uint8_t)CV2_INTENT_CLICK && st->have_action &&
      CV2_MS_DIFF(now_ms, st->last_action_ms) < (int32_t)cfg->cooldown_ms) {
    return fault_action(st, (uint8_t)CV2_FAULT_COOLDOWN);
  }

  /* 10. Grant. */
  {
    cv2_action_t a = no_action();
    a.kind = ev->hdr.kind;
    a.granted_seq = ev->hdr.seq;
    if (ev->hdr.kind == (uint8_t)CV2_INTENT_POINTER_MOVE) {
      a.dx = ev->dx;
      a.dy = ev->dy;
    } else {
      st->last_action_ms = now_ms;
      st->have_action = true;
      st->holding = true;
    }
    return a;
  }
}
