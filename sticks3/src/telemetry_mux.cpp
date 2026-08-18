// See telemetry_mux.h for the design contract. Numbers used here were read
// from the installed Arduino-ESP32 2.0.17 / lwIP configuration:
//  - TCP_MSS is 1436 (sdkconfig CONFIG_LWIP_TCP_MSS), so the per-pass drain
//    budget is two segments = 2872 bytes.
//  - The lwIP send buffer is 5744 bytes; together with the 32 KB ring the
//    device tolerates roughly two seconds of Wi-Fi stall at full trace rate
//    (~18 KB/s) before anything is dropped.
//  - WiFiServer::begin sets O_NONBLOCK on the listener and binds INADDR_ANY,
//    so the listening socket survives STA reconnects; begin() is idempotent
//    and resets the no-delay flag, hence setNoDelay(true) is re-applied after
//    every begin().
#include "telemetry_mux.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if __has_include("colibrino_secrets.h")
#define COLIBRINO_TELEMETRY_CONFIGURED 1
#else
#define COLIBRINO_TELEMETRY_CONFIGURED 0
#endif

#if COLIBRINO_TELEMETRY_CONFIGURED
#include <ESPmDNS.h>
#include <WiFi.h>

#include <errno.h>
#include <lwip/sockets.h>
// lwip/sockets.h may macro-rename POSIX calls; mirror WiFiClient.cpp so the
// Print overrides below keep their own names.
#undef write
#undef read
#undef connect
#endif

#ifndef COLIBRINO_BUILD_ID
#define COLIBRINO_BUILD_ID "unknown"
#endif

namespace {

#if COLIBRINO_TELEMETRY_CONFIGURED

constexpr uint16_t kTelemetryPort = 35533;
// Two full TCP segments per cooperative pass. The loop runs hundreds of
// passes per second, so drain capacity exceeds the ~18 KB/s trace rate by
// more than a factor of five even during display redraw stalls.
constexpr size_t kDrainBudgetBytes = 2u * 1436u;
constexpr size_t kRingCapacity = 32768;

uint8_t ring_storage[kRingCapacity];
size_t ring_head = 0;  // next byte to send
size_t ring_size = 0;  // bytes queued
uint32_t dropped_lines_total = 0;
uint32_t dropped_lines_reported = 0;
uint32_t last_dropped_report_ms = 0;

WiFiServer telemetry_server(kTelemetryPort, 1);
WiFiClient telemetry_client;
bool server_started = false;
uint32_t last_server_begin_ms = 0;
bool mdns_service_added = false;
// Attachment is tracked here, NEVER via WiFiClient::operator bool(): the
// framework's connected() probes with a zero-length recv and latches
// disconnected on the peer FIN (lwIP maps ERR_CLSD to ENOTCONN), which is
// precisely the healthy half-close this stream expects. Only a send() error
// clears this flag.
bool client_attached = false;
// Set when a drain stopped mid-line; an overflow drop in that state would
// otherwise splice the next surviving line onto the transmitted head bytes.
bool partial_line_sent = false;
// The capture host half-closes its side (shutdown(SHUT_WR)) immediately
// after connecting, so recv() returning 0 is the expected FIN of a healthy
// read-only peer, never a disconnect. Only a send() error ends the client.
bool client_rx_closed = false;

size_t ringContiguous() {
  const size_t until_wrap = kRingCapacity - ring_head;
  return ring_size < until_wrap ? ring_size : until_wrap;
}

void ringPop(size_t count) {
  ring_head = (ring_head + count) % kRingCapacity;
  ring_size -= count;
}

// Drops whole oldest lines until at least `needed` bytes are free. Counting
// whole lines keeps the client stream parseable after an overflow.
void ringDropOldest(size_t needed) {
  while (kRingCapacity - ring_size < needed && ring_size > 0) {
    size_t scanned = 0;
    while (scanned < ring_size) {
      const uint8_t byte = ring_storage[(ring_head + scanned) % kRingCapacity];
      ++scanned;
      if (byte == '\n') {
        break;
      }
    }
    ringPop(scanned);
    ++dropped_lines_total;
  }
}

void ringAppend(const uint8_t* buffer, size_t size);

// Restores line framing after an overflow that discarded the un-sent tail of
// a partially transmitted line: without this the client would receive the
// transmitted head bytes spliced onto the next surviving line.
void resyncAfterPartial() {
  if (partial_line_sent) {
    const uint8_t newline = '\n';
    partial_line_sent = false;
    ringAppend(&newline, 1);
  }
}

void ringAppend(const uint8_t* buffer, size_t size) {
  if (size > kRingCapacity) {
    // A single write larger than the ring cannot be mirrored coherently;
    // count it as one dropped line and keep the stream parseable.
    ++dropped_lines_total;
    return;
  }
  const uint32_t drops_before = dropped_lines_total;
  ringDropOldest(size);
  if (dropped_lines_total != drops_before) {
    resyncAfterPartial();
  }
  for (size_t i = 0; i < size; ++i) {
    ring_storage[(ring_head + ring_size + i) % kRingCapacity] = buffer[i];
  }
  ring_size += size;
}

bool clientAlive() {
  return client_attached && telemetry_client.fd() >= 0;
}

void detachClient() {
  client_attached = false;
  client_rx_closed = false;
  partial_line_sent = false;
  telemetry_client.stop();  // reclaim the fd (peer may sit in CLOSE_WAIT)
}

// Sends ring bytes with a hard per-pass budget. MSG_DONTWAIT never blocks;
// EAGAIN simply ends the pass. Any other error is a dead peer.
void drainToClient() {
  size_t budget = kDrainBudgetBytes;
  while (budget > 0 && ring_size > 0 && clientAlive()) {
    size_t chunk = ringContiguous();
    if (chunk > budget) {
      chunk = budget;
    }
    const ssize_t sent = send(telemetry_client.fd(),
                              &ring_storage[ring_head], chunk, MSG_DONTWAIT);
    if (sent > 0) {
      partial_line_sent =
          ring_storage[(ring_head + static_cast<size_t>(sent) - 1) %
                       kRingCapacity] != '\n';
      ringPop(static_cast<size_t>(sent));
      budget -= static_cast<size_t>(sent);
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;  // socket buffer full; try again next pass
    }
    detachClient();
    return;
  }
}

// Reads and discards anything the peer sends. The stream must never become a
// command channel; recv()==0 is the expected half-close FIN (see above).
void discardInbound() {
  if (!clientAlive() || client_rx_closed) {
    return;
  }
  uint8_t scratch[64];
  for (int i = 0; i < 4; ++i) {
    const ssize_t received =
        recv(telemetry_client.fd(), scratch, sizeof scratch, MSG_DONTWAIT);
    if (received > 0) {
      continue;  // discard
    }
    if (received == 0) {
      client_rx_closed = true;
    }
    return;
  }
}

void greetClient() {
  // Carries the cumulative-since-boot drop total so a new client can
  // baseline: any later DROPPED event above this number is loss it saw.
  char line[112];
  const int length = snprintf(line, sizeof line,
                              "EVENT,TELEMETRY,CONNECTED,%s,dropped=%lu\n",
                              COLIBRINO_BUILD_ID,
                              static_cast<unsigned long>(dropped_lines_total));
  if (length > 0 && clientAlive()) {
    // Best-effort single segment; a fresh socket buffer always has room.
    send(telemetry_client.fd(), line, static_cast<size_t>(length),
         MSG_DONTWAIT);
  }
}

#endif  // COLIBRINO_TELEMETRY_CONFIGURED

}  // namespace

TelemetryMux::TelemetryMux(USBCDC& cdc) : cdc_(cdc) {}

void TelemetryMux::begin(unsigned long baud) { cdc_.begin(baud); }

size_t TelemetryMux::write(uint8_t c) { return write(&c, 1); }

size_t TelemetryMux::write(const uint8_t* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return 0;
  }
  const size_t written = cdc_.write(buffer, size);
#if COLIBRINO_TELEMETRY_CONFIGURED
  // Mirror only while a client is attached: buffering with nobody listening
  // would inflate the drop counter and flood a fresh client with stale data.
  if (clientAlive()) {
    ringAppend(buffer, size);
  }
#endif
  // Report the CDC result so Print semantics on the USB path are unchanged;
  // the mirror is fire-and-forget by design.
  return written;
}

size_t TelemetryMux::printf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (length < 0) {
    return 0;
  }
  if (static_cast<size_t>(length) >= sizeof buffer) {
    // Rare oversized line: fall back to the heap-based base implementation
    // rather than truncating.
    va_start(args, format);
    char* heap_line = nullptr;
    const int heap_length = vasprintf(&heap_line, format, args);
    va_end(args);
    if (heap_length < 0 || heap_line == nullptr) {
      return 0;
    }
    const size_t written =
        write(reinterpret_cast<const uint8_t*>(heap_line),
              static_cast<size_t>(heap_length));
    free(heap_line);
    return written;
  }
  return write(reinterpret_cast<const uint8_t*>(buffer),
               static_cast<size_t>(length));
}

void TelemetryMux::service(uint32_t now_ms) {
#if COLIBRINO_TELEMETRY_CONFIGURED
  if (WiFi.status() != WL_CONNECTED) {
    // Without the association there is nobody to drain to; detaching also
    // stops the mirror from churning the ring and inflating the drop counter
    // against a peer that cannot be reached (SO_KEEPALIVE would otherwise
    // take hours to notice).
    if (client_attached) {
      detachClient();
    }
    return;
  }
  if (!server_started) {
    // Rate-limited: WiFiServer::begin leaks its socket on a transient bind
    // or listen failure, and an every-pass retry could exhaust the lwIP
    // socket table and take authenticated OTA (the recovery lane) with it.
    if (now_ms - last_server_begin_ms < 1000) {
      return;
    }
    last_server_begin_ms = now_ms;
    telemetry_server.begin(kTelemetryPort, 1);
    server_started = static_cast<bool>(telemetry_server);
    if (!server_started) {
      return;
    }
  }
  // begin() resets the flag, and the listener may be re-begun after an AP
  // change; re-applying is a cheap no-op otherwise.
  telemetry_server.setNoDelay(true);
  if (!mdns_service_added) {
    // ArduinoOTA already ran MDNS.begin(); advertising the diagnostics port
    // is additive and never gates the data path.
    mdns_service_added = MDNS.addService("colibrino-diag", "tcp",
                                         kTelemetryPort);
  }

  WiFiClient incoming = telemetry_server.available();
  // fd(), never operator bool(): the bool conversion is connected(), whose
  // zero-length recv probe latches false on the peer FIN. The capture host
  // half-closes immediately after connect, so its FIN can arrive before this
  // accept poll runs and the probe would drop the client un-greeted.
  if (incoming.fd() >= 0) {
    // Assignment stops any previous client: a reconnecting host replaces a
    // stale connection without a device reboot.
    telemetry_client = incoming;  // assignment stops any previous client
    telemetry_client.setNoDelay(true);
    client_attached = true;
    client_rx_closed = false;
    partial_line_sent = false;
    // Each client starts from a clean stream (no stale backlog). The DROPPED
    // counter is cumulative since boot; the greeting's dropped= field is the
    // client's baseline.
    ring_head = 0;
    ring_size = 0;
    greetClient();
  }

  if (dropped_lines_total != dropped_lines_reported &&
      now_ms - last_dropped_report_ms >= 1000) {
    last_dropped_report_ms = now_ms;
    dropped_lines_reported = dropped_lines_total;
    // Goes through the mux itself: the CDC copy and the mirrored copy keep
    // both observers informed. Single loop task, so no reentrancy hazard.
    this->printf("EVENT,TELEMETRY,DROPPED,%lu\n",
                 static_cast<unsigned long>(dropped_lines_total));
  }
  if (!clientAlive()) {
    return;
  }
  discardInbound();
  drainToClient();
#else
  (void)now_ms;
#endif
}

bool TelemetryMux::clientConnected() const {
#if COLIBRINO_TELEMETRY_CONFIGURED
  return clientAlive();
#else
  return false;
#endif
}

uint32_t TelemetryMux::droppedLines() const {
#if COLIBRINO_TELEMETRY_CONFIGURED
  return dropped_lines_total;
#else
  return 0;
#endif
}
