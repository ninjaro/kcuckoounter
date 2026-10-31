#!/usr/bin/env python3
"""Exercise the real executable through native X11 window-manager close events.

Run explicitly on an X11/XWayland desktop: python3 application_shutdown.py BINARY.
No test hooks are installed in the application. Each process uses isolated XDG
settings; three launches reuse those settings to exercise normal restoration.
"""

import ctypes as c
import ctypes.util
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time


class ClientMessage(c.Structure):
    _fields_ = [
        ("type", c.c_int), ("serial", c.c_ulong), ("send", c.c_int),
        ("display", c.c_void_p), ("window", c.c_ulong),
        ("message_type", c.c_ulong), ("format", c.c_int),
        ("data", c.c_long * 5),
    ]


class Event(c.Union):
    _fields_ = [("client", ClientMessage), ("pad", c.c_long * 24)]


def check_shutdown(executable):
    x = c.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
    pointer, window_id = c.c_void_p, c.c_ulong

    def bind(name, args, result=c.c_int):
        function = getattr(x, name)
        function.argtypes, function.restype = args, result
        return function

    bind("XOpenDisplay", [c.c_char_p], pointer)
    bind("XCloseDisplay", [pointer])
    bind("XDefaultRootWindow", [pointer], window_id)
    bind("XInternAtom", [pointer, c.c_char_p, c.c_int], window_id)
    bind("XConnectionNumber", [pointer])
    bind("XFree", [pointer])
    bind("XSelectInput", [pointer, window_id, c.c_long])
    bind("XNextEvent", [pointer, c.POINTER(Event)])
    bind("XPending", [pointer])
    bind("XFlush", [pointer])
    bind("XSendEvent", [pointer, window_id, c.c_int, c.c_long, c.POINTER(Event)])
    bind("XQueryTree", [pointer, window_id, c.POINTER(window_id),
                       c.POINTER(window_id), c.POINTER(c.POINTER(window_id)),
                       c.POINTER(c.c_uint)])
    bind("XGetWindowProperty", [pointer, window_id, window_id, c.c_long,
                               c.c_long, c.c_int, window_id, c.POINTER(window_id),
                               c.POINTER(c.c_int), c.POINTER(window_id),
                               c.POINTER(window_id), c.POINTER(pointer)])
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("A real X11/XWayland display is required; offscreen is not native acceptance")
    root = x.XDefaultRootWindow(display)
    pid_atom = x.XInternAtom(display, b"_NET_WM_PID", 0)

    def pid(window):
        actual, count, remaining = window_id(), window_id(), window_id()
        form, data = c.c_int(), pointer()
        code = x.XGetWindowProperty(display, window, pid_atom, 0, 1, 0, 0,
                                    c.byref(actual), c.byref(form), c.byref(count),
                                    c.byref(remaining), c.byref(data))
        try:
            if code == 0 and form.value == 32 and count.value and data:
                return c.cast(data, c.POINTER(window_id))[0]
            return None
        finally:
            if data:
                x.XFree(data)

    def windows(window, depth=0):
        if depth > 4:
            return
        yield window
        parent, tree_root, count = window_id(), window_id(), c.c_uint()
        children = c.POINTER(window_id)()
        if x.XQueryTree(display, window, c.byref(tree_root), c.byref(parent),
                        c.byref(children), c.byref(count)):
            ids = list(children[:count.value])
            if children:
                x.XFree(children)
            for child in ids:
                yield from windows(child, depth + 1)

    # Wait for server events, not arbitrary startup sleeps or focus retries.
    x.XSelectInput(display, root, 1 << 19)  # SubstructureNotifyMask
    x.XFlush(display)
    try:
        with tempfile.TemporaryDirectory(prefix="kcuckoounter-shutdown-") as tmp:
            environment = os.environ.copy()
            environment["QT_QPA_PLATFORM"] = "xcb"
            for key in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
                environment[key] = str(Path(tmp) / key)
            for attempt in range(3):
                with tempfile.TemporaryFile() as output:
                    process = subprocess.Popen([str(executable)], env=environment,
                                               stdout=output, stderr=subprocess.STDOUT)
                    try:
                        deadline, window = time.monotonic() + 30, None
                        while process.poll() is None and time.monotonic() < deadline:
                            window = next((w for w in windows(root) if pid(w) == process.pid), None)
                            if window:
                                break
                            if not x.XPending(display):
                                select.select([x.XConnectionNumber(display)], [], [],
                                              max(0, deadline - time.monotonic()))
                            while x.XPending(display):
                                event = Event()
                                x.XNextEvent(display, c.byref(event))
                        if not window:
                            raise AssertionError("Application did not expose its native window")
                        event = Event()
                        event.client.type, event.client.format = 33, 32  # ClientMessage
                        event.client.display, event.client.window = display, window
                        event.client.message_type = x.XInternAtom(display, b"WM_PROTOCOLS", 0)
                        event.client.data[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
                        if not x.XSendEvent(display, window, 0, 0, c.byref(event)):
                            raise AssertionError("Could not deliver the normal window-close request")
                        x.XFlush(display)
                        code = process.wait(timeout=30)
                        output.seek(0)
                        text = output.read().decode(errors="replace")
                        if code != 0:
                            raise AssertionError(f"Normal close exited {code}\n{text}")
                        print(f"native close {attempt + 1}/3: exit 0", flush=True)
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait()
    finally:
        x.XCloseDisplay(display)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: application_shutdown.py BINARY")
    check_shutdown(Path(sys.argv[1]).resolve(strict=True))
