
/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SK_NO_COMMAND_BUFFER

#include "SkMutex.h"
#include "SkOnce.h"
#include "gl/GrGLInterface.h"
#include "gl/GrGLAssembleInterface.h"
#include "gl/command_buffer/GLTestContext_command_buffer.h"
#include "../ports/SkOSLibrary.h"

typedef void *EGLDisplay;
typedef unsigned int EGLBoolean;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef int32_t EGLint;
typedef void* EGLNativeDisplayType;
typedef void* EGLNativeWindowType;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3025
#define EGL_SAMPLES 0x3031
#define EGL_SAMPLE_BUFFERS 0x3032
#define EGL_NONE 0x3038
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A

typedef EGLDisplay (*GetDisplayProc)(EGLNativeDisplayType display_id);
typedef EGLBoolean (*InitializeProc)(EGLDisplay dpy, EGLint *major, EGLint *minor);
typedef EGLBoolean (*TerminateProc)(EGLDisplay dpy);
typedef EGLBoolean (*ChooseConfigProc)(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config);
typedef EGLBoolean (*GetConfigAttrib)(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value);
typedef EGLSurface (*CreateWindowSurfaceProc)(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list);
typedef EGLSurface (*CreatePbufferSurfaceProc)(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list);
typedef EGLBoolean (*DestroySurfaceProc)(EGLDisplay dpy, EGLSurface surface);
typedef EGLContext (*CreateContextProc)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list);
typedef EGLBoolean (*DestroyContextProc)(EGLDisplay dpy, EGLContext ctx);
typedef EGLBoolean (*MakeCurrentProc)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
typedef EGLBoolean (*SwapBuffersProc)(EGLDisplay dpy, EGLSurface surface);
typedef __eglMustCastToProperFunctionPointerType (*GetProcAddressProc)(const char* procname);

static GetDisplayProc gfGetDisplay = nullptr;
static InitializeProc gfInitialize = nullptr;
static TerminateProc gfTerminate = nullptr;
static ChooseConfigProc gfChooseConfig = nullptr;
static GetConfigAttrib gfGetConfigAttrib = nullptr;
static CreateWindowSurfaceProc gfCreateWindowSurface = nullptr;
static CreatePbufferSurfaceProc gfCreatePbufferSurface = nullptr;
static DestroySurfaceProc gfDestroySurface = nullptr;
static CreateContextProc gfCreateContext = nullptr;
static DestroyContextProc gfDestroyContext = nullptr;
static MakeCurrentProc gfMakeCurrent = nullptr;
static SwapBuffersProc gfSwapBuffers = nullptr;
static GetProcAddressProc gfGetProcAddress = nullptr;

static void* gLibrary = nullptr;
static bool gfFunctionsLoadedSuccessfully = false;

// The command buffer does not correctly implement eglGetCurrent. It always returns EGL_NO_<foo>.
// So we implement them ourselves and hook eglMakeCurrent to store the current values in TLS.
thread_local EGLDisplay gCurrDisplay = EGL_NO_DISPLAY;
thread_local EGLSurface gCurrReadSurface = EGL_NO_SURFACE;
thread_local EGLSurface gCurrDrawSurface = EGL_NO_SURFACE;
thread_local EGLContext gCurrContext = EGL_NO_CONTEXT;

EGLDisplay fakeGetCurrentDisplay() { return gCurrDisplay; }
EGLSurface fakeGetCurrentSurface(EGLint readdraw) {
    switch (readdraw) {
        case EGL_DRAW:
            return gCurrDrawSurface;
        case EGL_READ:
            return gCurrReadSurface;
        default:
            return EGL_NO_SURFACE;
    }
}
EGLContext fakeGetCurrentContext() { return gCurrContext; }

EGLBoolean hookedMakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    if (gfFunctionsLoadedSuccessfully && EGL_TRUE == gfMakeCurrent(display, draw, read, ctx)) {
        gCurrDisplay = display;
        gCurrDrawSurface = draw;
        gCurrReadSurface = read;
        gCurrContext = ctx;
        return EGL_TRUE;
    }
    return EGL_FALSE;
}

namespace {
static void load_command_buffer_functions() {
    if (!gLibrary) {
        static constexpr const char* libName =
#if defined _WIN32
        "command_buffer_gles2.dll";
#elif defined SK_BUILD_FOR_MAC
        "libcommand_buffer_gles2.dylib";
#else
        "libcommand_buffer_gles2.so";
#endif // defined _WIN32
        gLibrary = DynamicLoadLibrary(libName);
        if (gLibrary) {
            gfGetDisplay = (GetDisplayProc)GetProcedureAddress(gLibrary, "eglGetDisplay");
            gfInitialize = (InitializeProc)GetProcedureAddress(gLibrary, "eglInitialize");
            gfTerminate = (TerminateProc)GetProcedureAddress(gLibrary, "eglTerminate");
            gfChooseConfig = (ChooseConfigProc)GetProcedureAddress(gLibrary, "eglChooseConfig");
            gfGetConfigAttrib = (GetConfigAttrib)GetProcedureAddress(gLibrary, "eglGetConfigAttrib");
            gfCreateWindowSurface = (CreateWindowSurfaceProc)GetProcedureAddress(gLibrary, "eglCreateWindowSurface");
            gfCreatePbufferSurface = (CreatePbufferSurfaceProc)GetProcedureAddress(gLibrary, "eglCreatePbufferSurface");
            gfDestroySurface = (DestroySurfaceProc)GetProcedureAddress(gLibrary, "eglDestroySurface");
            gfCreateContext = (CreateContextProc)GetProcedureAddress(gLibrary, "eglCreateContext");
            gfDestroyContext = (DestroyContextProc)GetProcedureAddress(gLibrary, "eglDestroyContext");
            gfMakeCurrent = (MakeCurrentProc)GetProcedureAddress(gLibrary, "eglMakeCurrent");
            gfSwapBuffers = (SwapBuffersProc)GetProcedureAddress(gLibrary, "eglSwapBuffers");
            gfGetProcAddress = (GetProcAddressProc)GetProcedureAddress(gLibrary, "eglGetProcAddress");

            gfFunctionsLoadedSuccessfully =
                    gfGetDisplay && gfInitialize && gfTerminate && gfChooseConfig &&
                    gfCreateWindowSurface && gfCreatePbufferSurface && gfDestroySurface &&
                    gfCreateContext && gfDestroyContext && gfMakeCurrent && gfSwapBuffers &&
                    gfGetProcAddress;
        }
    }
}

static GrGLFuncPtr command_buffer_get_gl_proc(void* ctx, const char name[]) {
    if (!gfFunctionsLoadedSuccessfully) {
        return nullptr;
    }
    return gfGetProcAddress(name);
}

static void load_command_buffer_once() {
    static SkOnce once;
    once(load_command_buffer_functions);
}

static const GrGLInterface* create_command_buffer_interface() {
    load_command_buffer_once();
    if (!gfFunctionsLoadedSuccessfully) {
        return nullptr;
    }
    return GrGLAssembleGLESInterface(gLibrary, command_buffer_get_gl_proc);
}

std::function<void()> context_restorer() {
    if (!gfFunctionsLoadedSuccessfully) {
        return nullptr;
    }
    auto display = fakeGetCurrentDisplay();
    auto dsurface = fakeGetCurrentSurface(EGL_DRAW);
    auto rsurface = fakeGetCurrentSurface(EGL_READ);
    auto context = fakeGetCurrentContext();
    return [display, dsurface, rsurface, context] {
        hookedMakeCurrent(display, dsurface, rsurface, context);
    };
}

}  // anonymous namespace

namespace sk_gpu_test {

CommandBufferGLTestContext::CommandBufferGLTestContext(CommandBufferGLTestContext* shareContext)
    : fContext(EGL_NO_CONTEXT), fDisplay(EGL_NO_DISPLAY), fSurface(EGL_NO_SURFACE) {

    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    static const EGLint surfaceAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };

    load_command_buffer_once();
    if (!gfFunctionsLoadedSuccessfully) {
        return;
    }

    fDisplay = gfGetDisplay(EGL_DEFAULT_DISPLAY);
    if (EGL_NO_DISPLAY == fDisplay) {
        SkDebugf("Command Buffer: Could not create EGL display.\n");
        return;
    }
    if (!gfInitialize(fDisplay, nullptr, nullptr)) {
        SkDebugf("Command Buffer: Could not initialize EGL display.\n");
        this->destroyGLContext();
        return;
    }
    EGLint numConfigs;
    if (!gfChooseConfig(fDisplay, configAttribs, static_cast<EGLConfig *>(&fConfig), 1,
                        &numConfigs) || numConfigs != 1) {
        SkDebugf("Command Buffer: Could not choose EGL config.\n");
        this->destroyGLContext();
        return;
    }

    fSurface = gfCreatePbufferSurface(fDisplay,
                                        static_cast<EGLConfig>(fConfig),
                                        surfaceAttribs);

    if (EGL_NO_SURFACE == fSurface) {
        SkDebugf("Command Buffer: Could not create EGL surface.\n");
        this->destroyGLContext();
        return;
    }

    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext eglShareContext = shareContext
            ? reinterpret_cast<EGLContext>(shareContext->fContext) : nullptr;
    fContext = gfCreateContext(fDisplay, static_cast<EGLConfig>(fConfig), eglShareContext,
                               contextAttribs);
    if (EGL_NO_CONTEXT == fContext) {
        SkDebugf("Command Buffer: Could not create EGL context.\n");
        this->destroyGLContext();
        return;
    }

    SkScopeExit restorer(context_restorer());
    if (!hookedMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
        SkDebugf("Command Buffer: Could not make EGL context current.\n");
        this->destroyGLContext();
        return;
    }

    sk_sp<const GrGLInterface> gl(create_command_buffer_interface());
    if (nullptr == gl.get()) {
        SkDebugf("Command Buffer: Could not create CommandBuffer GL interface.\n");
        this->destroyGLContext();
        return;
    }
    if (!gl->validate()) {
        SkDebugf("Command Buffer: Could not validate CommandBuffer GL interface.\n");
        this->destroyGLContext();
        return;
    }

    this->init(gl.release());
}

CommandBufferGLTestContext::~CommandBufferGLTestContext() {
    this->teardown();
    this->destroyGLContext();
}

void CommandBufferGLTestContext::destroyGLContext() {
    if (!gfFunctionsLoadedSuccessfully) {
        return;
    }
    if (EGL_NO_DISPLAY == fDisplay) {
        return;
    }
    bool wasCurrent = false;
    if (EGL_NO_CONTEXT != fContext) {
        wasCurrent = fakeGetCurrentContext() == fContext;
        gfDestroyContext(fDisplay, fContext);
        fContext = EGL_NO_CONTEXT;
    }
    if (wasCurrent) {
        // Call MakeCurrent after destroying the context, so that the EGL implementation knows that
        // the context is not used anymore after it is released from being current.This way the
        // command buffer does not need to abandon the context before destruction, and no
        // client-side errors are printed.
        hookedMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (EGL_NO_SURFACE != fSurface) {
        gfDestroySurface(fDisplay, fSurface);
        fSurface = EGL_NO_SURFACE;
    }
    fDisplay = EGL_NO_DISPLAY;
}

void CommandBufferGLTestContext::onPlatformMakeCurrent() const {
    if (!gfFunctionsLoadedSuccessfully) {
        return;
    }
    if (!hookedMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
        SkDebugf("Command Buffer: Could not make EGL context current.\n");
    }
}

std::function<void()> CommandBufferGLTestContext::onPlatformGetAutoContextRestore() const {
    if (!gfFunctionsLoadedSuccessfully || fakeGetCurrentContext() == fContext) {
        return nullptr;
    }
    return context_restorer();
}

void CommandBufferGLTestContext::onPlatformSwapBuffers() const {
    if (!gfFunctionsLoadedSuccessfully) {
        return;
    }
    if (!gfSwapBuffers(fDisplay, fSurface)) {
        SkDebugf("Command Buffer: Could not complete gfSwapBuffers.\n");
    }
}

GrGLFuncPtr CommandBufferGLTestContext::onPlatformGetProcAddress(const char *name) const {
    if (!gfFunctionsLoadedSuccessfully) {
        return nullptr;
    }
    return gfGetProcAddress(name);
}

void CommandBufferGLTestContext::presentCommandBuffer() {
    if (this->gl()) {
        this->gl()->fFunctions.fFlush();
    }

    this->onPlatformSwapBuffers();
}

bool CommandBufferGLTestContext::makeCurrent() {
    return hookedMakeCurrent(fDisplay, fSurface, fSurface, fContext) != EGL_FALSE;
}

int CommandBufferGLTestContext::getStencilBits() {
    EGLint result = 0;
    gfGetConfigAttrib(fDisplay, static_cast<EGLConfig>(fConfig), EGL_STENCIL_SIZE, &result);
    return result;
}

int CommandBufferGLTestContext::getSampleCount() {
    EGLint result = 0;
    gfGetConfigAttrib(fDisplay, static_cast<EGLConfig>(fConfig), EGL_SAMPLES, &result);
    return result;
}

}  // namespace sk_gpu_test
#endif // SK_NO_COMMAND_BUFFER
