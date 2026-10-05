#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <hilog/log.h>
#include <native_window/external_window.h>

// Diagnostic only: this draws directly, without starting Godot or C#.
static EGLDisplay probe_display = EGL_NO_DISPLAY;
static EGLContext probe_context = EGL_NO_CONTEXT;
static EGLSurface probe_surface = EGL_NO_SURFACE;

static void stop_egl_probe() {
	if (probe_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(probe_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (probe_surface != EGL_NO_SURFACE) {
			eglDestroySurface(probe_display, probe_surface);
		}
		if (probe_context != EGL_NO_CONTEXT) {
			eglDestroyContext(probe_display, probe_context);
		}
		eglTerminate(probe_display);
	}
	probe_display = EGL_NO_DISPLAY;
	probe_context = EGL_NO_CONTEXT;
	probe_surface = EGL_NO_SURFACE;
}

static bool run_egl_probe(OHNativeWindow *window, int width, int height) {
	stop_egl_probe();
	probe_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	if (!eglInitialize(probe_display, &major, &minor)) {
		OH_LOG_ERROR(LOG_APP, "GLES_PROBE: eglInitialize failed 0x%{public}x", eglGetError());
		stop_egl_probe();
		return false;
	}
	OH_LOG_INFO(LOG_APP, "GLES_PROBE: EGL %{public}d.%{public}d vendor %{public}s", major, minor, eglQueryString(probe_display, EGL_VENDOR));
	EGLint attributes[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	EGLConfig config = nullptr;
	EGLint count = 0;
	if (!eglChooseConfig(probe_display, attributes, &config, 1, &count) || count == 0 || !eglBindAPI(EGL_OPENGL_ES_API)) {
		OH_LOG_ERROR(LOG_APP, "GLES_PROBE: ES3 config/API failed 0x%{public}x", eglGetError());
		stop_egl_probe();
		return false;
	}
	EGLint format = 0;
	eglGetConfigAttrib(probe_display, config, EGL_NATIVE_VISUAL_ID, &format);
	OH_NativeWindow_NativeWindowHandleOpt(window, SET_FORMAT, format);
	OH_NativeWindow_NativeWindowHandleOpt(window, SET_BUFFER_GEOMETRY, width, height);
	EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	probe_context = eglCreateContext(probe_display, config, EGL_NO_CONTEXT, context_attributes);
	probe_surface = eglCreateWindowSurface(probe_display, config, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
	if (probe_context == EGL_NO_CONTEXT || probe_surface == EGL_NO_SURFACE ||
			!eglMakeCurrent(probe_display, probe_surface, probe_surface, probe_context)) {
		OH_LOG_ERROR(LOG_APP, "GLES_PROBE: context/surface failed 0x%{public}x", eglGetError());
		stop_egl_probe();
		return false;
	}
	OH_LOG_INFO(LOG_APP, "GLES_PROBE: GL %{public}s renderer %{public}s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
	glViewport(0, 0, width, height);
	glClearColor(0.05f, 0.7f, 0.25f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	GLenum error = glGetError();
	EGLBoolean swapped = eglSwapBuffers(probe_display, probe_surface);
	OH_LOG_INFO(LOG_APP, "GLES_PROBE: green frame swap %{public}d GL error 0x%{public}x EGL error 0x%{public}x", swapped, error, eglGetError());
	return swapped && error == GL_NO_ERROR;
}
