/*
 * Internal little-endian byte helpers shared by the wire codec and the
 * profile loader. Byte-wise on purpose: host endianness and struct padding
 * never leak into a blob. Floats travel as their IEEE-754 bit pattern.
 */
#ifndef COLIBRINO_V2_INTERNAL_LE_BYTES_H
#define COLIBRINO_V2_INTERNAL_LE_BYTES_H

#include <stdint.h>
#include <string.h>

static inline void cv2_le_put_u8(uint8_t *b, uint8_t v) { b[0] = v; }

static inline void cv2_le_put_u16(uint8_t *b, uint16_t v) {
  b[0] = (uint8_t)(v & 0xFFu);
  b[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void cv2_le_put_u32(uint8_t *b, uint32_t v) {
  b[0] = (uint8_t)(v & 0xFFu);
  b[1] = (uint8_t)((v >> 8) & 0xFFu);
  b[2] = (uint8_t)((v >> 16) & 0xFFu);
  b[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void cv2_le_put_i8(uint8_t *b, int8_t v) { b[0] = (uint8_t)v; }

static inline void cv2_le_put_i16(uint8_t *b, int16_t v) {
  cv2_le_put_u16(b, (uint16_t)v);
}

static inline void cv2_le_put_f32(uint8_t *b, float v) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof bits); /* scalar type pun, not a struct copy */
  cv2_le_put_u32(b, bits);
}

static inline uint8_t cv2_le_get_u8(const uint8_t *b) { return b[0]; }

static inline uint16_t cv2_le_get_u16(const uint8_t *b) {
  return (uint16_t)((uint16_t)b[0] | (uint16_t)((uint16_t)b[1] << 8));
}

static inline uint32_t cv2_le_get_u32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

static inline int8_t cv2_le_get_i8(const uint8_t *b) { return (int8_t)b[0]; }

static inline int16_t cv2_le_get_i16(const uint8_t *b) {
  return (int16_t)cv2_le_get_u16(b);
}

static inline float cv2_le_get_f32(const uint8_t *b) {
  const uint32_t bits = cv2_le_get_u32(b);
  float v;
  memcpy(&v, &bits, sizeof v);
  return v;
}

#endif /* COLIBRINO_V2_INTERNAL_LE_BYTES_H */
