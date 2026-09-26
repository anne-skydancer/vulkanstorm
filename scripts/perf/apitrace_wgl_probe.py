"""Windows-only hidden-window loader smoke test; no viewer or user input.

Run under apitrace with APITRACE_OPENGL_DLL pointing at the same Mesa DLL
passed here. Exercises explicit loading as used by Vulkanstorm.
"""
import ctypes as c
from ctypes import wintypes as w
import json
import sys
from pathlib import Path


class PFD(c.Structure):
    _fields_ = [("nSize", w.WORD), ("nVersion", w.WORD), ("dwFlags", w.DWORD)] + [
        (name, w.BYTE) for name in ("iPixelType", "cColorBits", "cRedBits", "cRedShift", "cGreenBits", "cGreenShift",
                                  "cBlueBits", "cBlueShift", "cAlphaBits", "cAlphaShift", "cAccumBits", "cAccumRedBits",
                                  "cAccumGreenBits", "cAccumBlueBits", "cAccumAlphaBits", "cDepthBits", "cStencilBits",
                                  "cAuxBuffers", "iLayerType", "bReserved")] + [
        (name, w.DWORD) for name in ("dwLayerMask", "dwVisibleMask", "dwDamageMask")]


def main():
    mesa = Path(sys.argv[1]).resolve(strict=True)
    kernel, user = c.WinDLL("kernel32", use_last_error=True), c.WinDLL("user32", use_last_error=True)

    def api(dll, name, result, *args):
        fn = getattr(dll, name)
        fn.restype, fn.argtypes = result, args
        return fn

    api(kernel, "SetDllDirectoryW", w.BOOL, w.LPCWSTR)(str(mesa.parent))
    gl = c.WinDLL(str(mesa), winmode=8)
    window = api(user, "CreateWindowExW", w.HWND, w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                 c.c_int, c.c_int, c.c_int, c.c_int, w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID)(
                     0, "STATIC", "VulkanStorm apitrace smoke test", 0, 0, 0, 32, 32, None, None, None, None)
    if not window:
        raise c.WinError(c.get_last_error())
    dc = api(user, "GetDC", w.HDC, w.HWND)(window)
    context = None
    try:
        pfd = PFD()
        pfd.nSize, pfd.nVersion, pfd.dwFlags = c.sizeof(PFD), 1, 0x25
        pfd.cColorBits, pfd.cDepthBits = 24, 24
        fmt = api(gl, "wglChoosePixelFormat", c.c_int, w.HDC, c.POINTER(PFD))(dc, c.byref(pfd))
        if not fmt or not api(gl, "wglSetPixelFormat", w.BOOL, w.HDC, c.c_int, c.POINTER(PFD))(dc, fmt, c.byref(pfd)):
            raise RuntimeError("Pixel format setup failed")
        context = api(gl, "wglCreateContext", w.HANDLE, w.HDC)(dc)
        current = api(gl, "wglMakeCurrent", w.BOOL, w.HDC, w.HANDLE)
        if not context or not current(dc, context):
            raise RuntimeError("WGL context creation failed")
        get_string = api(gl, "glGetString", c.c_char_p, c.c_uint)
        strings = {name: get_string(token).decode("utf-8") for name, token in
                   (("vendor", 0x1F00), ("renderer", 0x1F01), ("version", 0x1F02))}
        print(json.dumps(strings), flush=True)
        if "zink" not in strings["renderer"].lower():
            raise RuntimeError("Probe is not running Zink")
        proc = api(gl, "wglGetProcAddress", c.c_void_p, c.c_char_p)
        def extension(name, result, *args):
            address = proc(name.encode("ascii"))
            if address in (None, 0, 1, 2, 3, c.c_void_p(-1).value):
                raise RuntimeError("Missing GL entry point: " + name)
            return c.WINFUNCTYPE(result, *args)(address)
        extension("glActiveTexture", None, c.c_uint)(0x84C0)
        texture = c.c_uint()
        api(gl, "glGenTextures", None, c.c_int, c.POINTER(c.c_uint))(1, c.byref(texture))
        # Re-establish active unit after resource-generation invalidation.
        extension("glActiveTexture", None, c.c_uint)(0x84C0)
        bind = api(gl, "glBindTexture", None, c.c_uint, c.c_uint)
        bind(0x0DE1, texture.value)
        bind(0x0DE1, texture.value)
        vao, buffer = c.c_uint(), c.c_uint()
        extension("glGenVertexArrays", None, c.c_int, c.POINTER(c.c_uint))(1, c.byref(vao))
        extension("glGenBuffers", None, c.c_int, c.POINTER(c.c_uint))(1, c.byref(buffer))
        extension("glBindVertexArray", None, c.c_uint)(vao.value)
        extension("glBindBuffer", None, c.c_uint, c.c_uint)(0x8892, buffer.value)
        data = (c.c_float * 4)(0, 0, 0, 1)
        extension("glBufferData", None, c.c_uint, c.c_ssize_t, c.c_void_p, c.c_uint)(0x8892, c.sizeof(data), data, 0x88E4)
        pointer = extension("glVertexAttribPointer", None, c.c_uint, c.c_int, c.c_uint, c.c_ubyte, c.c_int, c.c_void_p)
        pointer(0, 3, 0x1406, 0, 16, None)
        pointer(0, 3, 0x1406, 0, 16, None)
        api(gl, "glClearColor", None, c.c_float, c.c_float, c.c_float, c.c_float)(0, 0, 0, 1)
        api(gl, "glClear", None, c.c_uint)(0x4000)
        api(gl, "wglSwapBuffers", w.BOOL, w.HDC)(dc)
        error = api(gl, "glGetError", c.c_uint)()
        if error:
            raise RuntimeError(f"GL error: {error:#x}")
    finally:
        if context:
            api(gl, "wglMakeCurrent", w.BOOL, w.HDC, w.HANDLE)(None, None)
            api(gl, "wglDeleteContext", w.BOOL, w.HANDLE)(context)
        api(user, "ReleaseDC", c.c_int, w.HWND, w.HDC)(window, dc)
        api(user, "DestroyWindow", w.BOOL, w.HWND)(window)


if __name__ == "__main__":
    main()
