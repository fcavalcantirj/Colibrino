#!/usr/bin/env python3
"""Reconnecting multi-port serial logger for StickS3 diagnosis boots (PLAN-r2).

Why: on the StickS3 the USB-C port carries two different consoles at different
times. Between a hard reset and USB.begin() the ROM/bootloader/early-app
USB-Serial/JTAG port exists (location-named, e.g. /dev/cu.usbmodem101); after
USB.begin() only the TinyUSB composite CDC exists (serial-named, e.g.
/dev/cu.usbmodemAC276ED268B82) and the PHY hand-over survives software resets.
Panic text itself only reaches UART0 (G43/G44 on the HAT2-Bus header), which a
3.3 V USB-TTL adapter can tail through --uart. This tool opens every matching
port as it appears, survives every disappearance, tags each line with UTC time
and port, and appends everything to one log file.

Read-only, and careful with the control lines: the ROM USB-Serial/JTAG port
turns DTR/RTS patterns into chip reset / download mode, so every port is opened
with DTR and RTS deasserted; only ports whose USB product string matches
--dtr-product (default "Colibrino", the TinyUSB composite, which needs DTR to
transmit) get DTR asserted after opening. Ports are held exclusively while
open: stop this tool before any esptool/PlatformIO flash on the same port.

Run with PlatformIO's Python (it ships pyserial):
  /Users/fcavalcanti/.platformio/penv/bin/python scripts/serial_console.py \
      --log .device-backups/logs/serial-<build>-<utc>.log [--uart /dev/cu.usbserial-XXXX]
"""
import argparse
import datetime
import glob
import os
import sys
import threading
import time

try:
    import serial  # type: ignore
    from serial.tools import list_ports  # type: ignore
except ImportError:  # pragma: no cover - environment guard
    sys.exit("pyserial is missing; run with /Users/fcavalcanti/.platformio/penv/bin/python")


def utc_now():
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.strftime("%Y-%m-%dT%H:%M:%S.") + "%03dZ" % (now.microsecond // 1000)


def port_product(port):
    for info in list_ports.comports():
        if info.device == port:
            return " ".join(part for part in (info.manufacturer, info.product, info.description) if part)
    return ""


class LogSink:
    def __init__(self, path):
        self._lock = threading.Lock()
        self._file = open(path, "a", buffering=1)
        self._closed = False
        self._last = {}

    def write(self, label, text, dedupe_key=None):
        line = "%s [%s] %s" % (utc_now(), label, text)
        with self._lock:
            if self._closed:
                return
            if dedupe_key is not None:
                if self._last.get(label) == dedupe_key:
                    return
                self._last[label] = dedupe_key
            elif label in self._last:
                del self._last[label]
            self._file.write(line + "\n")
            print(line, flush=True)

    def close(self):
        with self._lock:
            self._closed = True
            self._file.close()


class PortReader(threading.Thread):
    def __init__(self, port, baud, sink, assert_dtr, idle_flush_s=0.5):
        super().__init__(name="reader:%s" % port, daemon=True)
        self.port = port
        self.baud = baud
        self.sink = sink
        self.assert_dtr = assert_dtr
        self.idle_flush_s = idle_flush_s
        self.alive = True
        self.opened = False

    def run(self):
        label = os.path.basename(self.port)
        handle = serial.Serial()
        handle.port = self.port
        handle.baudrate = self.baud
        handle.timeout = 0.1
        handle.dtr = False  # applied at open: no reset/download pattern on the JTAG port
        handle.rts = False
        try:
            handle.open()
            self.opened = True
            if self.assert_dtr:
                handle.dtr = True  # TinyUSB CDC only transmits with DTR asserted
            self.sink.write(label, "<opened dtr=%d>" % (1 if self.assert_dtr else 0))
            buffer = b""
            last_byte = time.monotonic()
            while True:
                chunk = handle.read(256)
                if chunk:
                    buffer += chunk
                    last_byte = time.monotonic()
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        self.sink.write(label, line.decode("utf-8", "replace").rstrip("\r"))
                elif buffer and time.monotonic() - last_byte > self.idle_flush_s:
                    self.sink.write(label, buffer.decode("utf-8", "replace").rstrip("\r") + " <partial>")
                    buffer = b""
        except (OSError, serial.SerialException) as error:
            message = str(error).splitlines()[0] if str(error) else error.__class__.__name__
            if self.opened:
                self.sink.write(label, "<closed: %s>" % message)
            else:
                self.sink.write(label, "<open failed: %s>" % message, dedupe_key=message)
        finally:
            try:
                handle.close()
            except Exception:  # pragma: no cover - best effort
                pass
            self.alive = False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--glob", default="/dev/cu.usbmodem*", help="tty glob to watch (default: %(default)s)")
    parser.add_argument("--exclude", action="append", default=[], help="tty path to ignore (repeatable)")
    parser.add_argument("--uart", default=None, help="optional UART0 USB-TTL adapter tty (G43/G44 tap)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--dtr-product", default="Colibrino",
                        help="assert DTR only on ports whose USB product string contains this (default: %(default)s)")
    parser.add_argument("--log", default=None, help="log file (default: .device-backups/logs/serial-<utc>.log)")
    parser.add_argument("--poll-ms", type=int, default=200)
    args = parser.parse_args()

    project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    log_path = args.log or os.path.join(project_dir, ".device-backups", "logs",
                                        "serial-%s.log" % utc_now().replace(":", "").replace(".", ""))
    os.makedirs(os.path.dirname(os.path.abspath(log_path)), exist_ok=True)
    sink = LogSink(log_path)
    sink.write("console", "<logging to %s; glob=%s uart=%s baud=%d; ports are held exclusively while open - "
                          "stop this tool before any esptool/PlatformIO flash>" % (log_path, args.glob, args.uart, args.baud))
    readers = {}
    backoff = {}  # port -> (next_attempt_monotonic, delay_s)
    excluded = set(os.path.realpath(p) for p in args.exclude)
    try:
        while True:
            wanted = set(glob.glob(args.glob))
            if args.uart and os.path.exists(args.uart):
                wanted.add(args.uart)
            wanted = set(p for p in wanted if os.path.realpath(p) not in excluded)
            now = time.monotonic()
            for port, reader in list(readers.items()):
                if not reader.alive:
                    del readers[port]
                    if reader.opened:
                        backoff.pop(port, None)
                    else:
                        _, delay = backoff.get(port, (0, 0.2))
                        backoff[port] = (now + delay, min(delay * 2, 5.0))
            for port in sorted(wanted):
                if port in readers:
                    continue
                next_attempt, _ = backoff.get(port, (0, 0.2))
                if now < next_attempt:
                    continue
                assert_dtr = bool(args.dtr_product) and args.dtr_product in port_product(port)
                reader = PortReader(port, args.baud, sink, assert_dtr)
                readers[port] = reader
                reader.start()
            time.sleep(args.poll_ms / 1000.0)
    except KeyboardInterrupt:
        sink.write("console", "<stopped by operator>")
    finally:
        sink.close()


if __name__ == "__main__":
    main()
