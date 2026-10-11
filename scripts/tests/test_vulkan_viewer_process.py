"""Private desktop process isolation, capture and process-tree timeout checks."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from vulkan_viewer_process import run_viewer_process, process_isolation_evidence


class ViewerProcessTests(unittest.TestCase):
    def test_requires_bounded_timeout(self):
        for timeout in (None, 0, -1, float('inf'), float('nan')):
            with self.assertRaises(ValueError):
                run_viewer_process(['unused'], '.', {}, timeout)

    def test_non_windows_preserves_subprocess_capture_contract(self):
        expected = subprocess.CompletedProcess(['probe'], 3, b'output', None)
        with patch('vulkan_viewer_process.os.name', 'posix'), patch('vulkan_viewer_process.subprocess.run', return_value=expected) as execute:
            result = run_viewer_process(['probe'], 'workspace', {'SETTING': 'value'}, 17)
        execute.assert_called_once_with(['probe'], cwd='workspace', env={'SETTING': 'value'},
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=17)
        self.assertIs(result, expected)
        self.assertFalse(result.desktop_isolated)
        self.assertEqual(process_isolation_evidence(result), process_isolation_evidence(None))

    def test_setup_failure_evidence_is_json_safe(self):
        self.assertEqual(json.loads(json.dumps(process_isolation_evidence(None))), {
            'desktop_isolated': False, 'desktop_identifier': None, 'window_station_identifier': None})

    @unittest.skipUnless(os.name == 'nt', 'Windows private desktop qualification')
    def test_real_private_desktop_capture_and_unchanged_input_desktop(self):
        before = input_desktop_name()
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.GetForegroundWindow.restype = wintypes.HWND
        foreground_before = user.GetForegroundWindow()
        clipboard_before = user.GetClipboardSequenceNumber()
        original = dict(os.environ)
        env = dict(original)
        command = [sys.executable, '-c', r'''
import ctypes,json,os,sys
from ctypes import wintypes
user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32')
user.GetThreadDesktop.argtypes=[wintypes.DWORD];user.GetThreadDesktop.restype=wintypes.HANDLE
user.GetUserObjectInformationW.argtypes=[wintypes.HANDLE,ctypes.c_int,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD)]
desktop=user.GetThreadDesktop(kernel.GetCurrentThreadId())
text=ctypes.create_unicode_buffer(256);needed=wintypes.DWORD()
assert user.GetUserObjectInformationW(desktop,2,text,ctypes.sizeof(text),ctypes.byref(needed))
actual_desktop=text.value
user.GetProcessWindowStation.restype=wintypes.HANDLE
assert user.GetUserObjectInformationW(user.GetProcessWindowStation(),2,text,ctypes.sizeof(text),ctypes.byref(needed))
print(json.dumps(dict(marker=os.environ['VS_VULKAN_TEST_DESKTOP'],id=os.environ['VS_VULKAN_TEST_DESKTOP_ID'],actual=actual_desktop,station=text.value,station_id=os.environ['VS_VULKAN_TEST_WINDOW_STATION_ID'])),flush=True)
# Write only the private station's clipboard; never read the interactive one.
user.CreateWindowExW.argtypes=[wintypes.DWORD,wintypes.LPCWSTR,wintypes.LPCWSTR,wintypes.DWORD,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.HWND,wintypes.HANDLE,wintypes.HANDLE,ctypes.c_void_p]
user.CreateWindowExW.restype=wintypes.HWND
window=user.CreateWindowExW(0,'STATIC','private clipboard witness',0,0,0,1,1,None,None,None,None)
assert window
user.OpenClipboard.argtypes=[wintypes.HWND]
assert user.OpenClipboard(window)
try:
    assert user.EmptyClipboard()
    value=ctypes.create_unicode_buffer('private clipboard witness')
    kernel.GlobalAlloc.argtypes=[wintypes.UINT,ctypes.c_size_t];kernel.GlobalAlloc.restype=wintypes.HANDLE
    kernel.GlobalLock.argtypes=[wintypes.HANDLE];kernel.GlobalLock.restype=ctypes.c_void_p
    kernel.GlobalUnlock.argtypes=[wintypes.HANDLE]
    user.SetClipboardData.argtypes=[wintypes.UINT,wintypes.HANDLE];user.SetClipboardData.restype=wintypes.HANDLE
    data=kernel.GlobalAlloc(2,ctypes.sizeof(value));assert data
    pointer=kernel.GlobalLock(data);assert pointer
    ctypes.memmove(pointer,value,ctypes.sizeof(value));kernel.GlobalUnlock(data)
    assert user.SetClipboardData(13,data)
finally:
    user.CloseClipboard();user.DestroyWindow.argtypes=[wintypes.HWND];user.DestroyWindow(window)
print('combined stderr',file=sys.stderr,flush=True)
sys.exit(7)
''']
        result = run_viewer_process(command, os.getcwd(), env, 10)
        self.assertEqual(result.returncode, 7)
        self.assertIn(b'combined stderr', result.stdout)
        record = json.loads(result.stdout.splitlines()[0])
        self.assertEqual(record['marker'], 'isolated')
        self.assertTrue(record['id'].startswith('vs-vulkan-'))
        self.assertEqual(record['actual'], record['id'])
        self.assertEqual(result.desktop_identifier, record['id'])
        self.assertEqual(record['station'], record['station_id'])
        self.assertEqual(result.window_station_identifier, record['station_id'])
        self.assertNotEqual(record['station'].lower(), 'winsta0')
        self.assertTrue(result.desktop_isolated)
        self.assertEqual(input_desktop_name(), before)
        self.assertEqual(user.GetForegroundWindow(), foreground_before)
        self.assertEqual(user.GetClipboardSequenceNumber(), clipboard_before)
        self.assertEqual(env, original)
        self.assertEqual(dict(os.environ), original)

    @unittest.skipUnless(os.name == 'nt', 'Windows Job process-tree qualification')
    def test_timeout_captures_output_and_kills_child_process(self):
        command = [sys.executable, '-c', '''
import subprocess,sys,time
child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])
print(child.pid,flush=True)
print('timeout evidence',flush=True)
time.sleep(30)
''']
        with self.assertRaises(subprocess.TimeoutExpired) as raised:
            run_viewer_process(command, os.getcwd(), dict(os.environ), 2)
        error = raised.exception
        self.assertIn(b'timeout evidence', error.stdout)
        self.assertTrue(error.desktop_isolated)
        pid = int(error.stdout.splitlines()[0])
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        process = kernel.OpenProcess(0x100000, False, pid)
        if process:
            try:
                self.assertEqual(kernel.WaitForSingleObject(process, 0), 0)
            finally:
                kernel.CloseHandle(process)

    @unittest.skipUnless(os.name == 'nt', 'Windows fail-closed qualification')
    def test_desktop_creation_failure_never_uses_foreground_fallback(self):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        user = ctypes.WinDLL('user32', use_last_error=True)

        def unavailable(*unused):
            ctypes.set_last_error(5)
            return None

        isolated_user = SimpleNamespace(CreateDesktopW=user.CreateDesktopW, CloseDesktop=user.CloseDesktop,
                                       CreateWindowStationW=unavailable, GetProcessWindowStation=user.GetProcessWindowStation,
                                       SetProcessWindowStation=user.SetProcessWindowStation, CloseWindowStation=user.CloseWindowStation)
        with patch('vulkan_viewer_process.ctypes.WinDLL',
                   side_effect=lambda name, **unused: isolated_user if name == 'user32' else kernel), \
                patch('vulkan_viewer_process.subprocess.run') as foreground:
            with self.assertRaises(OSError) as raised:
                run_viewer_process(['unused'], os.getcwd(), dict(os.environ), 10)
        foreground.assert_not_called()
        self.assertIn('no foreground fallback', str(raised.exception))

    @unittest.skipUnless(os.name == 'nt', 'Windows station restoration qualification')
    def test_desktop_setup_failure_restores_station_and_releases_lock(self):
        import vulkan_viewer_process as launcher
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        user = ctypes.WinDLL('user32', use_last_error=True)
        def unavailable(*unused):
            ctypes.set_last_error(5)
            return None
        set_station = Mock(return_value=True)
        close_station = Mock(return_value=True)
        create_station = Mock(return_value=123)
        isolated_user = SimpleNamespace(
            CreateDesktopW=unavailable, CloseDesktop=user.CloseDesktop,
            CreateWindowStationW=create_station, GetProcessWindowStation=Mock(return_value=456),
            SetProcessWindowStation=set_station, CloseWindowStation=close_station)
        with patch('vulkan_viewer_process.ctypes.WinDLL',
                   side_effect=lambda name, **unused: isolated_user if name == 'user32' else kernel):
            with self.assertRaises(OSError):
                run_viewer_process(['unused'], os.getcwd(), {}, 10)
        self.assertEqual(set_station.call_args_list, [unittest.mock.call(123), unittest.mock.call(456)])
        close_station.assert_called_once_with(123)
        self.assertEqual(create_station.call_args.args[1:3], (1, 0x37f))
        self.assertFalse(launcher._window_station_lock.locked())

    def test_no_input_desktop_switch_or_foreground_fallback(self):
        source = (Path(__file__).resolve().parents[1] / 'vulkan_viewer_process.py').read_text()
        self.assertNotIn('SwitchDesktop', source)
        self.assertNotIn('SetThreadDesktop', source)
        self.assertIn("startup.startup.desktop = station_name + '\\\\' + name", source)
        self.assertIn('create_station(station_name, 1,', source)
        self.assertIn('set_station(original_station)', source)
        self.assertIn('check(assign_job(job, process.process)', source)


def input_desktop_name():
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.OpenInputDesktop.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    user.OpenInputDesktop.restype = wintypes.HANDLE
    user.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                               wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    user.CloseDesktop.argtypes = [wintypes.HANDLE]
    desktop = user.OpenInputDesktop(0, False, 1)
    if not desktop:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        text = ctypes.create_unicode_buffer(256)
        needed = wintypes.DWORD()
        if not user.GetUserObjectInformationW(desktop, 2, text, ctypes.sizeof(text), ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        return text.value
    finally:
        user.CloseDesktop(desktop)


if __name__ == '__main__':
    unittest.main()
