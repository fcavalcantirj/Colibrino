// Diagnostics fan-out: every byte written to the mux goes to the native USB
// CDC interface exactly as before, and - when the OTA/Wi-Fi build is
// configured and associated - is also mirrored into a fixed ring buffer that
// a single TCP client may read. The mirror exists so trace capture can run
// cable-free: a USB cable is a physical anchor on the head mount and distorts
// the blink impulses being measured.
//
// Safety posture:
//  - The TCP stream is OUTPUT-ONLY. Inbound bytes are read and discarded so
//    the socket can never become a command channel; the large blue Button A
//    remains the sole application input.
//  - Mirroring is non-blocking by construction. WiFiClient::write can stall
//    the loop task for ~10 seconds on a dead peer (select 1 s x 10 retries in
//    Arduino-ESP32 2.0.17), so the drain uses raw send(fd, ..., MSG_DONTWAIT)
//    with a bounded per-pass byte budget instead.
//  - Ring overflow drops the OLDEST whole lines and counts them; the count is
//    surfaced as EVENT,TELEMETRY,DROPPED,<total> so transport loss is always
//    explicit, never silent.
//  - Without the ignored colibrino_secrets.h header the class degrades to a
//    pure CDC pass-through (no ring, no server, no Wi-Fi symbols).
#pragma once

#include <Print.h>
#include <USBCDC.h>

class TelemetryMux : public Print {
 public:
  explicit TelemetryMux(USBCDC& cdc);

  // Forwards to the wrapped CDC interface; the TCP side starts lazily from
  // service() once Wi-Fi is associated.
  void begin(unsigned long baud);

  // Print overrides: one call = CDC write + ring append of the same bytes,
  // preserving today's byte stream bit for bit.
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  // Shadows Print::printf with a stack vsnprintf (Print::printf mallocs for
  // every line longer than 64 bytes; the IMU line is ~100 bytes at ~170 Hz).
  // Falls back to Print::printf for the rare line that does not fit.
  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

  // Runs the TCP side for one cooperative pass: lazy server start, client
  // accept/replace, inbound discard, bounded non-blocking drain, and the
  // rate-limited DROPPED event. Call once per loop() pass, never during an
  // OTA transfer. No-op while Wi-Fi is down or in a no-secrets build.
  void service(uint32_t now_ms);

  bool clientConnected() const;
  uint32_t droppedLines() const;

 private:
  USBCDC& cdc_;
};
