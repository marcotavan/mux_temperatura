"""Exercise the firmware's actual C policy on Windows, without ESP hardware.

Usage: python tests/test_control_policy.py [path/to/MSVC/bin/Hostx64/x64]
MSVC builds a freestanding DLL; no ESP hardware or C runtime is needed.
"""
import ctypes
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
if len(sys.argv) > 1:
    COMPILER_BIN = pathlib.Path(sys.argv.pop(1))
else:
    vswhere = pathlib.Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
    compiler = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-find",
        "VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe"], text=True).strip().splitlines()[0]
    COMPILER_BIN = pathlib.Path(compiler).parent


class Retry(ctypes.Structure):
    _fields_ = [("cycle_start", ctypes.c_int64), ("delay_s", ctypes.c_uint)]


class Button(ctypes.Structure):
    _fields_ = [("raw", ctypes.c_bool), ("down", ctypes.c_bool), ("armed", ctypes.c_bool),
                ("raw_since", ctypes.c_int64), ("pressed_since", ctypes.c_int64)]


class ControlPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        path = pathlib.Path(cls.temp.name)
        source = path / "policy.c"
        source.write_text('''#include "control_policy.h"
unsigned retry(ct_retry_policy_t *p, int64_t now) { return ct_retry_delay(p, now); }
int button(ct_button_t *p, int pressed, int64_t now) { return ct_button_update(p, pressed != 0, now); }
''', encoding="utf-8")
        subprocess.run([str(COMPILER_BIN / "cl.exe"), "/nologo", "/std:c11", "/Zl", "/GS-",
                        "/I" + str(COMPILER_BIN.parents[2] / "include"), "/I" + str(ROOT / "main"),
                        "/c", str(source), "/Fo" + str(path / "policy.obj")], check=True)
        subprocess.run([str(COMPILER_BIN / "link.exe"), "/nologo", "/dll", "/noentry",
                        "/nodefaultlib", "/export:retry", "/export:button",
                        f"/out:{path / 'policy.dll'}", str(path / "policy.obj")], check=True)
        cls.dll = ctypes.CDLL(str(path / "policy.dll"))
        cls.dll.retry.argtypes = [ctypes.POINTER(Retry), ctypes.c_int64]
        cls.dll.retry.restype = ctypes.c_uint
        cls.dll.button.argtypes = [ctypes.POINTER(Button), ctypes.c_int, ctypes.c_int64]
        cls.dll.button.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        # Windows keeps loaded DLLs locked until explicitly unloaded.
        ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.FreeLibrary(cls.dll._handle)
        cls.temp.cleanup()

    def update(self, button, pressed, now):
        return self.dll.button(ctypes.byref(button), pressed, now)

    def armed_button(self):
        button = Button()
        self.update(button, False, 100)
        return button

    def test_exponential_delay_is_capped(self):
        policy = Retry()
        actual = [self.dll.retry(ctypes.byref(policy), 1000) for _ in range(12)]
        self.assertEqual(actual, [0, 1, 2, 4, 8, 16, 32, 60, 60, 60, 60, 60])

    def test_ten_minute_cycle_and_64_bit_uptime(self):
        start = 2**33
        policy = Retry(start, 60)
        self.assertEqual(self.dll.retry(ctypes.byref(policy), start + 599999), 60)
        self.assertEqual(self.dll.retry(ctypes.byref(policy), start + 600000), 0)
        self.assertEqual(self.dll.retry(ctypes.byref(policy), start + 600001), 1)

    def test_actions_only_on_release_and_thresholds(self):
        for duration, expected in [(4999, 0), (5000, 1), (14999, 1), (15000, 2), (60000, 2)]:
            with self.subTest(duration=duration):
                button = self.armed_button()
                self.assertEqual(self.update(button, True, 200), 0)
                self.assertEqual(self.update(button, True, 250), 0)
                self.assertEqual(self.update(button, True, 200 + duration - 1), 0)
                self.assertEqual(self.update(button, False, 200 + duration), 0)
                self.assertEqual(self.update(button, False, 250 + duration), expected)
                self.assertEqual(self.update(button, False, 300 + duration), 0)

    def test_held_at_boot_never_resets(self):
        button = Button()
        for now in [0, 50, 20000]:
            self.assertEqual(self.update(button, True, now), 0)
        self.assertEqual(self.update(button, False, 21000), 0)
        self.assertEqual(self.update(button, False, 21050), 0)
        self.assertTrue(button.armed)

    def test_release_bounce_does_not_trigger_reset(self):
        button = self.armed_button()
        self.update(button, True, 200)
        self.update(button, True, 250)
        self.assertEqual(self.update(button, False, 16000), 0)
        self.assertEqual(self.update(button, True, 16020), 0)
        self.assertEqual(self.update(button, True, 16070), 0)
        self.assertEqual(self.update(button, False, 17000), 0)
        self.assertEqual(self.update(button, False, 17050), 2)


if __name__ == "__main__":
    unittest.main()
