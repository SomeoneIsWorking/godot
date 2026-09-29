/**************************************************************************/
/*  test_display_server_registration.cpp                                  */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_display_server_registration)

#include "servers/display/display_server.h"

namespace TestDisplayServerRegistration {

TEST_CASE("[DisplayServer] Explicit-only servers follow every regular server") {
	const int count = DisplayServer::get_create_function_count();
	REQUIRE(count >= 1);

	// Headless is registered statically and stays last.
	CHECK(String(DisplayServer::get_create_function_name(count - 1)) == "headless");
	CHECK(DisplayServer::is_create_function_explicit_only(count - 1));

	// Once an explicit-only server appears, no regular server may follow it, so that the
	// default (index 0) and every fallback candidate are regular servers.
	bool seen_explicit_only = false;
	for (int i = 0; i < count; i++) {
		const bool explicit_only = DisplayServer::is_create_function_explicit_only(i);
		if (seen_explicit_only) {
			CHECK_MESSAGE(explicit_only, vformat("Regular display server \"%s\" was registered after an explicit-only one.", DisplayServer::get_create_function_name(i)));
		}
		seen_explicit_only = seen_explicit_only || explicit_only;
	}
}

TEST_CASE("[DisplayServer] Out-of-range server indices are treated as explicit-only") {
	ERR_PRINT_OFF;
	CHECK(DisplayServer::is_create_function_explicit_only(-1));
	CHECK(DisplayServer::is_create_function_explicit_only(DisplayServer::get_create_function_count()));
	ERR_PRINT_ON;
}

} // namespace TestDisplayServerRegistration
