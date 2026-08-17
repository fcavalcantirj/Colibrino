/*
 * Colibrino v2 - explicit little-endian wire codec.
 *
 * Every event that crosses a bus, a queue or a Luos message boundary goes
 * through these functions. Rules: fixed sizes, versioned header, field-by-
 * field encoding (never memcpy a raw struct), decode validates length and
 * header BEFORE writing the output struct (no partial writes on error).
 */
#ifndef COLIBRINO_V2_WIRE_H
#define COLIBRINO_V2_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colibrino/v2/access_intent.h"
#include "colibrino/v2/blink_code.h"
#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CV2_WIRE_OK = 0,
  CV2_WIRE_BAD_ARG = 1,   /* NULL pointer */
  CV2_WIRE_TRUNCATED = 2, /* len shorter than the fixed encoded size */
  CV2_WIRE_BAD_HEADER = 3 /* magic / version / size do not match */
} cv2_wire_status_t;

/* Encoders return the number of bytes written, or 0 when cap is too small
 * or an argument is NULL. Decoders write *out only on CV2_WIRE_OK. */
size_t cv2_hdr_encode(const cv2_event_hdr_t *hdr, uint8_t *buf, size_t cap);
cv2_wire_status_t cv2_hdr_decode(const uint8_t *buf, size_t len,
                                 cv2_event_hdr_t *out);

size_t cv2_intent_event_encode(const cv2_intent_event_t *ev, uint8_t *buf,
                               size_t cap);
cv2_wire_status_t cv2_intent_event_decode(const uint8_t *buf, size_t len,
                                          cv2_intent_event_t *out);

size_t cv2_blink_event_encode(const cv2_blink_event_t *ev, uint8_t *buf,
                              size_t cap);
cv2_wire_status_t cv2_blink_event_decode(const uint8_t *buf, size_t len,
                                         cv2_blink_event_t *out);

size_t cv2_gesture_event_encode(const cv2_gesture_event_t *ev, uint8_t *buf,
                                size_t cap);
cv2_wire_status_t cv2_gesture_event_decode(const uint8_t *buf, size_t len,
                                           cv2_gesture_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_WIRE_H */
