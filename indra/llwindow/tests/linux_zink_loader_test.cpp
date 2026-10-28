// Exercise the same provider loader as the viewer against a real GLX context.
#include "lllinuxzink.h"
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <cstring>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    std::string error;
    if (LLLinuxZink::activate("/nonexistent-mesa-test", error) || error.empty()) return 3;
    const char* original = std::getenv("__GLX_VENDOR_LIBRARY_NAME");
    const bool had_original = original != nullptr;
    const std::string saved = original ? original : "";
    if (!LLLinuxZink::activate(argv[1], error))
    {
        std::cerr << error << '\n';
        return 4;
    }
    LLLinuxZink::Environment environment;
    environment.apply();
    Display* display = XOpenDisplay(nullptr);
    if (!display) return 5;
    int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, None};
    XVisualInfo* visual = glXChooseVisual(display, DefaultScreen(display), attributes);
    if (!visual) return 6;
    XSetWindowAttributes window_attributes{};
    window_attributes.colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);
    Window window = XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, 32, 32,
                                  0, visual->depth, InputOutput, visual->visual,
                                  CWColormap, &window_attributes);
    GLXContext context = glXCreateContext(display, visual, nullptr, True);
    if (!context || !glXMakeCurrent(display, window, context)) return 7;
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (!renderer || !std::strstr(renderer, "zink")) return 8;
    std::cout << "Viewer loader renderer: " << renderer << '\n';
    environment.restore();
    original = std::getenv("__GLX_VENDOR_LIBRARY_NAME");
    if ((original != nullptr) != had_original || (original && saved != original)) return 9;
    // Shared contexts must keep the selected provider after restoring child-process environment.
    GLXContext shared = glXCreateContext(display, visual, context, True);
    if (!shared || !glXMakeCurrent(display, window, shared)) return 10;
    renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (!renderer || !std::strstr(renderer, "zink")) return 11;
    glXMakeCurrent(display, None, nullptr);
    glXDestroyContext(display, shared);
    glXDestroyContext(display, context);
    XDestroyWindow(display, window);
    XFreeColormap(display, window_attributes.colormap);
    XFree(visual);
    XCloseDisplay(display);
    std::cout << "PASS: private provider loading, missing-library failure, environment restoration, shared context\n";
}
