# A minimal virtual gamepad through /dev/uinput (raw ioctls, no python-evdev), used by the handheld checks to
# press View + Menu the same way a real pad would reach Prism Console's chord watcher.
import fcntl, os, struct, time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_DEV_CREATE, UI_DEV_DESTROY = 0x40045564, 0x40045565, 0x5501, 0x5502
UI_DEV_SETUP = (1 << 30) | (92 << 16) | (ord('U') << 8) | 3  # _IOW('U', 3, struct uinput_setup)
EV_SYN, EV_KEY = 0, 1
BTN_SOUTH, BTN_SELECT, BTN_START = 0x130, 0x13a, 0x13b


class VirtualPad:
    def __init__(self, name='Prism Console test pad'):
        self.fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        for key in (BTN_SOUTH, BTN_SELECT, BTN_START):
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, key)
        setup = struct.pack('<HHHH80sI', 0x03, 0x1234, 0x5678, 1, name.encode()[:79], 0)
        fcntl.ioctl(self.fd, UI_DEV_SETUP, setup)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)

    def _emit(self, kind, code, value):
        os.write(self.fd, struct.pack('<qqHHi', 0, 0, kind, code, value))

    def buttons(self, codes, down):
        for code in codes:
            self._emit(EV_KEY, code, 1 if down else 0)
        self._emit(EV_SYN, 0, 0)

    def chord(self, hold=1.4):
        self.buttons((BTN_SELECT, BTN_START), True)
        time.sleep(hold)
        self.buttons((BTN_SELECT, BTN_START), False)

    def close(self):
        fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        os.close(self.fd)
