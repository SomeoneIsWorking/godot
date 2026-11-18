/**************************************************************************/
/*  perf_zones.cpp                                                        */
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

#include "core/profiling/profiling.gen.h"

#ifdef GODOT_USE_PERF_UNITS

#include "perf_zones.h"

#include "core/profiling/count_histogram.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace PerfZones {

std::atomic<bool> counting{ false };

namespace {

constexpr uint32_t MAX_ZONES = 1024;
constexpr uint32_t MAX_DEPTH = 64;

struct Zone {
	char name[96] = {};
	CountHistogram self;
	CountHistogram inclusive;
	CountHistogram per_frame;
	CountHistogram calls;
	std::atomic<uint64_t> frame_self{ 0 };
	std::atomic<uint64_t> frame_calls{ 0 };
};

struct Open {
	uint32_t zone = 0;
	uint64_t start = 0;
	uint64_t child = 0;
};

// One thread's counter and its open zones.
struct ThreadState {
	int fds[2] = { -1, -1 };
	bool opened = false;
	Open stack[MAX_DEPTH];
	uint32_t depth = 0;

	~ThreadState() {
#ifdef __linux__
		for (int fd : fds) {
			if (fd >= 0) {
				close(fd);
			}
		}
#endif
	}
};

Zone *zones[MAX_ZONES] = {};
std::atomic<uint32_t> zone_count{ 1 };
std::mutex mutex; // registration, per-call histograms, frame close
HashMap<StringName, uint32_t> script_zones[2];
bool instructions = false;
int64_t skip_frames = -1; // GODOT_PERF_ZONES; -1: off
uint64_t frames_seen = 0;
uint64_t frames = 0;
thread_local ThreadState thread_state;

#ifdef __linux__
uint64_t hybrid_pmu_type(const char *p_pmu) {
	char path[128];
	snprintf(path, sizeof(path), "/sys/bus/event_source/devices/%s/type", p_pmu);
	FILE *file = fopen(path, "r");
	if (!file) {
		return 0;
	}
	unsigned long long type = 0;
	const int read = fscanf(file, "%llu", &type);
	fclose(file);
	return read == 1 ? type : 0;
}

int open_counter(uint64_t p_pmu_type) {
	perf_event_attr attr = {};
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_HARDWARE;
	// A hybrid PMU is named in the config's high word (PERF_PMU_TYPE_SHIFT).
	attr.config = (p_pmu_type << 32) | PERF_COUNT_HW_INSTRUCTIONS;
	attr.exclude_kernel = 1;
	attr.exclude_hv = 1;
	return int(syscall(SYS_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC));
}

// The calling thread's instruction counter, one event per PMU of a hybrid CPU.
bool open_instructions(int *r_fds) {
	int opened = 0;
	for (const char *pmu : { "cpu_core", "cpu_atom" }) {
		if (const uint64_t type = hybrid_pmu_type(pmu)) {
			r_fds[opened++] = open_counter(type);
		}
	}
	if (opened == 0) {
		r_fds[opened++] = open_counter(0);
	}
	for (int i = 0; i < opened; i++) {
		if (r_fds[i] < 0) {
			for (int j = 0; j < opened; j++) {
				if (r_fds[j] >= 0) {
					close(r_fds[j]);
				}
				r_fds[j] = -1;
			}
			return false;
		}
	}
	return true;
}
#endif

uint64_t read_work(ThreadState &p_state) {
	if (!instructions) {
		timespec now = {};
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
		return uint64_t(now.tv_sec) * 1000000000 + uint64_t(now.tv_nsec);
	}
	uint64_t total = 0;
#ifdef __linux__
	if (!p_state.opened) {
		p_state.opened = true;
		open_instructions(p_state.fds);
	}
	for (int fd : p_state.fds) {
		uint64_t value = 0;
		if (fd >= 0 && ::read(fd, &value, sizeof(value)) == ssize_t(sizeof(value))) {
			total += value;
		}
	}
#endif
	return total;
}

void print_line_for(const char *p_zone, const char *p_what, const CountHistogram &p_work, const char *p_unit, const char *p_per) {
	char name[160];
	snprintf(name, sizeof(name), "engine %s %s", p_zone, p_what);
	p_work.print(name, p_unit, p_per);
}

} // namespace

uint32_t register_zone(const char *p_name) {
	std::lock_guard<std::mutex> lock(mutex);
	const uint32_t id = zone_count.load(std::memory_order_relaxed);
	for (uint32_t i = 1; i < id; i++) {
		if (strcmp(zones[i]->name, p_name) == 0) {
			return i;
		}
	}
	if (id == MAX_ZONES) {
		return 0;
	}
	Zone *zone = new Zone;
	// The report's names are perf_compare names: no colons.
	size_t out = 0;
	for (const char *c = p_name; *c && out + 1 < sizeof(zone->name); c++) {
		if (*c == ':') {
			if (c[1] == ':') {
				c++;
			}
			zone->name[out++] = '.';
		} else {
			zone->name[out++] = *c;
		}
	}
	// Frames counted before it ran had none of its calls.
	for (uint64_t i = 0; i < frames; i++) {
		zone->per_frame.add(0);
		zone->calls.add(0);
	}
	zones[id] = zone;
	zone_count.store(id + 1, std::memory_order_release);
	return id;
}

uint32_t register_script(const StringName &p_name, bool p_call) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (const uint32_t *id = script_zones[p_call].getptr(p_name)) {
			return *id;
		}
	}
	const String name = String(p_call ? "script call " : "script ") + String(p_name);
	const uint32_t id = register_zone(name.utf8().get_data());
	std::lock_guard<std::mutex> lock(mutex);
	script_zones[p_call][p_name] = id;
	return id;
}

uint32_t begin(uint32_t p_zone) {
	ThreadState &state = thread_state;
	if (state.depth == MAX_DEPTH) {
		fprintf(stderr, "perf zones: nested deeper than %u\n", MAX_DEPTH);
		abort();
	}
	Open &open = state.stack[state.depth++];
	open.zone = p_zone;
	open.child = 0;
	open.start = read_work(state);
	return state.depth;
}

void end(uint32_t p_depth) {
	ThreadState &state = thread_state;
	const uint64_t now = read_work(state);
	if (p_depth != state.depth) {
		fprintf(stderr, "perf zones: %s ended while %s, opened inside it, is open\n", zones[state.stack[p_depth - 1].zone]->name, zones[state.stack[state.depth - 1].zone]->name);
		abort();
	}
	const Open &open = state.stack[--state.depth];
	const uint64_t inclusive = now - open.start;
	const uint64_t self = inclusive - open.child;
	if (state.depth > 0) {
		state.stack[state.depth - 1].child += inclusive;
	}
	Zone *zone = zones[open.zone];
	zone->frame_self.fetch_add(self, std::memory_order_relaxed);
	zone->frame_calls.fetch_add(1, std::memory_order_relaxed);
	std::lock_guard<std::mutex> lock(mutex);
	zone->self.add(self);
	zone->inclusive.add(inclusive);
}

void frame_mark() {
	if (skip_frames < 0) {
		return;
	}
	if (counting.load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(mutex);
		frames++;
		const uint32_t count = zone_count.load(std::memory_order_acquire);
		for (uint32_t i = 1; i < count; i++) {
			zones[i]->per_frame.add(zones[i]->frame_self.exchange(0, std::memory_order_relaxed));
			zones[i]->calls.add(zones[i]->frame_calls.exchange(0, std::memory_order_relaxed));
		}
	} else if (int64_t(++frames_seen) > skip_frames) {
		counting.store(true, std::memory_order_relaxed);
	}
}

void init() {
	const char *skip = getenv("GODOT_PERF_ZONES");
	if (!skip) {
		return;
	}
	skip_frames = atoll(skip);
#ifdef __linux__
	int fds[2] = { -1, -1 };
	instructions = open_instructions(fds);
	for (int fd : fds) {
		if (fd >= 0) {
			close(fd);
		}
	}
#endif
}

void report() {
	counting.store(false, std::memory_order_relaxed);
	if (skip_frames < 0 || frames == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex);
	const char *unit = instructions ? "insn" : "cpu-ns";
	printf("[perf] engine run-end: %llu frames, %s; self excludes nested zones\n", (unsigned long long)frames,
			instructions ? "user instructions retired" : "thread CPU time ns (fallback: no instruction counter opens here; moves with load and clock speed)");
	const uint32_t count = zone_count.load(std::memory_order_acquire);
	for (uint32_t i = 1; i < count; i++) {
		const Zone &zone = *zones[i];
		if (zone.self.count == 0) {
			continue;
		}
		print_line_for(zone.name, "self", zone.self, unit, "call");
		print_line_for(zone.name, "inclusive", zone.inclusive, unit, "call");
		print_line_for(zone.name, "per frame", zone.per_frame, unit, "frame");
		print_line_for(zone.name, "calls", zone.calls, "calls", "frame");
	}
	fflush(stdout);
}

} // namespace PerfZones

#endif // GODOT_USE_PERF_UNITS
