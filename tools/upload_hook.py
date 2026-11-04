# PlatformIO post-upload hook.
# The default DTR/RTS reset leaves this board stuck in the ROM bootloader, so after flashing
# we reset the chip with an internal watchdog reset (needs esptool >= 5, installed on PATH).
Import("env")
import shutil, subprocess, sys

def watchdog_reset(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    exe = shutil.which("esptool") or shutil.which("esptool.py")
    cmd = ([exe] if exe else [sys.executable, "-m", "esptool"]) + [
        "--chip", "esp32s3", "--port", port,
        "--before", "no-reset", "--after", "watchdog-reset", "read-mem", "0x6000403C"]
    print("Resetting the board (watchdog reset) ...")
    subprocess.run(cmd, check=False)

env.AddPostAction("upload", watchdog_reset)
