/*
 * Colibrino v2 - profile (persisted configuration) contract.
 *
 * A profile is a fixed-size little-endian blob: header, enabled producers,
 * every unit config by value, CRC32. Loading is atomic: the output struct is
 * written only when the whole blob validated (magic, version, size, CRC,
 * ranges, known producers). CRC32 detects corruption; it is NOT
 * authentication - a profile from an untrusted source is still untrusted.
 */
#ifndef COLIBRINO_V2_PROFILE_H
#define COLIBRINO_V2_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colibrino/v2/access_intent.h"
#include "colibrino/v2/feel_defaults.h"
#include "colibrino/v2/imu_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "CV2P" as little-endian bytes. */
#define CV2_PROFILE_MAGIC 0x50325643u
#define CV2_PROFILE_VERSION 1u
/* 12 header + 32 dsp + 20 code + 8 intent + 20 motion + 4 crc. */
#define CV2_PROFILE_SIZE 96u

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size; /* CV2_PROFILE_SIZE */
  uint8_t enabled_producers_mask;
  uint8_t reserved[3];
  cv2_blink_dsp_config_t blink_dsp;
  cv2_blink_code_config_t blink_code;
  cv2_intent_config_t intent;
  cv2_motion_config_t motion;
  uint32_t crc32; /* IEEE CRC32 over the encoded bytes [0, size - 4) */
} cv2_profile_t;

typedef enum {
  CV2_PROFILE_OK = 0,
  CV2_PROFILE_BAD_MAGIC = 1,
  CV2_PROFILE_BAD_VERSION = 2,
  CV2_PROFILE_BAD_SIZE = 3,
  CV2_PROFILE_BAD_CRC = 4,
  CV2_PROFILE_OUT_OF_RANGE = 5,
  CV2_PROFILE_UNKNOWN_PRODUCER = 6
} cv2_profile_status_t;

/* Fills feel defaults, header fields and a matching CRC. */
void cv2_profile_defaults(cv2_profile_t *out);
/* Validates an in-memory profile; len must be sizeof(cv2_profile_t). */
cv2_profile_status_t cv2_profile_validate(const cv2_profile_t *p, size_t len);
/* Decodes and validates blob; writes *out only when the result is OK. */
cv2_profile_status_t cv2_profile_load(const uint8_t *blob, size_t len,
                                      cv2_profile_t *out);
/* Encodes p (including its crc32 field as-is). Returns bytes written or 0. */
size_t cv2_profile_encode(const cv2_profile_t *p, uint8_t *buf, size_t cap);
/* CRC32 the profile should carry for its current field values. */
uint32_t cv2_profile_crc(const cv2_profile_t *p);

/* IEEE 802.3 CRC32 (reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF),
 * bitwise, table-free. Corruption detection only, NOT authentication. */
uint32_t cv2_crc32(const uint8_t *data, size_t len);
uint32_t cv2_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* COLIBRINO_V2_PROFILE_H */
