/**************************************************************************/
/*  count_histogram.h                                                     */
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

#include "core/typedefs.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Constant-memory percentiles of per-frame or per-call counts: exact
// below 64, then 64 buckets per power of two (at most 1.6% high); max and total exact.
class CountHistogram {
	static constexpr unsigned SUB_BITS = 6;
	static constexpr uint64_t SUB = uint64_t(1) << SUB_BITS;
	static constexpr size_t BUCKETS = SUB + (64 - SUB_BITS) * SUB;
	uint32_t buckets[BUCKETS] = {};

	static size_t bucket_of(uint64_t p_value) {
		if (p_value < SUB) {
			return size_t(p_value);
		}
		const unsigned shift = unsigned(63 - __builtin_clzll(p_value)) - SUB_BITS;
		return SUB + shift * SUB + size_t((p_value >> shift) - SUB);
	}
	static uint64_t bucket_top(size_t p_bucket) {
		if (p_bucket < SUB) {
			return p_bucket;
		}
		const size_t shift = (p_bucket - SUB) / SUB;
		const uint64_t mantissa = SUB + (p_bucket - SUB) % SUB;
		return ((mantissa + 1) << shift) - 1;
	}

public:
	uint64_t count = 0;
	uint64_t total = 0;
	uint64_t max = 0;

	void add(uint64_t p_value) {
		buckets[bucket_of(p_value)]++;
		count++;
		total += p_value;
		max = MAX(max, p_value);
	}
	uint64_t percentile(double p_fraction) const {
		if (count == 0) {
			return 0;
		}
		const uint64_t rank = MAX(uint64_t(1), uint64_t(p_fraction * double(count) + 0.999999));
		uint64_t seen = 0;
		for (size_t i = 0; i < BUCKETS; i++) {
			seen += buckets[i];
			if (seen >= rank) {
				return MIN(bucket_top(i), max);
			}
		}
		return max;
	}
	// One perf-units line: `[perf] <p_name>: p50 .. p99 .. max .. <unit>/<per>, total .. <unit>`.
	void print(const char *p_name, const char *p_unit, const char *p_per) const {
		printf("[perf] %s: p50 %llu p99 %llu max %llu %s/%s, total %llu %s\n", p_name,
				(unsigned long long)percentile(0.5), (unsigned long long)percentile(0.99),
				(unsigned long long)max, p_unit, p_per, (unsigned long long)total, p_unit);
	}
};
