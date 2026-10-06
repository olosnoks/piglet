# PlatformIO pre-build fixes for building this Arduino sketch in-place.
#
# Two problems are worked around here, both so the folder can stay exactly
# where it is (keeping Piglet.ino intact for the Arduino IDE):
#
# 1) Missing FatFS include.
#    The pioarduino 55.03.39 esp32c5 package ships a build CPPPATH that omits
#    the "fatfs" component (every other FS component is present), so the
#    bundled SD library fails with: fatal error: ff.h: No such file or
#    directory. We recompute the fatfs include dirs from the installed
#    framework-libs package and add them back.
#
# 2) Space in the project path breaks the .ino -> .cpp conversion on Windows.
#    This project lives under ".../Arduino Files/Piglet" (note the space).
#    PlatformIO converts Piglet.ino to Piglet.ino.cpp by invoking the compiler
#    through a VerboseAction string, which SCons runs via cmd.exe. cmd.exe
#    mangles the nested quotes around the spaced output path, so the generated
#    .cpp is never written and the build dies with:
#        cc1plus.exe: fatal error: Piglet.ino.cpp: No such file or directory
#    We replace the converter's compiler step with a direct subprocess call
#    that passes arguments as a list (no shell, no quote mangling).

Import("env")  # noqa: F821
import os
import subprocess
import tempfile

# esptool draws its progress bars with Unicode block characters. PlatformIO
# captures esptool's output and re-prints it through its own stdout; on a
# Windows console using a legacy code page (cp1252) that write raises
# UnicodeEncodeError *mid-flash*, which interrupts the write and can leave the
# board unbootable (black screen). Two defenses:
#   1. make this process's own stdout/stderr tolerate those characters instead
#      of crashing (this is what actually saves the IDE "Upload" button, whose
#      pio.exe we can't pass env vars to); and
#   2. ask spawned tools to emit UTF-8 too. SCons uses env['ENV'] (not
#      os.environ) for the subprocess environment, so set both.
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
for _k, _v in (("PYTHONUTF8", "1"), ("PYTHONIOENCODING", "utf-8")):
    os.environ.setdefault(_k, _v)
    env["ENV"][_k] = _v

platform = env.PioPlatform()
mcu = env.BoardConfig().get("build.mcu", "esp32c5")

# --- Fix 1: re-add FatFS include dirs -----------------------------------
libs_dir = platform.get_package_dir("framework-arduinoespressif32-libs")
if libs_dir:
    fatfs_root = os.path.join(libs_dir, mcu, "include", "fatfs")
    added = 0
    for sub in ("src", "diskio", "vfs"):
        path = os.path.join(fatfs_root, sub)
        if os.path.isdir(path):
            env.Append(CPPPATH=[path])
            added += 1
    print("pio_build_fixes: added %d FatFS include dir(s) for %s" % (added, mcu))
else:
    print("pio_build_fixes: framework-arduinoespressif32-libs not found; skipping fatfs fix")

# --- Fix 2: shell-free .ino conversion (spaces in path) -----------------
try:
    from platformio.builder.tools import pioino

    def _gcc_preprocess_noshell(self, contents, out_file):
        tmp_path = tempfile.mkstemp()[1]
        self.write_safe_contents(tmp_path, contents)
        cxx = self.env.subst("$CXX")
        try:
            subprocess.run(
                [cxx, "-o", out_file, "-x", "c++",
                 "-fpreprocessed", "-dD", "-E", tmp_path],
                check=True,
            )
        finally:
            try:
                os.remove(tmp_path)
            except OSError:
                pass
        return os.path.isfile(out_file)

    pioino.InoToCPPConverter._gcc_preprocess = _gcc_preprocess_noshell
    print("pio_build_fixes: patched .ino converter to run without cmd.exe")
except Exception as exc:  # noqa: BLE001
    print("pio_build_fixes: could not patch .ino converter (%s); "
          "builds from a path with spaces may fail" % exc)

# --- Fix 3: pick the ESP32 port by VID:PID, not whatever is first --------
# PlatformIO's auto-detect falls back to the first serial port when the board
# isn't matched, which on this machine can be an unrelated USB device (an SDR
# on another COM port) -> "Invalid head of packet / serial noise". Pin the
# upload (and monitor) port to the board's own USB id so the right device is
# always chosen regardless of its COM number. A user-set upload_port wins.
if not env.subst("$UPLOAD_PORT"):
    hwids = env.BoardConfig().get("build.hwids", [["0x303A", "0x1001"]])

    def _norm(x):
        return "%04X" % (int(x, 16) if isinstance(x, str) else x)

    wanted = set()
    for vid, pid in hwids:
        wanted.add("%s:%s" % (_norm(vid), _norm(pid)))
    try:
        import serial.tools.list_ports as _lp

        match = None
        for port in _lp.comports():
            hwid = (port.hwid or "").upper()
            if any(w in hwid for w in wanted):
                match = port.device
                break
        if match:
            env.Replace(UPLOAD_PORT=match)
            print("pio_build_fixes: selected upload port %s (matched %s)"
                  % (match, ", ".join(sorted(wanted))))
        else:
            print("pio_build_fixes: no port matching %s found; "
                  "plug in the board, then re-run upload"
                  % ", ".join(sorted(wanted)))
    except Exception as exc:  # noqa: BLE001
        print("pio_build_fixes: port auto-select skipped (%s)" % exc)
