"""Hidden-window Windows Core-context/shared-texture smoke test.

Use --dll for the bundled Mesa opengl32.dll; omission tests the system driver.
This checks driver capabilities, not complete viewer rendering correctness.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
from apitrace_wgl_probe import PFD

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--dll',type=Path)
    parser.add_argument('--minor',type=int,default=3)
    args=parser.parse_args()
    def api(dll,name,result,*types):
        fn=getattr(dll,name); fn.restype=result; fn.argtypes=types; return fn
    user=c.WinDLL('user32',use_last_error=True)
    gdi=c.WinDLL('gdi32',use_last_error=True)
    if args.dll:
        dll=args.dll.resolve(strict=True)
        api(c.WinDLL('kernel32'),'SetDllDirectoryW',w.BOOL,w.LPCWSTR)(str(dll.parent))
        gl=c.WinDLL(str(dll),winmode=8)
        choose=api(gl,'wglChoosePixelFormat',c.c_int,w.HDC,c.POINTER(PFD))
        set_format=api(gl,'wglSetPixelFormat',w.BOOL,w.HDC,c.c_int,c.POINTER(PFD))
    else:
        gl=c.WinDLL('opengl32')
        choose=api(gdi,'ChoosePixelFormat',c.c_int,w.HDC,c.POINTER(PFD))
        set_format=api(gdi,'SetPixelFormat',w.BOOL,w.HDC,c.c_int,c.POINTER(PFD))
    win=api(user,'CreateWindowExW',w.HWND,w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,
        c.c_int,c.c_int,c.c_int,c.c_int,w.HWND,w.HMENU,w.HINSTANCE,w.LPVOID)(
        0,'STATIC','Core context test',0,0,0,32,32,None,None,None,None)
    if not win: raise c.WinError(c.get_last_error())
    dc=api(user,'GetDC',w.HDC,w.HWND)(win)
    contexts=[]
    current=api(gl,'wglMakeCurrent',w.BOOL,w.HDC,w.HANDLE)
    try:
        pfd=PFD(); pfd.nSize=c.sizeof(pfd); pfd.nVersion=1; pfd.dwFlags=0x25
        pfd.cColorBits=24; pfd.cDepthBits=24
        fmt=choose(dc,c.byref(pfd))
        assert fmt and set_format(dc,fmt,c.byref(pfd))
        boot=api(gl,'wglCreateContext',w.HANDLE,w.HDC)(dc)
        assert boot; contexts.append(boot); assert current(dc,boot)
        proc=api(gl,'wglGetProcAddress',c.c_void_p,c.c_char_p)
        def extension(name,result,*types):
            p=proc(name.encode())
            assert p not in (None,0,1,2,3,c.c_void_p(-1).value),name
            return c.WINFUNCTYPE(result,*types)(p)
        create=extension('wglCreateContextAttribsARB',w.HANDLE,w.HDC,w.HANDLE,c.POINTER(c.c_int))
        attrs=(c.c_int*7)(0x2091,4,0x2092,args.minor,0x9126,1,0)
        main_context=create(dc,None,attrs)
        assert main_context,'Core context unavailable'
        contexts.append(main_context); assert current(dc,main_context)
        get=api(gl,'glGetIntegerv',None,c.c_uint,c.POINTER(c.c_int))
        values={}
        for name,token in [('major',0x821B),('minor',0x821C),('profile',0x9126)]:
            v=c.c_int(); get(token,c.byref(v)); values[name]=v.value
        assert (values['major'],values['minor']) >= (4,3)
        assert values['profile'] & 1
        for name in ['glDispatchCompute','glMemoryBarrier','glMultiDrawElementsIndirect']:
            extension(name,None)
        get_string=api(gl,'glGetString',c.c_char_p,c.c_uint)
        values.update({name:get_string(token).decode() for name,token in [('vendor',0x1F00),('renderer',0x1F01),('version',0x1F02)]})
        texture=c.c_uint()
        api(gl,'glGenTextures',None,c.c_int,c.POINTER(c.c_uint))(1,c.byref(texture))
        bind=api(gl,'glBindTexture',None,c.c_uint,c.c_uint)
        bind(0x0DE1,texture.value)
        pixel=(c.c_ubyte*4)(17,34,51,255)
        api(gl,'glTexImage2D',None,c.c_uint,c.c_int,c.c_int,c.c_int,c.c_int,c.c_int,c.c_uint,c.c_uint,c.c_void_p)(0x0DE1,0,0x8058,1,1,0,0x1908,0x1401,pixel)
        api(gl,'glFinish',None)() # test publication, not proposed renderer scheduling
        shared=create(dc,main_context,attrs)
        assert shared; contexts.append(shared); assert current(dc,shared)
        bind(0x0DE1,texture.value)
        received=(c.c_ubyte*4)()
        api(gl,'glGetTexImage',None,c.c_uint,c.c_int,c.c_uint,c.c_uint,c.c_void_p)(0x0DE1,0,0x1908,0x1401,received)
        assert list(received)==list(pixel),'Shared texture mismatch'
        assert api(gl,'glGetError',c.c_uint)()==0
        api(gl,'glDeleteTextures',None,c.c_int,c.POINTER(c.c_uint))(1,c.byref(texture))
        values['shared_texture_verified']=True
        print(json.dumps(values,indent=2))
    finally:
        current(None,None)
        delete=api(gl,'wglDeleteContext',w.BOOL,w.HANDLE)
        for context in reversed(contexts): delete(context)
        api(user,'ReleaseDC',c.c_int,w.HWND,w.HDC)(win,dc)
        api(user,'DestroyWindow',w.BOOL,w.HWND)(win)

if __name__=='__main__': main()
