/**************************************************************************/
/*  perf_zones.h                                                          */
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

#pragma once

// The `profiler=perf_units` backend: user-space instructions retired per profiling zone, counted by
// each thread on its own counter, printed at exit as `[perf] engine <zone> ...` lines, kept inside
// the engine so the SCons build needs nothing outside it.

#include "core/typedefs.h"

#include <atomic>
#include <cstdint>

class StringName;

namespace PerfZones {

// Counting runs from the frame mark named by GODOT_PERF_ZONES (frames to skip) to exit.
extern std::atomic<bool> counting;

// Zone ids start at 1; 0 names no zone.
uint32_t register_zone(const char *p_name);
// A script function's zone, `script <name>` (or `script call <name>` for a call from a script).
uint32_t register_script(const StringName &p_name, bool p_call);
uint32_t begin(uint32_t p_zone); // Returns the stack depth, 0 when nothing opened.
void end(uint32_t p_depth);
void frame_mark();
void init();
void report();

} // namespace PerfZones

class PerfZoneScope {
	uint32_t depth = 0;

public:
	_FORCE_INLINE_ explicit PerfZoneScope(uint32_t p_zone) {
		if (p_zone != 0 && PerfZones::counting.load(std::memory_order_relaxed)) {
			depth = PerfZones::begin(p_zone);
		}
	}
	_FORCE_INLINE_ void end_now() {
		if (depth != 0) {
			PerfZones::end(depth);
			depth = 0;
		}
	}
	_FORCE_INLINE_ void next(uint32_t p_zone) {
		end_now();
		if (p_zone != 0 && PerfZones::counting.load(std::memory_order_relaxed)) {
			depth = PerfZones::begin(p_zone);
		}
	}
	_FORCE_INLINE_ ~PerfZoneScope() { end_now(); }
	PerfZoneScope(const PerfZoneScope &) = delete;
	PerfZoneScope &operator=(const PerfZoneScope &) = delete;
};
