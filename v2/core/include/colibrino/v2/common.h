/*
 * Colibrino v2 - shared contracts: time, sequence, producers, event header.
 *
 * Pure C11. Only <stdint.h>, <stddef.h>, <stdbool.h>. No globals, no clock
 * reads, no allocation. Every unit exchanges fixed-size events that start with
 * the 16-byte header below; the wire form is explicit little-endian
 * (see wire.h), never a raw struct copy.
 */
#ifndef COLIBRINO_V2_COMMON_H
#define COLIBRINO_V2_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device milliseconds. Wraps every ~49.7 days; always compare with the
 * signed-difference macros below, never with < or >. */
typedef uint32_t cv2_ms_t;

/* Signed distance a - b in ms, valid while |a - b| < 2^31 (~24.8 days).
 * The uint32->int32 cast is modulo 2^32 on every supported compiler. */
#define CV2_MS_DIFF(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)))
/* True when a is strictly later than b across the 32-bit wrap. */
#define CV2_MS_AFTER(a, b) (CV2_MS_DIFF(a, b) > 0)
/* Sequence numbers use the SAME signed-difference form. Assumption: two
 * compared sequences of one producer are always < 2^31 apart (a producer
 * emits far fewer than 2^31 events between two arbiter observations), so a
 * wrap from 0xFFFFFFFF to 0 is still "after". */
#define CV2_SEQ_AFTER(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) > 0)

/* Producers are the only sources the arbiter will ever index state by. Ids are
 * fixed forever; new sources append before COUNT. */
enum cv2_producer {
  CV2_PRODUCER_NONE = 0,
  CV2_PRODUCER_IMU_MOTION = 1,
  CV2_PRODUCER_BLINK_IMU = 2,
  CV2_PRODUCER_BLINK_CODE = 3,
  CV2_PRODUCER_BLINK_OPTICAL = 4, /* reserved: IR / proximity channel */
  CV2_PRODUCER_VOICE = 5,         /* round 2 */
  CV2_PRODUCER_SWITCH = 6,        /* reserved: external switch */
  CV2_PRODUCER_COUNT = 7
};

/* Bit i of an enabled-producers mask names producer id i. NONE (bit 0) is
 * never enabled; bits >= COUNT are unknown. */
#define CV2_PRODUCER_BIT(id) ((uint8_t)(1u << (id)))
#define CV2_PRODUCER_KNOWN_MASK                                              \
  ((uint8_t)(CV2_PRODUCER_BIT(CV2_PRODUCER_IMU_MOTION) |                     \
             CV2_PRODUCER_BIT(CV2_PRODUCER_BLINK_IMU) |                      \
             CV2_PRODUCER_BIT(CV2_PRODUCER_BLINK_CODE) |                     \
             CV2_PRODUCER_BIT(CV2_PRODUCER_BLINK_OPTICAL) |                  \
             CV2_PRODUCER_BIT(CV2_PRODUCER_VOICE) |                          \
             CV2_PRODUCER_BIT(CV2_PRODUCER_SWITCH)))

/* Bounded expiry horizon: ttl_ms above this is malformed, far below 2^31 so
 * t_ms + ttl_ms never becomes ambiguous under wrap. */
#define CV2_MAX_TTL_MS 60000u

#define CV2_EVENT_MAGIC 0xC2A1u
#define CV2_EVENT_VERSION 1u

/* Common 16-byte event header. Field offsets are part of the contract:
 * magic 0, version 2, kind 3, producer_id 4, size 6, seq 8, t_ms 12
 * (static-asserted in core/contracts.c and checked at runtime by tests).
 * kind 0 always means "no event" and is invalid on the wire. */
typedef struct {
  uint16_t magic;       /* CV2_EVENT_MAGIC */
  uint8_t version;      /* CV2_EVENT_VERSION */
  uint8_t kind;         /* per event family; 0 = NONE */
  uint16_t producer_id; /* enum cv2_producer, < CV2_PRODUCER_COUNT */
  uint16_t size;        /* encoded size incl. header (24 for all events) */
  uint32_t seq;         /* per-producer, increments by one per event */
  uint32_t t_ms;        /* producer timestamp on the device clock */
} cv2_event_hdr_t;

#define CV2_EVENT_HDR_SIZE 16u
#define CV2_EVENT_SIZE 24u

/* One IMU sample in physical units (BMI270 through M5Unified): degrees per
 * second and g. t_us is the device microsecond stamp kept for provenance;
 * all decisions use t_ms. */
typedef struct {
  cv2_ms_t t_ms;
  uint32_t t_us;
  float gyro_dps[3];
  float accel_g[3];
} cv2_imu_sample_t;

#define CV2_IMU_SAMPLE_SIZE 32u

typedef enum {
  CV2_HDR_OK = 0,
  CV2_HDR_NULL = 1,
  CV2_HDR_BAD_MAGIC = 2,
  CV2_HDR_BAD_VERSION = 3,
  CV2_HDR_BAD_SIZE = 4,
  CV2_HDR_BAD_KIND = 5,
  CV2_HDR_BAD_PRODUCER = 6,
  CV2_HDR_BAD_TTL = 7
} cv2_hdr_status_t;

/* Shared header validation used by every consumer BEFORE any producer-indexed
 * state is touched. Order: magic -> version -> size -> kind range (1..max_kind)
 * -> producer_id < CV2_PRODUCER_COUNT -> ttl horizon. */
static inline cv2_hdr_status_t cv2_header_validate(const cv2_event_hdr_t *hdr,
                                                   uint16_t expected_size,
                                                   uint8_t max_kind,
                                                   uint16_t ttl_ms) {
  if (hdr == NULL) {
    return CV2_HDR_NULL;
  }
  if (hdr->magic != CV2_EVENT_MAGIC) {
    return CV2_HDR_BAD_MAGIC;
  }
  if (hdr->version != CV2_EVENT_VERSION) {
    return CV2_HDR_BAD_VERSION;
  }
  if (hdr->size != expected_size) {
    return CV2_HDR_BAD_SIZE;
  }
  if (hdr->kind == 0u || hdr->kind > max_kind) {
    return CV2_HDR_BAD_KIND;
  }
  if (hdr->producer_id >= (uint16_t)CV2_PRODUCER_COUNT) {
    return CV2_HDR_BAD_PRODUCER;
  }
  if (ttl_ms > CV2_MAX_TTL_MS) {
    return CV2_HDR_BAD_TTL;
  }
  return CV2_HDR_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_COMMON_H */
