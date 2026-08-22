# PlatformIO pre-script: injects the git identity of the working tree as
# COLIBRINO_BUILD_ID so the firmware can report which commit produced it
# (boot EVENT,BUILD line, STATUS build= field, telemetry greeting).
#
# Notes:
#  - Changing the sha changes CPPDEFINES, which invalidates every object file
#    and forces a full rebuild. Accepted cost for this project size.
#  - Never prints anything but the short sha; no secrets are read.
#  - Falls back to "unknown" when git is unavailable (clean exports, CI).
import subprocess

Import("env")  # noqa: F821  (PlatformIO injects this)


def _git(args):
    try:
        return subprocess.check_output(
            ["git"] + args,
            cwd=env["PROJECT_DIR"],  # noqa: F821
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        return ""


sha = _git(["rev-parse", "--short", "HEAD"]) or "unknown"
if sha != "unknown" and _git(["status", "--porcelain"]):
    sha += "+dirty"

# Build variants that extend the production environment (for example
# `m5stack-sticks3-noble`, the BLE-less instrumentation image) carry the
# variant suffix in the id so a greeting or STATUS line can never be mistaken
# for the full image built from the same commit.
_pioenv = env.get("PIOENV", "")  # noqa: F821
try:
    _default_env = env.GetProjectConfig().get_default_env()  # noqa: F821
except Exception:  # pragma: no cover - very old PlatformIO cores
    _default_env = "m5stack-sticks3"
if _pioenv and _pioenv != _default_env:
    sha += _pioenv[len(_default_env):] if _pioenv.startswith(_default_env + "-") else "-" + _pioenv

print("COLIBRINO_BUILD_ID=%s" % sha)
env.Append(CPPDEFINES=[("COLIBRINO_BUILD_ID", env.StringifyMacro(sha))])  # noqa: F821
