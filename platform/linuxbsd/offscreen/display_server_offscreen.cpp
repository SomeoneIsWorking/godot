/**************************************************************************/
/*  display_server_offscreen.cpp                                          */
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

#include "display_server_offscreen.h"

#if defined(RD_ENABLED)
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/rendering_device.h"

#if defined(VULKAN_ENABLED)
#include "drivers/vulkan/rendering_context_driver_vulkan_headless.h"
#endif
#endif

#if defined(GLES3_ENABLED)
#include "egl_manager_surfaceless.h"

#include "drivers/gles3/rasterizer_gles3.h"
#endif

Vector<String> DisplayServerOffscreen::get_rendering_drivers_func() {
	Vector<String> drivers;
#if defined(VULKAN_ENABLED)
	drivers.push_back("vulkan");
#endif
#if defined(GLES3_ENABLED)
	drivers.push_back("opengl3");
#endif
	return drivers;
}

DisplayServer *DisplayServerOffscreen::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerOffscreen(p_rendering_driver, p_resolution, p_vsync_mode, r_error));
	if (r_error != OK) {
		memdelete(ds);
		return nullptr;
	}
	return ds;
}

void DisplayServerOffscreen::register_offscreen_driver() {
	register_create_function("offscreen", create_func, get_rendering_drivers_func, true);
}

Error DisplayServerOffscreen::_initialize_vulkan() {
#if defined(RD_ENABLED) && defined(VULKAN_ENABLED)
	rendering_context = memnew(RenderingContextDriverVulkanHeadless);
	// Initialization fails with a message naming the missing instance extension,
	// such as VK_EXT_headless_surface.
	ERR_FAIL_COND_V_MSG(rendering_context->initialize() != OK, ERR_UNAVAILABLE, "Offscreen rendering requires a Vulkan driver that supports VK_EXT_headless_surface.");

	const RenderingContextDriverVulkanHeadless::WindowPlatformData platform_data = {};
	ERR_FAIL_COND_V_MSG(rendering_context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &platform_data) != OK, ERR_CANT_CREATE, "Can't create a headless Vulkan surface.");
	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, window_size.width, window_size.height);
	rendering_context->window_set_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID, vsync_mode);

	rendering_device = memnew(RenderingDevice);
	ERR_FAIL_COND_V_MSG(rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID) != OK, ERR_UNAVAILABLE, "Can't initialize a Vulkan rendering device for offscreen rendering.");
	ERR_FAIL_COND_V_MSG(rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID) != OK, ERR_CANT_CREATE, "Can't create a swap chain on the headless Vulkan surface.");

	RendererCompositorRD::make_current();
	return OK;
#else
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "This build has no Vulkan support.");
#endif
}

Error DisplayServerOffscreen::_initialize_opengl() {
#if defined(GLES3_ENABLED)
	egl_manager = memnew(EGLManagerSurfaceless);
	// Initialization fails with a message naming the missing EGL platform extension.
	ERR_FAIL_COND_V_MSG(egl_manager->initialize(nullptr) != OK || egl_manager->open_display(nullptr) != OK, ERR_UNAVAILABLE, "Offscreen OpenGL rendering requires an EGL implementation with EGL_MESA_platform_surfaceless and OpenGL 3.3.");
	ERR_FAIL_COND_V_MSG(egl_manager->window_create_pbuffer(DisplayServerEnums::MAIN_WINDOW_ID, nullptr, window_size.width, window_size.height) != OK, ERR_CANT_CREATE, "Can't create an EGL pbuffer for offscreen rendering.");
	egl_manager->set_use_vsync(vsync_mode != DisplayServerEnums::VSYNC_DISABLED);

	RasterizerGLES3::make_current(true);
	return OK;
#else
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "This build has no OpenGL support.");
#endif
}

Vector<DisplayServerEnums::WindowID> DisplayServerOffscreen::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> windows;
	windows.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return windows;
}

DisplayServerEnums::WindowID DisplayServerOffscreen::create_sub_window(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Rect2i &p_rect, bool p_exclusive, DisplayServerEnums::WindowID p_transient_parent) {
	ERR_FAIL_V_MSG(DisplayServerEnums::INVALID_WINDOW_ID, "The offscreen display server only has the main window. Enable \"display/window/subwindows/embed_subwindows\" to render subwindows into it.");
}

void DisplayServerOffscreen::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(p_window != DisplayServerEnums::MAIN_WINDOW_ID);
	rect_changed_callback = p_callable;
}

void DisplayServerOffscreen::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(p_window != DisplayServerEnums::MAIN_WINDOW_ID);
	ERR_FAIL_COND_MSG(p_size.width <= 0 || p_size.height <= 0, "The offscreen window size must be positive.");
	if (p_size == window_size) {
		return;
	}
	window_size = p_size;

#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_size(p_window, window_size.width, window_size.height);
	}
#endif
#if defined(GLES3_ENABLED)
	if (egl_manager) {
		// Pbuffers can't be resized; replace the surface.
		egl_manager->window_destroy(p_window);
		const Error err = egl_manager->window_create_pbuffer(p_window, nullptr, window_size.width, window_size.height);
		ERR_FAIL_COND_MSG(err != OK, "Can't resize the offscreen EGL pbuffer.");
	}
#endif

	if (rect_changed_callback.is_valid()) {
		rect_changed_callback.call(Rect2i(Point2i(), window_size));
	}
}

Size2i DisplayServerOffscreen::window_get_size(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, Size2i());
	return window_size;
}

Size2i DisplayServerOffscreen::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return window_get_size(p_window);
}

void DisplayServerOffscreen::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(p_window != DisplayServerEnums::MAIN_WINDOW_ID);
	vsync_mode = p_vsync_mode;
#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
#endif
#if defined(GLES3_ENABLED)
	if (egl_manager) {
		egl_manager->set_use_vsync(p_vsync_mode != DisplayServerEnums::VSYNC_DISABLED);
	}
#endif
}

void DisplayServerOffscreen::gl_window_make_current(DisplayServerEnums::WindowID p_window_id) {
#if defined(GLES3_ENABLED)
	if (egl_manager) {
		egl_manager->window_make_current(p_window_id);
	}
#endif
}

void DisplayServerOffscreen::release_rendering_thread() {
#if defined(GLES3_ENABLED)
	if (egl_manager) {
		egl_manager->release_current();
	}
#endif
}

void DisplayServerOffscreen::swap_buffers() {
#if defined(GLES3_ENABLED)
	if (egl_manager) {
		egl_manager->swap_buffers();
	}
#endif
}

DisplayServerOffscreen::DisplayServerOffscreen(const String &p_rendering_driver, const Size2i &p_resolution, DisplayServerEnums::VSyncMode p_vsync_mode, Error &r_error) {
	rendering_driver = p_rendering_driver;
	window_size = p_resolution;
	vsync_mode = p_vsync_mode;

	if (window_size.width <= 0 || window_size.height <= 0) {
		r_error = ERR_INVALID_PARAMETER;
		ERR_FAIL_MSG(vformat("Invalid offscreen window size %s.", window_size));
	}

	if (rendering_driver == "vulkan") {
		r_error = _initialize_vulkan();
	} else if (rendering_driver == "opengl3") {
		r_error = _initialize_opengl();
	} else {
		r_error = ERR_UNAVAILABLE;
		ERR_FAIL_MSG(vformat("The offscreen display server can't render with the \"%s\" driver. Supported drivers: %s.", rendering_driver, String(", ").join(get_rendering_drivers_func())));
	}
}

DisplayServerOffscreen::~DisplayServerOffscreen() {
#if defined(RD_ENABLED)
	if (rendering_device) {
		memdelete(rendering_device);
		rendering_device = nullptr;
	}
	if (rendering_context) {
		memdelete(rendering_context);
		rendering_context = nullptr;
	}
#endif
#if defined(GLES3_ENABLED)
	if (egl_manager) {
		memdelete(egl_manager);
		egl_manager = nullptr;
	}
#endif
}
