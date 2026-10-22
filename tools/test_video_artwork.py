#!/usr/bin/env python3
"""Exercise both video backends on a private X server, without game assets."""
import os
import subprocess
import sys

read_fd, write_fd = os.pipe()
server = subprocess.Popen(
    ["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1920x1080x24", "-nolisten", "tcp"],
    pass_fds=(write_fd,), stdout=subprocess.DEVNULL,
)
os.close(write_fd)
try:
    with os.fdopen(read_fd) as display:
        number = display.readline().strip()
    if not number:
        raise RuntimeError("private X server did not start")
    env = dict(os.environ, DISPLAY=":" + number, SDL_VIDEODRIVER="x11", LIBGL_ALWAYS_SOFTWARE="1")
    env.pop("KI2_NO_GL", None)
    subprocess.run([sys.argv[1], "gl"], env=env, check=True)
    subprocess.run([sys.argv[1], "sdl"], env=dict(env, KI2_NO_GL="1"), check=True)
finally:
    server.terminate()
    server.wait(timeout=10)
