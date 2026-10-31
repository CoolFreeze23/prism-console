# A minimal virtual gamepad through /dev/uinput (raw ioctls, no python-evdev), used by the handheld checks to
# press View + Menu the same way a real pad would reach Prism Console's chord watcher.
import fcntl, os, struct, time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_DEV_CREATE, UI_DEV_DESTROY = 0x40045564, 0x40045565, 0x5501, 0x5502
UI_DEV_SETUP = (1 << 30) | (92 << 16) | (ord('U') << 8) | 3  # _IOW('U', 3, struct uinput_setup)
UI_ABS_SETUP = (1 << 30) | (28 << 16) | (ord('U') << 8) | 4  # _IOW('U', 4, struct uinput_abs_setup)
UI_SET_ABSBIT = 0x40045567
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN_SOUTH, BTN_EAST, BTN_SELECT, BTN_START = 0x130, 0x131, 0x13a, 0x13b
ABS_X, ABS_Y = 0x00, 0x01


class VirtualPad:
    def __init__(self, name='Prism Console test pad', stick=False):
        """stick=True adds B and a left stick (ABS_X/ABS_Y, -32768..32767) for navigation checks."""
        self.fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        for key in (BTN_SOUTH, BTN_SELECT, BTN_START) + ((BTN_EAST,) if stick else ()):
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, key)
        if stick:
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
            for axis in (ABS_X, ABS_Y):
                fcntl.ioctl(self.fd, UI_SET_ABSBIT, axis)
                fcntl.ioctl(self.fd, UI_ABS_SETUP, struct.pack('<Hxxiiiiii', axis, 0, -32768, 32767, 16, 128, 0))
        setup = struct.pack('<HHHH80sI', 0x03, 0x1234, 0x5678, 1, name.encode()[:79], 0)
        fcntl.ioctl(self.fd, UI_DEV_SETUP, setup)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)

    def _emit(self, kind, code, value):
        os.write(self.fd, struct.pack('<qqHHi', 0, 0, kind, code, value))

    def buttons(self, codes, down):
        for code in codes:
            self._emit(EV_KEY, code, 1 if down else 0)
        self._emit(EV_SYN, 0, 0)

    def tap(self, code, hold=0.12):
        self.buttons((code,), True)
        time.sleep(hold)
        self.buttons((code,), False)

    def push(self, axis, value, hold=0.35):
        """Holds the stick on one axis (-1.0..1.0) for a moment, then centres it."""
        self._emit(EV_ABS, axis, int(value * 32767)); self._emit(EV_SYN, 0, 0)
        time.sleep(hold)
        self._emit(EV_ABS, axis, 0); self._emit(EV_SYN, 0, 0)

    def chord(self, hold=1.4):
        self.buttons((BTN_SELECT, BTN_START), True)
        time.sleep(hold)
        self.buttons((BTN_SELECT, BTN_START), False)

    def close(self):
        fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        os.close(self.fd)
