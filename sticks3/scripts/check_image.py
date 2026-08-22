# PlatformIO post-link gate for the StickS3 production image (PLAN-r2).
#
# 521bc26 boot-looped with no way to recover or to see why. Simulation cannot
# reach a BLE bring-up crash, but the linked image can prove it is at least
# RECOVERABLE and OBSERVABLE when one happens. Every check below would have
# failed that build:
#  1. verifyRollbackLater() really returns true (the Arduino core otherwise
#     confirms a fresh OTA image before setup() and the bootloader can never
#     roll back); checked in the disassembly, not by symbol presence, because
#     a mangled C++ override links fine and does nothing.
#  2. esp_reset_reason() is linked and the boot/crash breadcrumb strings exist.
#  3. True internal-DRAM static footprint = .dram0.dummy (IRAM shadow) +
#     .dram0.data + .dram0.bss stays under a ceiling; PlatformIO's "RAM" hides
#     the shadow, which is where BLE's cost actually lands.
# Fails the build on any finding. Never uploads, never touches a device.
import os
import re
import subprocess

Import("env")  # noqa: F821  (PlatformIO injects this)

DRAM_TOTAL = 458752  # ESP32-S3 internal DRAM reachable by the heap (incl. the 32 KB freed by the 32 KB D-cache config)
DRAM_CEILING = int(os.environ.get("COLIBRINO_DRAM_CEILING", "204800"))
REQUIRED_STRINGS = (b"EVENT,LAST_CRASH", b"EVENT,BOOT,", b"reset=%s,raw=%d,slot=%s,ota_state=")


class ToolError(RuntimeError):
    pass


def _tool(name):
    found = env.WhereIs(name)  # noqa: F821
    if found:
        return found
    compiler = env.WhereIs(env.subst("$CC")) or ""  # noqa: F821
    candidate = os.path.join(os.path.dirname(compiler), name)
    if os.path.exists(candidate):
        return candidate
    raise ToolError("toolchain tool not found: %s" % name)


def _run(args):
    try:
        done = subprocess.run(args, capture_output=True, text=True, check=False)
    except OSError as error:
        raise ToolError("%s: %s" % (args[0], error))
    if done.returncode != 0:
        raise ToolError("%s exited %d: %s" % (os.path.basename(args[0]), done.returncode,
                                              done.stderr.strip().splitlines()[-1:] or "?"))
    return done.stdout


# The override is correct only if the body is the unconditional constant-true
# function (-Os on Xtensa: `entry a1, N; movi.n a2, 1; retw.n`). A body that
# writes a2 any other way, branches, calls, or loads is rejected: a later
# `return some_flag;` must fail this gate, not sneak through on a substring.
ALLOWED_MNEMONICS = {"entry", "movi", "movi.n", "retw", "retw.n", "nop", "nop.n"}


def _rollback_hook_returns_true(elf):
    nm = _run([_tool("xtensa-esp32s3-elf-nm"), "-S", elf])
    match = re.search(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+[Tt]\s+verifyRollbackLater$", nm, re.M)
    if not match:
        if re.search(r"\sW\s+verifyRollbackLater$", nm, re.M):
            return False, "verifyRollbackLater is still the Arduino weak default (no project override linked)"
        return False, "verifyRollbackLater symbol missing"
    start = int(match.group(1), 16)
    stop = start + int(match.group(2), 16)
    disassembly = _run([
        _tool("xtensa-esp32s3-elf-objdump"), "-d",
        "--start-address=0x%x" % start, "--stop-address=0x%x" % stop, elf,
    ])
    instructions = []
    for line in disassembly.splitlines():
        # Xtensa objdump: "<addr>:\t<opcode-hex>\t<mnemonic>\t<operands>"
        parsed = re.match(r"^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([a-z0-9_.]+)\s*(.*)$", line)
        if parsed:
            instructions.append((parsed.group(1), parsed.group(2).strip()))
    if not instructions:
        return False, "verifyRollbackLater body could not be disassembled"
    saw_constant_true = False
    for mnemonic, operands in instructions:
        if mnemonic not in ALLOWED_MNEMONICS:
            return False, "verifyRollbackLater body is not constant-true (contains %s %s)" % (mnemonic, operands)
        if mnemonic.startswith("movi"):
            if not re.match(r"^a2,\s*1$", operands):
                return False, "verifyRollbackLater writes a2 with '%s %s'" % (mnemonic, operands)
            saw_constant_true = True
    if not saw_constant_true:
        return False, "verifyRollbackLater never materialises the constant true"
    return True, "returns true (%d instructions)" % len(instructions)


def check_image(source, target, env):  # noqa: F811
    elf = str(target[0])
    failures = []
    detail = "not checked"
    try:
        ok, detail = _rollback_hook_returns_true(elf)
        if not ok:
            failures.append("rollback_hook: %s" % detail)

        nm = _run([_tool("xtensa-esp32s3-elf-nm"), elf])
        if not re.search(r"\sT\s+esp_reset_reason$", nm, re.M):
            failures.append("esp_reset_reason is not linked (boot breadcrumb missing)")
        size_output = _run([_tool("xtensa-esp32s3-elf-size"), "-A", elf])
    except ToolError as error:
        print("CHECK_IMAGE,FAIL,tool_error,%s" % error)
        env.Exit(1)
        return

    with open(elf, "rb") as handle:
        blob = handle.read()
    for needle in REQUIRED_STRINGS:
        if needle not in blob:
            failures.append("breadcrumb string missing: %s" % needle.decode())

    sections = {}
    for line in size_output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] in (".dram0.dummy", ".dram0.data", ".dram0.bss"):
            sections[parts[0]] = int(parts[1])
    missing = [name for name in (".dram0.dummy", ".dram0.data", ".dram0.bss") if name not in sections]
    if missing:
        failures.append("size -A did not report %s" % ",".join(missing))
    total = sum(sections.values())
    print("CHECK_IMAGE,internal_dram_static=%d,ceiling=%d,dummy=%d,data=%d,bss=%d,heap_at_reset_estimate=%d" % (
        total, DRAM_CEILING, sections.get(".dram0.dummy", 0), sections.get(".dram0.data", 0),
        sections.get(".dram0.bss", 0), DRAM_TOTAL - total))
    if total > DRAM_CEILING:
        failures.append("internal DRAM static footprint %d exceeds ceiling %d" % (total, DRAM_CEILING))

    if failures:
        for failure in failures:
            print("CHECK_IMAGE,FAIL,%s" % failure)
        env.Exit(1)
    print("CHECK_IMAGE,PASS,rollback_hook=%s" % detail)


# A changed ceiling must re-run the gate even when nothing else relinks.
env.Depends("$BUILD_DIR/${PROGNAME}.elf", env.Value(DRAM_CEILING))  # noqa: F821
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_image)  # noqa: F821
