/**************************************************************************/
/*  test_audio_driver_dummy.cpp                                           */
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

TEST_FORCE_LINK(test_audio_driver_dummy)

#include "servers/audio/audio_driver_dummy.h"

namespace TestAudioDriverDummy {

TEST_CASE("[AudioDriverDummy] Frame clock mixes exactly the elapsed audio") {
	SUBCASE("60 frames per second at 44.1 kHz") {
		double carry = 0.0;
		uint64_t total = 0;
		for (int i = 0; i < 600; i++) {
			const uint32_t frames = AudioDriverDummy::frames_for_duration(1.0 / 60.0, 44100, carry);
			CHECK(frames == 735);
			total += frames;
		}
		CHECK(total == 441000);
	}

	SUBCASE("Fractional frames carry over instead of drifting") {
		double carry = 0.0;
		uint64_t total = 0;
		for (int i = 0; i < 7 * 100; i++) {
			total += AudioDriverDummy::frames_for_duration(1.0 / 7.0, 48000, carry);
		}
		CHECK(total == 48000 * 100);
		CHECK(carry < 1.0);
	}

	SUBCASE("No time, no audio") {
		double carry = 0.0;
		CHECK(AudioDriverDummy::frames_for_duration(0.0, 44100, carry) == 0);
		CHECK(carry == 0.0);
	}

	SUBCASE("Invalid input mixes nothing") {
		double carry = 0.25;
		ERR_PRINT_OFF;
		CHECK(AudioDriverDummy::frames_for_duration(-1.0, 44100, carry) == 0);
		CHECK(AudioDriverDummy::frames_for_duration(1.0, 0, carry) == 0);
		ERR_PRINT_ON;
		CHECK(carry == 0.25);
	}
}

} // namespace TestAudioDriverDummy
