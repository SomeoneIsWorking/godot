/**************************************************************************/
/*  egl_manager_surfaceless.cpp                                           */
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

#include "egl_manager_surfaceless.h"

#ifdef EGL_ENABLED
#ifdef GLES3_ENABLED

// Defined by EGL_MESA_platform_surfaceless; the bundled glad header predates it.
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

const char *EGLManagerSurfaceless::_get_platform_extension_name() const {
	return "EGL_MESA_platform_surfaceless";
}

bool EGLManagerSurfaceless::_is_platform_extension_required() const {
	return true;
}

EGLenum EGLManagerSurfaceless::_get_platform_extension_enum() const {
	return EGL_PLATFORM_SURFACELESS_MESA;
}

EGLenum EGLManagerSurfaceless::_get_platform_api_enum() const {
	return EGL_OPENGL_API;
}

Vector<EGLAttrib> EGLManagerSurfaceless::_get_platform_display_attributes() const {
	return Vector<EGLAttrib>();
}

Vector<EGLint> EGLManagerSurfaceless::_get_platform_context_attribs() const {
	Vector<EGLint> ret;
	ret.push_back(EGL_CONTEXT_MAJOR_VERSION);
	ret.push_back(3);
	ret.push_back(EGL_CONTEXT_MINOR_VERSION);
	ret.push_back(3);
	ret.push_back(EGL_CONTEXT_OPENGL_PROFILE_MASK);
	ret.push_back(EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT);
	ret.push_back(EGL_NONE);
	return ret;
}

EGLint EGLManagerSurfaceless::_get_platform_surface_type() const {
	return EGL_PBUFFER_BIT;
}

#endif // GLES3_ENABLED
#endif // EGL_ENABLED
