/*
 * IEEE 802.3 CRC32, bitwise (no lookup table: 1 KiB of flash matters more
 * than 8 shifts per byte at profile-load frequency).
 *
 * Corruption detection only, NOT authentication: anyone can compute it. A
 * profile from an untrusted source stays untrusted after the CRC matches.
 */
#include "colibrino/v2/profile.h"

uint32_t cv2_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  if (data == NULL) {
    return crc;
  }
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8u; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

uint32_t cv2_crc32(const uint8_t *data, size_t len) {
  return cv2_crc32_update(0u, data, len);
}
