"""Run Windows graphics qualification on a private window station and never-activated desktop."""
import ctypes
from ctypes import wintypes
import math
import os
import subprocess
import threading
import uuid

_window_station_lock = threading.Lock()


def run_viewer_process(command, cwd, env, timeout):
    """Capture combined byte output, with a finite deadline and no GUI fallback."""
    if not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('Viewer process requires a finite positive timeout')
    if os.name != 'nt':
        result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        result.window_station_identifier = None
        result.desktop_identifier = None
        result.desktop_isolated = False
        return result
    return _run_windows(command, cwd, env, timeout)


def process_isolation_evidence(result):
    """Return JSON-safe isolation provenance, including setup failures."""
    return {
        'desktop_isolated': bool(getattr(result, 'desktop_isolated', False)),
        'desktop_identifier': getattr(result, 'desktop_identifier', None),
        'window_station_identifier': getattr(result, 'window_station_identifier', None),
    }


def _run_windows(command, cwd, env, timeout):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    user = ctypes.WinDLL('user32', use_last_error=True)
    size_t = ctypes.c_size_t
    handle = wintypes.HANDLE

    class Security(ctypes.Structure):
        _fields_ = [('length', wintypes.DWORD), ('descriptor', ctypes.c_void_p),
                    ('inherit', wintypes.BOOL)]

    class Startup(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('reserved', wintypes.LPWSTR),
                    ('desktop', wintypes.LPWSTR), ('title', wintypes.LPWSTR),
                    ('x', wintypes.DWORD), ('y', wintypes.DWORD),
                    ('width', wintypes.DWORD), ('height', wintypes.DWORD),
                    ('chars_x', wintypes.DWORD), ('chars_y', wintypes.DWORD),
                    ('fill', wintypes.DWORD), ('flags', wintypes.DWORD),
                    ('show', wintypes.WORD), ('reserved_bytes', wintypes.WORD),
                    ('reserved_data', ctypes.c_void_p), ('stdin', handle),
                    ('stdout', handle), ('stderr', handle)]

    class StartupEx(ctypes.Structure):
        _fields_ = [('startup', Startup), ('attributes', ctypes.c_void_p)]

    class Process(ctypes.Structure):
        _fields_ = [('process', handle), ('thread', handle),
                    ('pid', wintypes.DWORD), ('tid', wintypes.DWORD)]

    class BasicLimits(ctypes.Structure):
        _fields_ = [('process_time', ctypes.c_longlong), ('job_time', ctypes.c_longlong),
                    ('flags', wintypes.DWORD), ('min_working', size_t), ('max_working', size_t),
                    ('process_limit', wintypes.DWORD), ('affinity', size_t),
                    ('priority', wintypes.DWORD), ('scheduling', wintypes.DWORD)]

    class IOLimits(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in
                    ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]

    class JobLimits(ctypes.Structure):
        _fields_ = [('basic', BasicLimits), ('io', IOLimits), ('process_memory', size_t),
                    ('job_memory', size_t), ('peak_process_memory', size_t), ('peak_job_memory', size_t)]

    def bind(dll, name, result, arguments):
        function = getattr(dll, name)
        function.restype = result
        function.argtypes = arguments
        return function

    create_desktop = bind(user, 'CreateDesktopW', handle,
                          [wintypes.LPCWSTR, ctypes.c_void_p, ctypes.c_void_p,
                           wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p])
    close_desktop = bind(user, 'CloseDesktop', wintypes.BOOL, [handle])
    create_station = bind(user, 'CreateWindowStationW', handle,
                          [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p])
    get_station = bind(user, 'GetProcessWindowStation', handle, [])
    set_station = bind(user, 'SetProcessWindowStation', wintypes.BOOL, [handle])
    close_station = bind(user, 'CloseWindowStation', wintypes.BOOL, [handle])
    close = bind(kernel, 'CloseHandle', wintypes.BOOL, [handle])
    create_job = bind(kernel, 'CreateJobObjectW', handle, [ctypes.c_void_p, wintypes.LPCWSTR])
    configure_job = bind(kernel, 'SetInformationJobObject', wintypes.BOOL,
                         [handle, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD])
    assign_job = bind(kernel, 'AssignProcessToJobObject', wintypes.BOOL, [handle, handle])
    terminate_job = bind(kernel, 'TerminateJobObject', wintypes.BOOL, [handle, wintypes.UINT])
    terminate = bind(kernel, 'TerminateProcess', wintypes.BOOL, [handle, wintypes.UINT])
    create_pipe = bind(kernel, 'CreatePipe', wintypes.BOOL,
                       [ctypes.POINTER(handle), ctypes.POINTER(handle), ctypes.POINTER(Security), wintypes.DWORD])
    set_handle = bind(kernel, 'SetHandleInformation', wintypes.BOOL, [handle, wintypes.DWORD, wintypes.DWORD])
    open_null = bind(kernel, 'CreateFileW', handle,
                     [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(Security),
                      wintypes.DWORD, wintypes.DWORD, handle])
    init_attributes = bind(kernel, 'InitializeProcThreadAttributeList', wintypes.BOOL,
                           [ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(size_t)])
    update_attributes = bind(kernel, 'UpdateProcThreadAttribute', wintypes.BOOL,
                             [ctypes.c_void_p, wintypes.DWORD, size_t, ctypes.c_void_p,
                              size_t, ctypes.c_void_p, ctypes.c_void_p])
    delete_attributes = bind(kernel, 'DeleteProcThreadAttributeList', None, [ctypes.c_void_p])
    create_process = bind(kernel, 'CreateProcessW', wintypes.BOOL,
                          [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
                           wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
                           ctypes.POINTER(StartupEx), ctypes.POINTER(Process)])
    resume = bind(kernel, 'ResumeThread', wintypes.DWORD, [handle])
    wait = bind(kernel, 'WaitForSingleObject', wintypes.DWORD, [handle, wintypes.DWORD])
    exit_code = bind(kernel, 'GetExitCodeProcess', wintypes.BOOL, [handle, ctypes.POINTER(wintypes.DWORD)])
    read = bind(kernel, 'ReadFile', wintypes.BOOL,
                [handle, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p])

    def check(ok, operation):
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error(), operation + ' (private window station and desktop required; no foreground fallback)')

    station = desktop = job = stdin = None
    original_station = None
    station_changed = False
    station_locked = False
    pipe_read = handle()
    pipe_write = handle()
    process = Process()
    attributes = None
    reader = None
    chunks = []
    read_errors = []
    name = 'vs-vulkan-' + uuid.uuid4().hex
    station_name = 'vs-station-' + uuid.uuid4().hex
    child_env = dict(os.environ if env is None else env)
    child_env['VS_VULKAN_TEST_DESKTOP'] = 'isolated'
    child_env['VS_VULKAN_TEST_DESKTOP_ID'] = name
    child_env['VS_VULKAN_TEST_WINDOW_STATION_ID'] = station_name
    environment = ctypes.create_unicode_buffer('\0'.join(
        str(key) + '=' + str(value) for key, value in sorted(child_env.items(), key=lambda pair: pair[0].upper())) + '\0\0')
    command_line = ctypes.create_unicode_buffer(command if isinstance(command, str)
                                               else subprocess.list2cmdline([str(part) for part in command]))
    assigned = False
    try:
        # Window stations own the clipboard and global atoms. Isolate these too.
        # This changes only the launcher process's station while creating the
        # child's objects; never change any caller/input thread desktop.
        _window_station_lock.acquire()
        station_locked = True
        original_station = get_station()
        check(original_station, 'GetProcessWindowStation')
        station = create_station(station_name, 1, 0x037F, None)  # CWF_CREATE_ONLY; never reuse another station
        check(station, 'CreateWindowStationW')
        check(set_station(station), 'SetProcessWindowStation private')
        station_changed = True
        desktop = create_desktop(name, None, None, 0, 0x01FF, None)
        check(desktop, 'CreateDesktopW')
        job = create_job(None, None)
        check(job, 'CreateJobObjectW')
        limits = JobLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        check(configure_job(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)), 'SetInformationJobObject')
        security = Security(ctypes.sizeof(Security), None, True)
        check(create_pipe(ctypes.byref(pipe_read), ctypes.byref(pipe_write), ctypes.byref(security), 0), 'CreatePipe')
        check(set_handle(pipe_read, 1, 0), 'SetHandleInformation')
        stdin = open_null('NUL', 0x80000000, 3, ctypes.byref(security), 3, 0, None)
        check(stdin and stdin != ctypes.c_void_p(-1).value, 'CreateFileW(NUL)')
        attribute_size = size_t()
        init_attributes(None, 1, 0, ctypes.byref(attribute_size))
        check(attribute_size.value, 'InitializeProcThreadAttributeList size')
        attribute_storage = ctypes.create_string_buffer(attribute_size.value)
        check(init_attributes(attribute_storage, 1, 0, ctypes.byref(attribute_size)), 'InitializeProcThreadAttributeList')
        attributes = attribute_storage
        # Inherit only our pipe and NUL, not unrelated caller handles.
        inherited = (handle * 2)(stdin, pipe_write.value)
        check(update_attributes(attributes, 0, 0x00020002, inherited, ctypes.sizeof(inherited), None, None),
              'UpdateProcThreadAttribute(HANDLE_LIST)')
        startup = StartupEx()
        startup.startup.cb = ctypes.sizeof(startup)
        startup.startup.desktop = station_name + '\\' + name
        startup.startup.flags = 0x100  # STARTF_USESTDHANDLES
        startup.startup.stdin = stdin
        startup.startup.stdout = startup.startup.stderr = pipe_write.value
        startup.attributes = ctypes.cast(attributes, ctypes.c_void_p)
        flags = 0x4 | 0x400 | 0x80000 | 0x08000000  # suspended, Unicode env, extended startup, no console
        check(create_process(None, command_line, None, None, True, flags, environment,
                             str(cwd) if cwd is not None else None, ctypes.byref(startup), ctypes.byref(process)),
              'CreateProcessW')
        check(assign_job(job, process.process), 'AssignProcessToJobObject')
        assigned = True
        check(set_station(original_station), 'Restore launcher window station')
        station_changed = False
        _window_station_lock.release()
        station_locked = False
        close(pipe_write)
        pipe_write = handle()
        close(stdin)
        stdin = None

        def capture():
            buffer = ctypes.create_string_buffer(65536)
            count = wintypes.DWORD()
            while True:
                if not read(pipe_read, buffer, len(buffer), ctypes.byref(count), None):
                    error = ctypes.get_last_error()
                    if error != 109:  # ERROR_BROKEN_PIPE
                        read_errors.append(error)
                    return
                if not count.value:
                    return
                chunks.append(buffer.raw[:count.value])

        reader = threading.Thread(target=capture, name='vulkan-private-desktop-output', daemon=True)
        reader.start()
        check(resume(process.thread) != 0xFFFFFFFF, 'ResumeThread')
        outcome = wait(process.process, min(0xFFFFFFFE, math.ceil(timeout * 1000)))
        timed_out = outcome == 258
        if timed_out:
            check(terminate_job(job, 1), 'TerminateJobObject')
            check(wait(process.process, 5000) == 0, 'WaitForSingleObject after timeout')
        else:
            check(outcome == 0, 'WaitForSingleObject')
        code = wintypes.DWORD()
        check(exit_code(process.process, ctypes.byref(code)), 'GetExitCodeProcess')
        # Closing the job also retires children retaining the stdout pipe.
        close(job)
        job = None
        reader.join(5)
        if reader.is_alive():
            raise RuntimeError('Private-desktop output did not close after process-tree teardown')
        output = b''.join(chunks)
        if timed_out:
            error = subprocess.TimeoutExpired(command, timeout, output=output)
            error.desktop_identifier = name
            error.desktop_isolated = True
            error.window_station_identifier = station_name
            raise error
        if read_errors:
            raise ctypes.WinError(read_errors[0], 'ReadFile private-desktop output')
        result = subprocess.CompletedProcess(command, code.value, stdout=output, stderr=None)
        result.desktop_identifier = name
        result.desktop_isolated = True
        result.window_station_identifier = station_name
        return result
    finally:
        if process.process and not assigned:
            terminate(process.process, 1)  # setup failure must not leave a suspended process
        if job:
            close(job)  # kill-on-close includes every descendant
        if reader is not None and reader.is_alive():
            reader.join(5)
        for value in (process.thread, process.process, stdin, pipe_write, pipe_read):
            if value and getattr(value, 'value', value):
                close(value)
        if attributes is not None:
            delete_attributes(attributes)
        if desktop:
            close_desktop(desktop)
        restore_error = None
        if station_changed and not set_station(original_station):
            restore_error = ctypes.WinError(ctypes.get_last_error(), 'Restore launcher window station')
        if station_locked:
            _window_station_lock.release()
        if station:
            close_station(station)
        if restore_error is not None:
            raise restore_error
