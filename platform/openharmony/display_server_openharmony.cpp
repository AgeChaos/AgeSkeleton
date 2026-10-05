/**************************************************************************/
/*  display_server_openharmony.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "display_server_openharmony.h"

#include "os_openharmony.h"
#include "rendering_context_driver_vulkan_openharmony.h"
#include "wrapper_openharmony.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/variant/variant_callable.h"
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/rendering_device.h"
#ifdef GLES3_ENABLED
#include "drivers/gles3/rasterizer_gles3.h"
#endif

#include <database/pasteboard/oh_pasteboard.h>
#include <database/udmf/udmf.h>
#include <database/udmf/uds.h>
#include <native_window/external_window.h>

void DisplayServerOpenHarmony::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	get_singleton()->send_input_event(p_event);
}

DisplayServerOpenHarmony *DisplayServerOpenHarmony::get_singleton() {
	return static_cast<DisplayServerOpenHarmony *>(DisplayServer::get_singleton());
}

Vector<String> DisplayServerOpenHarmony::get_rendering_drivers_func() {
	Vector<String> drivers;
	drivers.push_back("vulkan");
#ifdef GLES3_ENABLED
	drivers.push_back("opengl3");
#endif
	return drivers;
}

DisplayServer *DisplayServerOpenHarmony::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerOpenHarmony(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, p_parent_window, r_error));
	if (r_error != OK) {
		OS::get_singleton()->alert(
				vformat("Unable to initialize the %s video driver.", p_rendering_driver) +
				(p_rendering_driver == "vulkan" ? " Please try exporting your game using the Compatibility (gl_compatibility) renderer. No automatic GLES retry was performed." : ""));
	}
	return ds;
}

void DisplayServerOpenHarmony::register_openharmony_driver() {
	register_create_function("openharmony", create_func, get_rendering_drivers_func);
}

DisplayServerOpenHarmony::DisplayServerOpenHarmony(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	rendering_driver = p_rendering_driver;

	rendering_context = nullptr;
	rendering_device = nullptr;
	vsync_mode = p_vsync_mode;

#ifdef GLES3_ENABLED
	if (rendering_driver == "opengl3") {
		r_error = initialize_egl();
		if (r_error != OK) {
			ERR_PRINT(vformat("OpenHarmony EGL initialization failed: 0x%x", eglGetError()));
			return;
		}
		RasterizerGLES3::make_current(false);
		Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);
		return;
	}
#endif

	if (rendering_driver != "vulkan") {
		ERR_PRINT(vformat("Failed to create %s context.", rendering_driver));
		r_error = ERR_UNAVAILABLE;
		return;
	}

	rendering_context = memnew(RenderingContextDriverVulkanOpenHarmony);

	if (rendering_context->initialize() != OK) {
		memdelete(rendering_context);
		rendering_context = nullptr;
		ERR_PRINT(vformat("Failed to initialize %s context.", rendering_driver));
		r_error = ERR_UNAVAILABLE;
		return;
	}
	RenderingContextDriverVulkanOpenHarmony::WindowPlatformData vulkan;
	OHNativeWindow *native_window = OS_OpenHarmony::get_singleton()->get_native_window();
	if (!native_window) {
		r_error = ERR_UNAVAILABLE;
		return;
	}
	vulkan.window = native_window;

	if (rendering_context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &vulkan) != OK) {
		ERR_PRINT(vformat("Failed to create %s window.", rendering_driver));
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_UNAVAILABLE;
		return;
	}

	Size2i display_size = OS_OpenHarmony::get_singleton()->get_display_size();
	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, display_size.width, display_size.height);
	rendering_context->window_set_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID, p_vsync_mode);

	rendering_device = memnew(RenderingDevice);
	if (rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID) != OK) {
		memdelete(rendering_device);
		rendering_device = nullptr;
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_UNAVAILABLE;
		return;
	}
	// Check the selected device before the compositor allocates its default textures.
	const struct {
		RenderingDevice::DataFormat format;
		const char *name;
	} required_sampled_formats[] = {
		{ RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM" },
		{ RenderingDevice::DATA_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT" },
		{ RenderingDevice::DATA_FORMAT_D16_UNORM, "D16_UNORM" },
	};
	bool required_formats_supported = true;
	for (const auto &required : required_sampled_formats) {
		if (!rendering_device->texture_is_format_supported_for_usage(required.format, RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT)) {
			ERR_PRINT(vformat("OpenHarmony Vulkan preflight: required sampled texture format %s is unsupported by the selected device.", required.name));
			required_formats_supported = false;
		}
	}
	if (!required_formats_supported) {
		ERR_PRINT("OpenHarmony Vulkan initialization stopped before renderer resource creation. The Vulkan driver does not expose required texture capabilities. Use Compatibility rendering or test another driver/device; this is not a successful Vulkan run.");
		r_error = ERR_UNAVAILABLE;
		return;
	}
	rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);

	RendererCompositorRD::make_current();

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	r_error = OK;
}

DisplayServerOpenHarmony::~DisplayServerOpenHarmony() {
#ifdef GLES3_ENABLED
	if (egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (egl_surface != EGL_NO_SURFACE) {
			eglDestroySurface(egl_display, egl_surface);
		}
		if (egl_context != EGL_NO_CONTEXT) {
			eglDestroyContext(egl_display, egl_context);
		}
		eglTerminate(egl_display);
	}
#endif
	if (rendering_device) {
		rendering_device->finalize();
		memdelete(rendering_device);
	}
	if (rendering_context) {
		memdelete(rendering_context);
	}
}

void DisplayServerOpenHarmony::_window_callback(const Callable &p_callable, const Variant &p_arg, bool p_deferred) const {
	if (p_callable.is_valid()) {
		if (p_deferred) {
			p_callable.call_deferred(p_arg);
		} else {
			p_callable.call(p_arg);
		}
	}
}

void DisplayServerOpenHarmony::send_input_event(const Ref<InputEvent> &p_event) const {
	_window_callback(input_event_callback, p_event);
}

void DisplayServerOpenHarmony::resize_window(uint32_t p_width, uint32_t p_height) {
	Size2i size = Size2i(p_width, p_height);
	OS_OpenHarmony::get_singleton()->set_display_size(size);
#ifdef GLES3_ENABLED
	if (egl_surface != EGL_NO_SURFACE) {
		OH_NativeWindow_NativeWindowHandleOpt(OS_OpenHarmony::get_singleton()->get_native_window(), SET_BUFFER_GEOMETRY, int(p_width), int(p_height));
	}
#endif

#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, size.x, size.y);
	}
#endif

	Variant resize_rect = Rect2i(Point2i(), size);
	_window_callback(window_resize_callback, resize_rect);
}

void DisplayServerOpenHarmony::send_window_event(DisplayServerEnums::WindowEvent p_event) const {
	_window_callback(window_event_callback, int(p_event));
}

bool DisplayServerOpenHarmony::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_SWAP_BUFFERS:
			return rendering_driver == "opengl3";
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_CLIPBOARD:
		case DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD:
		case DisplayServerEnums::FEATURE_IME:
		case DisplayServerEnums::FEATURE_KEEP_SCREEN_ON:
			return true;
		default:
			return false;
	}
}

String DisplayServerOpenHarmony::get_name() const {
	return "OpenHarmony";
}

int DisplayServerOpenHarmony::get_screen_count() const {
	return 1;
}

int DisplayServerOpenHarmony::get_primary_screen() const {
	return 0;
}

Point2i DisplayServerOpenHarmony::screen_get_position(int p_screen) const {
	return Point2i(0, 0);
}

Size2i DisplayServerOpenHarmony::screen_get_size(int p_screen) const {
	return OS_OpenHarmony::get_singleton()->get_display_size();
}

Rect2i DisplayServerOpenHarmony::screen_get_usable_rect(int p_screen) const {
	Size2i display_size = OS_OpenHarmony::get_singleton()->get_display_size();
	return Rect2i(0, 0, display_size.width, display_size.height);
}

int DisplayServerOpenHarmony::screen_get_dpi(int p_screen) const {
	return ohos_wrapper_get_display_dpi();
}

float DisplayServerOpenHarmony::screen_get_scale(int p_screen) const {
	return ohos_wrapper_get_display_scaled_density();
}

float DisplayServerOpenHarmony::screen_get_refresh_rate(int p_screen) const {
	return ohos_wrapper_get_display_refresh_rate();
}

bool DisplayServerOpenHarmony::is_touchscreen_available() const {
	return true;
}

void DisplayServerOpenHarmony::screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen) {
	// Not supported on OpenHarmony.
}

DisplayServerEnums::ScreenOrientation DisplayServerOpenHarmony::screen_get_orientation(int p_screen) const {
	switch (ohos_wrapper_get_display_orientation()) {
		case WrapperScreenOrientation::WRAPPER_SCREEN_LANDSCAPE:
			return DisplayServerEnums::SCREEN_LANDSCAPE;
		case WrapperScreenOrientation::WRAPPER_SCREEN_PORTRAIT:
			return DisplayServerEnums::SCREEN_PORTRAIT;
		case WrapperScreenOrientation::WRAPPER_SCREEN_REVERSE_LANDSCAPE:
			return DisplayServerEnums::SCREEN_REVERSE_LANDSCAPE;
		case WrapperScreenOrientation::WRAPPER_SCREEN_REVERSE_PORTRAIT:
			return DisplayServerEnums::SCREEN_REVERSE_PORTRAIT;
		default:
			return DisplayServerEnums::SCREEN_PORTRAIT;
	}
}

void DisplayServerOpenHarmony::clipboard_set(const String &p_text) {
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	OH_UdsPlainText *plainText = OH_UdsPlainText_Create();
	OH_UdsPlainText_SetContent(plainText, p_text.utf8().get_data());
	OH_UdmfRecord *record = OH_UdmfRecord_Create();
	OH_UdmfRecord_AddPlainText(record, plainText);
	OH_UdmfData *data = OH_UdmfData_Create();
	OH_UdmfData_AddRecord(data, record);
	int status = OH_Pasteboard_SetData(pasteboard, data);
	if (status != 0) {
		ERR_PRINT("Failed to set clipboard data with PASTEBOARD_ErrCode: " + itos(status));
	}
	OH_UdsPlainText_Destroy(plainText);
	OH_UdmfRecord_Destroy(record);
	OH_UdmfData_Destroy(data);
	OH_Pasteboard_Destroy(pasteboard);
}

String DisplayServerOpenHarmony::clipboard_get() const {
	String content;
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	bool hasPlainTextData = OH_Pasteboard_HasType(pasteboard, "text/plain");
	if (hasPlainTextData) {
		int status = 0;
		OH_UdmfData *udmfData = OH_Pasteboard_GetData(pasteboard, &status);
		if (status == 0) {
			OH_UdmfRecord *record = OH_UdmfData_GetRecord(udmfData, 0);
			OH_UdsPlainText *plainText = OH_UdsPlainText_Create();
			OH_UdmfRecord_GetPlainText(record, plainText);
			content = String::utf8(OH_UdsPlainText_GetContent(plainText));
			OH_UdsPlainText_Destroy(plainText);
		} else {
			ERR_PRINT("Failed to get clipboard data with PASTEBOARD_ErrCode: " + itos(status));
		}
		OH_UdmfData_Destroy(udmfData);
	}
	OH_Pasteboard_Destroy(pasteboard);
	return content;
}

void DisplayServerOpenHarmony::screen_set_keep_on(bool p_enable) {
	ohos_wrapper_screen_set_keep_on(OS_OpenHarmony::get_singleton()->get_window_id(), p_enable);
}

bool DisplayServerOpenHarmony::screen_is_kept_on() const {
	return ohos_wrapper_screen_is_kept_on(OS_OpenHarmony::get_singleton()->get_window_id());
}

void DisplayServerOpenHarmony::_get_text_config(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_TextConfig *p_text_config) {
	InputMethod_TextInputType input_type = IME_TEXT_INPUT_TYPE_TEXT;
	InputMethod_EnterKeyType enter_key_type = IME_ENTER_KEY_DONE;
	switch (get_singleton()->keyboard_type) {
		case DisplayServerEnums::KEYBOARD_TYPE_DEFAULT:
			input_type = IME_TEXT_INPUT_TYPE_TEXT;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_MULTILINE:
			input_type = IME_TEXT_INPUT_TYPE_MULTILINE;
			enter_key_type = IME_ENTER_KEY_NEWLINE;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER:
			input_type = IME_TEXT_INPUT_TYPE_NUMBER;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER_DECIMAL:
			input_type = IME_TEXT_INPUT_TYPE_NUMBER_DECIMAL;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_PHONE:
			input_type = IME_TEXT_INPUT_TYPE_PHONE;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_EMAIL_ADDRESS:
			input_type = IME_TEXT_INPUT_TYPE_EMAIL_ADDRESS;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_PASSWORD:
			input_type = IME_TEXT_INPUT_TYPE_VISIBLE_PASSWORD;
			break;
		case DisplayServerEnums::KEYBOARD_TYPE_URL:
			input_type = IME_TEXT_INPUT_TYPE_URL;
			break;
		default:
			break;
	}
	OH_TextConfig_SetInputType(p_text_config, input_type);
	OH_TextConfig_SetPreviewTextSupport(p_text_config, false);
	OH_TextConfig_SetEnterKeyType(p_text_config, enter_key_type);
}

void DisplayServerOpenHarmony::_insert_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t length) {
	String characters = String::utf16(p_text, length);

	for (int i = 0; i < characters.size(); i++) {
		int character = characters[i];
		Key key = Key::NONE;

		if (character == '\t') { // 0x09
			key = Key::TAB;
		} else if (character == '\n') { // 0x0A
			key = Key::ENTER;
		} else if (character == 0x2006) {
			key = Key::SPACE;
		}

		_input_text_key(key, character, key, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
		_input_text_key(key, character, key, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
	}
}

void DisplayServerOpenHarmony::_delete_forward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t length) {
	for (int i = 0; i < length; i++) {
		_input_text_key(Key::KEY_DELETE, 0, Key::KEY_DELETE, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
		_input_text_key(Key::KEY_DELETE, 0, Key::KEY_DELETE, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
	}
}

void DisplayServerOpenHarmony::_delete_backward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t length) {
	for (int i = 0; i < length; i++) {
		_input_text_key(Key::BACKSPACE, 0, Key::BACKSPACE, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
		_input_text_key(Key::BACKSPACE, 0, Key::BACKSPACE, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
	}
}

void DisplayServerOpenHarmony::_send_keyboard_status(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_KeyboardStatus status) {
	get_singleton()->keyboard_status = status;
}

void DisplayServerOpenHarmony::_send_enter_key(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_EnterKeyType enter_key_type) {
	_input_text_key(Key::ENTER, 0, Key::ENTER, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
	_input_text_key(Key::ENTER, 0, Key::ENTER, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
}

void DisplayServerOpenHarmony::_move_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_Direction direction) {
	switch (direction) {
		case IME_DIRECTION_LEFT:
			_input_text_key(Key::LEFT, 0, Key::LEFT, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::LEFT, 0, Key::LEFT, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		case IME_DIRECTION_RIGHT:
			_input_text_key(Key::RIGHT, 0, Key::RIGHT, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::RIGHT, 0, Key::RIGHT, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		case IME_DIRECTION_UP:
			_input_text_key(Key::UP, 0, Key::UP, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::UP, 0, Key::UP, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		case IME_DIRECTION_DOWN:
			_input_text_key(Key::DOWN, 0, Key::DOWN, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::DOWN, 0, Key::DOWN, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		default:
			break;
	}
}

void DisplayServerOpenHarmony::_handle_set_selection(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t start, int32_t end) {
	// Not supported by Godot.
}

void DisplayServerOpenHarmony::_handle_extend_action(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_ExtendAction action) {
	// Not supported by Godot.
}

void DisplayServerOpenHarmony::_get_left_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t number, char16_t *p_text, size_t *p_length) {
	// Not supported by Godot.
}

void DisplayServerOpenHarmony::_get_right_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t number, char16_t *p_text, size_t *p_length) {
	// Not supported by Godot.
}

int32_t DisplayServerOpenHarmony::_get_text_index_at_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	// Not supported by Godot.
	return 0;
}

int32_t DisplayServerOpenHarmony::_receive_private_command(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_PrivateCommand *p_command[], size_t length) {
	// Not supported by Godot.
	return 0;
}

int32_t DisplayServerOpenHarmony::_set_preview_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t length, int32_t start, int32_t end) {
	// Not supported by Godot.
	return 0;
}

void DisplayServerOpenHarmony::_finish_text_preview(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	// Not supported by Godot.
}

void DisplayServerOpenHarmony::_input_text_key(Key p_key, char32_t p_char, Key p_unshifted, Key p_physical, int p_modifier, bool p_pressed, KeyLocation p_location) {
	Ref<InputEventKey> ev;
	ev.instantiate();
	ev->set_echo(false);
	ev->set_pressed(p_pressed);
	ev->set_keycode(fix_keycode(p_char, p_key));
	ev->set_key_label(p_unshifted);
	ev->set_physical_keycode(p_physical);
	ev->set_unicode(fix_unicode(p_char));
	ev->set_location(p_location);
	Input::get_singleton()->parse_input_event(ev);
}

void DisplayServerOpenHarmony::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	if (keyboard_status == IME_KEYBOARD_STATUS_SHOW && keyboard_type == p_type) {
		return;
	}
	if (keyboard_status != IME_KEYBOARD_STATUS_NONE) {
		virtual_keyboard_hide();
	}

	keyboard_type = p_type;
	text_editor_proxy = OH_TextEditorProxy_Create();
	attach_options = OH_AttachOptions_Create(true);

	OH_TextEditorProxy_SetGetTextConfigFunc(text_editor_proxy, _get_text_config);
	OH_TextEditorProxy_SetInsertTextFunc(text_editor_proxy, _insert_text);
	OH_TextEditorProxy_SetDeleteForwardFunc(text_editor_proxy, _delete_forward);
	OH_TextEditorProxy_SetDeleteBackwardFunc(text_editor_proxy, _delete_backward);
	OH_TextEditorProxy_SetSendKeyboardStatusFunc(text_editor_proxy, _send_keyboard_status);
	OH_TextEditorProxy_SetSendEnterKeyFunc(text_editor_proxy, _send_enter_key);
	OH_TextEditorProxy_SetMoveCursorFunc(text_editor_proxy, _move_cursor);
	OH_TextEditorProxy_SetHandleSetSelectionFunc(text_editor_proxy, _handle_set_selection);
	OH_TextEditorProxy_SetHandleExtendActionFunc(text_editor_proxy, _handle_extend_action);
	OH_TextEditorProxy_SetGetLeftTextOfCursorFunc(text_editor_proxy, _get_left_text_of_cursor);
	OH_TextEditorProxy_SetGetRightTextOfCursorFunc(text_editor_proxy, _get_right_text_of_cursor);
	OH_TextEditorProxy_SetGetTextIndexAtCursorFunc(text_editor_proxy, _get_text_index_at_cursor);
	OH_TextEditorProxy_SetReceivePrivateCommandFunc(text_editor_proxy, _receive_private_command);
	OH_TextEditorProxy_SetSetPreviewTextFunc(text_editor_proxy, _set_preview_text);
	OH_TextEditorProxy_SetFinishTextPreviewFunc(text_editor_proxy, _finish_text_preview);

	InputMethod_ErrorCode code = OH_InputMethodController_Attach(text_editor_proxy, attach_options, &input_method_proxy);
	ERR_FAIL_COND_MSG(code != IME_ERR_OK, vformat("Failed to attach input method controller: %d.", code));
}

void DisplayServerOpenHarmony::virtual_keyboard_hide() {
	if (keyboard_status == IME_KEYBOARD_STATUS_SHOW) {
		if (OH_InputMethodProxy_HideKeyboard(input_method_proxy) != IME_ERR_OK) {
			ERR_PRINT("Failed to hide keyboard.");
		}
	}
	if (input_method_proxy) {
		if (OH_InputMethodController_Detach(input_method_proxy) != IME_ERR_OK) {
			ERR_PRINT("Failed to detach input method controller.");
		}
		input_method_proxy = nullptr;
	}
	if (attach_options) {
		OH_AttachOptions_Destroy(attach_options);
		attach_options = nullptr;
	}
	if (text_editor_proxy) {
		OH_TextEditorProxy_Destroy(text_editor_proxy);
		text_editor_proxy = nullptr;
	}
	keyboard_status = IME_KEYBOARD_STATUS_NONE;
}

int DisplayServerOpenHarmony::virtual_keyboard_get_height() const {
	if (keyboard_status == IME_KEYBOARD_STATUS_SHOW) {
		int height = ohos_wrapper_get_keyboard_avoid_area(OS_OpenHarmony::get_singleton()->get_window_id());
		return height;
	}
	return 0;
}

void DisplayServerOpenHarmony::window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window) {
	ime_active = p_active;
}

void DisplayServerOpenHarmony::window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window) {
	if (ime_active) {
		InputMethod_CursorInfo *info = OH_CursorInfo_Create(p_pos.x, p_pos.y, 0, 30);
		OH_InputMethodProxy_NotifyCursorUpdate(input_method_proxy, info);
	}
}

Vector<DisplayServerEnums::WindowID> DisplayServerOpenHarmony::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> ret;
	ret.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return ret;
}

DisplayServerEnums::WindowID DisplayServerOpenHarmony::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

void DisplayServerOpenHarmony::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id = p_instance;
}

ObjectID DisplayServerOpenHarmony::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id;
}

void DisplayServerOpenHarmony::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_event_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_event_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_text_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_resize_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

int DisplayServerOpenHarmony::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::SCREEN_OF_MAIN_WINDOW;
}

void DisplayServerOpenHarmony::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Point2i DisplayServerOpenHarmony::window_get_position(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerOpenHarmony::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

void DisplayServerOpenHarmony::window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Size2i DisplayServerOpenHarmony::window_get_max_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerOpenHarmony::window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Size2i DisplayServerOpenHarmony::window_get_min_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerOpenHarmony::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Size2i DisplayServerOpenHarmony::window_get_size(DisplayServerEnums::WindowID p_window) const {
	return OS_OpenHarmony::get_singleton()->get_display_size();
}

Size2i DisplayServerOpenHarmony::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return OS_OpenHarmony::get_singleton()->get_display_size();
}

void DisplayServerOpenHarmony::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

DisplayServerEnums::WindowMode DisplayServerOpenHarmony::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WINDOW_MODE_FULLSCREEN;
}

void DisplayServerOpenHarmony::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	vsync_mode = p_vsync_mode;
#ifdef GLES3_ENABLED
	if (egl_surface != EGL_NO_SURFACE) {
		vsync_mode = p_vsync_mode == DisplayServerEnums::VSYNC_DISABLED ? DisplayServerEnums::VSYNC_DISABLED : DisplayServerEnums::VSYNC_ENABLED;
		ERR_FAIL_COND(!eglSwapInterval(egl_display, vsync_mode == DisplayServerEnums::VSYNC_DISABLED ? 0 : 1));
	}
#endif
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
}

DisplayServerEnums::VSyncMode DisplayServerOpenHarmony::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
	return vsync_mode;
}

#ifdef GLES3_ENABLED
Error DisplayServerOpenHarmony::initialize_egl() {
	OHNativeWindow *window = OS_OpenHarmony::get_singleton()->get_native_window();
	ERR_FAIL_NULL_V(window, ERR_CANT_CREATE);
	ERR_FAIL_COND_V_MSG(!gladLoaderLoadEGL(EGL_NO_DISPLAY), ERR_CANT_CREATE, "Failed to load OpenHarmony EGL entry points.");
	egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	ERR_FAIL_COND_V(egl_display == EGL_NO_DISPLAY, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(!eglInitialize(egl_display, nullptr, nullptr), ERR_CANT_CREATE);
	ERR_FAIL_COND_V_MSG(!gladLoaderLoadEGL(egl_display), ERR_CANT_CREATE, "Failed to load initialized OpenHarmony EGL entry points.");
	ERR_FAIL_COND_V(!eglBindAPI(EGL_OPENGL_ES_API), ERR_CANT_CREATE);
	const EGLint attributes[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
	EGLConfig config = nullptr;
	EGLint count = 0;
	ERR_FAIL_COND_V(!eglChooseConfig(egl_display, attributes, &config, 1, &count) || count == 0, ERR_CANT_CREATE);
	EGLint format = 0;
	ERR_FAIL_COND_V(!eglGetConfigAttrib(egl_display, config, EGL_NATIVE_VISUAL_ID, &format), ERR_CANT_CREATE);
	ERR_FAIL_COND_V(OH_NativeWindow_NativeWindowHandleOpt(window, SET_FORMAT, format) != 0, ERR_CANT_CREATE);
	Size2i size = OS_OpenHarmony::get_singleton()->get_display_size();
	ERR_FAIL_COND_V(OH_NativeWindow_NativeWindowHandleOpt(window, SET_BUFFER_GEOMETRY, size.x, size.y) != 0, ERR_CANT_CREATE);
	const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attributes);
	ERR_FAIL_COND_V(egl_context == EGL_NO_CONTEXT, ERR_CANT_CREATE);
	egl_surface = eglCreateWindowSurface(egl_display, config, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
	ERR_FAIL_COND_V(egl_surface == EGL_NO_SURFACE, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context), ERR_CANT_CREATE);
	window_set_vsync_mode(vsync_mode);
	return OK;
}
#endif

void DisplayServerOpenHarmony::swap_buffers() {
#ifdef GLES3_ENABLED
	if (egl_surface != EGL_NO_SURFACE) {
#ifdef DEBUG_ENABLED
		if (ProjectSettings::get_singleton()->get_setting("debug/openharmony/presentation_probe", false)) {
			static uint32_t probe_frame = 0;
			probe_frame++;
			EGLint width = 0, height = 0;
			eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &width);
			eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &height);
			GLint draw_fbo = 0, read_fbo = 0, pack_buffer = 0, scissor[4] = {};
			GLfloat clear_color[4] = {};
			GLint color_mask[4] = { 1, 1, 1, 1 };
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
			glGetIntegerv(GL_SCISSOR_BOX, scissor);
			glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
			glGetIntegerv(GL_COLOR_WRITEMASK, color_mask);
			bool scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			if (probe_frame == 120 || probe_frame == 240) {
				GLenum prior_error = glGetError();
				glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
				uint8_t pixel[4] = {};
				glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
				print_line(vformat("OHOS_PRESENT frame=%d surface=%dx%d draw_fbo=%d read_fbo=%d center=%d,%d,%d,%d prior_error=%d read_error=%d", probe_frame, width, height, draw_fbo, read_fbo, pixel[0], pixel[1], pixel[2], pixel[3], prior_error, glGetError()));
				glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
			}
			glEnable(GL_SCISSOR_TEST);
			glScissor(MAX(0, width - 160), MAX(0, height - 160), 128, 128);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
			glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
			glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
			if (!scissor_enabled) {
				glDisable(GL_SCISSOR_TEST);
			}
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
		}
#endif
		ERR_FAIL_COND_MSG(!eglSwapBuffers(egl_display, egl_surface), "OpenHarmony EGL buffer swap failed.");
	}
#endif
}

void DisplayServerOpenHarmony::release_rendering_thread() {
#ifdef GLES3_ENABLED
	if (egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
#endif
}

void DisplayServerOpenHarmony::gl_window_make_current(DisplayServerEnums::WindowID p_window_id) {
#ifdef GLES3_ENABLED
	if (egl_surface != EGL_NO_SURFACE) {
		ERR_FAIL_COND(!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context));
	}
#endif
}

bool DisplayServerOpenHarmony::window_is_maximize_allowed(DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerOpenHarmony::window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

bool DisplayServerOpenHarmony::window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerOpenHarmony::window_request_attention(DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_move_to_foreground(DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

bool DisplayServerOpenHarmony::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerOpenHarmony::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerOpenHarmony::can_any_window_draw() const {
	return true;
}

void DisplayServerOpenHarmony::process_events() {
	Input::get_singleton()->flush_buffered_events();
}
