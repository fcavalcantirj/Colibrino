/*
 * Explicit little-endian codec for the 16-byte header and every 24-byte
 * event. Decoders parse into a local, validate, then publish: a caller's
 * struct is never half-written by a truncated or foreign buffer.
 */
#include "colibrino/v2/wire.h"

#include "wire/le_bytes.h"

/* ---- header ------------------------------------------------------------ */

static void hdr_put(const cv2_event_hdr_t *hdr, uint8_t *b) {
  cv2_le_put_u16(b + 0, hdr->magic);
  cv2_le_put_u8(b + 2, hdr->version);
  cv2_le_put_u8(b + 3, hdr->kind);
  cv2_le_put_u16(b + 4, hdr->producer_id);
  cv2_le_put_u16(b + 6, hdr->size);
  cv2_le_put_u32(b + 8, hdr->seq);
  cv2_le_put_u32(b + 12, hdr->t_ms);
}

static void hdr_get(const uint8_t *b, cv2_event_hdr_t *hdr) {
  hdr->magic = cv2_le_get_u16(b + 0);
  hdr->version = cv2_le_get_u8(b + 2);
  hdr->kind = cv2_le_get_u8(b + 3);
  hdr->producer_id = cv2_le_get_u16(b + 4);
  hdr->size = cv2_le_get_u16(b + 6);
  hdr->seq = cv2_le_get_u32(b + 8);
  hdr->t_ms = cv2_le_get_u32(b + 12);
}

/* Header sanity shared by the typed decoders: magic and version must match
 * and the declared size must be the fixed size of that event family. */
static cv2_wire_status_t hdr_check(const cv2_event_hdr_t *hdr,
                                   uint16_t expected_size) {
  if (hdr->magic != CV2_EVENT_MAGIC || hdr->version != CV2_EVENT_VERSION ||
      hdr->size != expected_size) {
    return CV2_WIRE_BAD_HEADER;
  }
  return CV2_WIRE_OK;
}

size_t cv2_hdr_encode(const cv2_event_hdr_t *hdr, uint8_t *buf, size_t cap) {
  if (hdr == NULL || buf == NULL || cap < CV2_EVENT_HDR_SIZE) {
    return 0;
  }
  hdr_put(hdr, buf);
  return CV2_EVENT_HDR_SIZE;
}

cv2_wire_status_t cv2_hdr_decode(const uint8_t *buf, size_t len,
                                 cv2_event_hdr_t *out) {
  cv2_event_hdr_t tmp;
  if (buf == NULL || out == NULL) {
    return CV2_WIRE_BAD_ARG;
  }
  if (len < CV2_EVENT_HDR_SIZE) {
    return CV2_WIRE_TRUNCATED;
  }
  hdr_get(buf, &tmp);
  if (tmp.magic != CV2_EVENT_MAGIC || tmp.version != CV2_EVENT_VERSION) {
    return CV2_WIRE_BAD_HEADER;
  }
  *out = tmp;
  return CV2_WIRE_OK;
}

/* ---- intent event ------------------------------------------------------ */

size_t cv2_intent_event_encode(const cv2_intent_event_t *ev, uint8_t *buf,
                               size_t cap) {
  if (ev == NULL || buf == NULL || cap < CV2_EVENT_SIZE) {
    return 0;
  }
  hdr_put(&ev->hdr, buf);
  cv2_le_put_u16(buf + 16, ev->ttl_ms);
  cv2_le_put_u8(buf + 18, ev->confidence);
  cv2_le_put_u8(buf + 19, ev->flags);
  cv2_le_put_i16(buf + 20, ev->dx);
  cv2_le_put_i16(buf + 22, ev->dy);
  return CV2_EVENT_SIZE;
}

cv2_wire_status_t cv2_intent_event_decode(const uint8_t *buf, size_t len,
                                          cv2_intent_event_t *out) {
  cv2_intent_event_t tmp;
  cv2_wire_status_t st;
  if (buf == NULL || out == NULL) {
    return CV2_WIRE_BAD_ARG;
  }
  if (len < CV2_EVENT_SIZE) {
    return CV2_WIRE_TRUNCATED;
  }
  hdr_get(buf, &tmp.hdr);
  st = hdr_check(&tmp.hdr, CV2_EVENT_SIZE);
  if (st != CV2_WIRE_OK) {
    return st;
  }
  tmp.ttl_ms = cv2_le_get_u16(buf + 16);
  tmp.confidence = cv2_le_get_u8(buf + 18);
  tmp.flags = cv2_le_get_u8(buf + 19);
  tmp.dx = cv2_le_get_i16(buf + 20);
  tmp.dy = cv2_le_get_i16(buf + 22);
  *out = tmp;
  return CV2_WIRE_OK;
}

/* ---- blink event ------------------------------------------------------- */

size_t cv2_blink_event_encode(const cv2_blink_event_t *ev, uint8_t *buf,
                              size_t cap) {
  if (ev == NULL || buf == NULL || cap < CV2_EVENT_SIZE) {
    return 0;
  }
  hdr_put(&ev->hdr, buf);
  cv2_le_put_u16(buf + 16, ev->ttl_ms);
  cv2_le_put_u8(buf + 18, ev->confidence);
  cv2_le_put_u8(buf + 19, ev->reason);
  cv2_le_put_u16(buf + 20, ev->duration_ms);
  cv2_le_put_u16(buf + 22, ev->reserved);
  return CV2_EVENT_SIZE;
}

cv2_wire_status_t cv2_blink_event_decode(const uint8_t *buf, size_t len,
                                         cv2_blink_event_t *out) {
  cv2_blink_event_t tmp;
  cv2_wire_status_t st;
  if (buf == NULL || out == NULL) {
    return CV2_WIRE_BAD_ARG;
  }
  if (len < CV2_EVENT_SIZE) {
    return CV2_WIRE_TRUNCATED;
  }
  hdr_get(buf, &tmp.hdr);
  st = hdr_check(&tmp.hdr, CV2_EVENT_SIZE);
  if (st != CV2_WIRE_OK) {
    return st;
  }
  tmp.ttl_ms = cv2_le_get_u16(buf + 16);
  tmp.confidence = cv2_le_get_u8(buf + 18);
  tmp.reason = cv2_le_get_u8(buf + 19);
  tmp.duration_ms = cv2_le_get_u16(buf + 20);
  tmp.reserved = cv2_le_get_u16(buf + 22);
  *out = tmp;
  return CV2_WIRE_OK;
}

/* ---- gesture event ----------------------------------------------------- */

size_t cv2_gesture_event_encode(const cv2_gesture_event_t *ev, uint8_t *buf,
                                size_t cap) {
  if (ev == NULL || buf == NULL || cap < CV2_EVENT_SIZE) {
    return 0;
  }
  hdr_put(&ev->hdr, buf);
  cv2_le_put_u16(buf + 16, ev->ttl_ms);
  cv2_le_put_u8(buf + 18, ev->confidence);
  cv2_le_put_u8(buf + 19, ev->impulses);
  cv2_le_put_u16(buf + 20, ev->span_ms);
  cv2_le_put_u16(buf + 22, ev->reserved);
  return CV2_EVENT_SIZE;
}

cv2_wire_status_t cv2_gesture_event_decode(const uint8_t *buf, size_t len,
                                           cv2_gesture_event_t *out) {
  cv2_gesture_event_t tmp;
  cv2_wire_status_t st;
  if (buf == NULL || out == NULL) {
    return CV2_WIRE_BAD_ARG;
  }
  if (len < CV2_EVENT_SIZE) {
    return CV2_WIRE_TRUNCATED;
  }
  hdr_get(buf, &tmp.hdr);
  st = hdr_check(&tmp.hdr, CV2_EVENT_SIZE);
  if (st != CV2_WIRE_OK) {
    return st;
  }
  tmp.ttl_ms = cv2_le_get_u16(buf + 16);
  tmp.confidence = cv2_le_get_u8(buf + 18);
  tmp.impulses = cv2_le_get_u8(buf + 19);
  tmp.span_ms = cv2_le_get_u16(buf + 20);
  tmp.reserved = cv2_le_get_u16(buf + 22);
  *out = tmp;
  return CV2_WIRE_OK;
}
