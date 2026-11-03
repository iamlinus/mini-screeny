"""Finding the mini screeny board and sharing its serial port between the tools.

tools/companion.py holds the port all the time. Anything else that needs it (sim.py, flashing)
asks the companion to step aside first:

  PAUSE file  tools/out/.companion_pause   written by the tool, contains that tool's PID
  ACK file    tools/out/.companion_paused  written by the companion once it has closed the port

The companion ignores a pause file whose PID is dead, so a crashed sim.py never blocks it for good.
Both sides open the port with exclusive=True, so a forgotten pause fails loudly instead of the two
processes splitting the bytes of a framebuffer dump.
"""
import glob, os, time

import serial
import serial.tools.list_ports

ESPRESSIF_VID = 0x303A
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
PAUSE = os.path.join(OUT, ".companion_pause")
ACK = os.path.join(OUT, ".companion_paused")
LOCK = os.path.join(OUT, ".companion.lock")   # companion single-instance lock, contains its PID


def find_port():
    """SIM_PORT if set, else the Espressif USB serial/JTAG device, else the first usbmodem."""
    if os.environ.get("SIM_PORT"):
        return os.environ["SIM_PORT"]
    for p in serial.tools.list_ports.comports():
        if p.vid == ESPRESSIF_VID and p.device.startswith("/dev/cu."):
            return p.device
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def open_serial(port, timeout=2):
    return serial.Serial(port, 115200, timeout=timeout, exclusive=True)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _read_pid(path):
    try:
        with open(path) as f:
            return int(f.read().strip() or 0)
    except (OSError, ValueError):
        return 0


def companion_pid():
    pid = _read_pid(LOCK)
    return pid if pid and pid_alive(pid) else 0


def pause_holder():
    """PID of the live process that asked the companion to pause, or 0."""
    pid = _read_pid(PAUSE)
    return pid if pid and pid_alive(pid) else 0


class CompanionPaused:
    """Context manager: make the companion release the port while the block runs."""

    def __init__(self, wait=6.0):
        self.wait = wait
        self.mine = False

    def __enter__(self):
        if not companion_pid():
            return self
        os.makedirs(OUT, exist_ok=True)
        with open(PAUSE, "w") as f:
            f.write(str(os.getpid()))
        self.mine = True
        t = time.time()
        while time.time() - t < self.wait:
            if _read_pid(ACK) == companion_pid():
                return self
            time.sleep(0.1)
        self.__exit__(None, None, None)
        raise RuntimeError("companion did not release the port (tools/out/.companion_pause)")

    def __exit__(self, *exc):
        if self.mine and _read_pid(PAUSE) == os.getpid():
            try:
                os.remove(PAUSE)
            except OSError:
                pass
        self.mine = False
        return False
