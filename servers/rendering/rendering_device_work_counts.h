/**************************************************************************/
/*  rendering_device_work_counts.h                                        */
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

// The `profiler=perf_units` GPU channel: deterministic work counts per present. Every draw the graph replays is wrapped in a pipeline statistics query (vertex and
// fragment shader invocations, primitives clipped) and charged to its pipeline's shader; render
// passes, attachment bytes loaded and stored, copies and uploads are counted as they are recorded.
// Printed at exit as `[perf] gpu ...` lines; on with GODOT_PERF_ZONES, like the engine zones.

#include "core/profiling/profiling.h"

#ifdef GODOT_USE_PERF_UNITS

#include "core/math/vector2i.h"
#include "core/profiling/count_histogram.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device_driver.h"

#include <mutex>

class RenderingDeviceWorkCounts {
public:
	// Values per query, in the driver's pipeline statistics order.
	enum Statistic {
		STATISTIC_VERTEX_INVOCATIONS,
		STATISTIC_CLIPPING_INVOCATIONS,
		STATISTIC_CLIPPING_PRIMITIVES,
		STATISTIC_FRAGMENT_INVOCATIONS,
		STATISTIC_MAX
	};

private:
	static constexpr uint32_t QUERIES_PER_FRAME = 16384;

	struct FrameCounts {
		uint64_t render_passes = 0;
		uint64_t screen_passes = 0;
		uint64_t pass_bytes_loaded = 0;
		uint64_t pass_bytes_stored = 0;
		uint64_t copy_bytes = 0;
		uint64_t upload_bytes = 0;
		uint64_t draws = 0;
		uint64_t pipeline_binds = 0;
	};

	struct Pass {
		Size2i size;
		uint32_t first_query = 0;
		uint32_t draws = 0;
	};

	struct Slot {
		RDD::QueryPoolID pool;
		LocalVector<Pass> passes; // As the graph replays them.
		bool counting = false;
		FrameCounts counts;
		LocalVector<uint32_t> query_shaders; // A shader index per query.
	};

	struct Shader {
		String name;
		CountHistogram fragments;
		CountHistogram draws;
		uint64_t frame_fragments = 0;
		uint64_t frame_draws = 0;
	};

	RenderingDeviceDriver *driver = nullptr;
	bool enabled = false;
	LocalVector<Slot> slots;
	uint32_t recording = 0;
	uint32_t open_query = UINT32_MAX;
	uint32_t open_shader = 0;
	uint64_t screen_pixels = 0;
	uint64_t frames = 0;
	uint64_t dropped_queries = 0;
	std::mutex names_mutex; // Pipelines are created on worker threads.
	HashMap<uint64_t, uint32_t> pipeline_shaders;
	HashMap<String, uint32_t> shader_by_name;
	LocalVector<Shader *> shaders;
	LocalVector<uint64_t> results;
	CountHistogram statistics[STATISTIC_MAX];
	CountHistogram render_passes;
	CountHistogram screen_passes;
	CountHistogram pass_bytes_loaded;
	CountHistogram pass_bytes_stored;
	CountHistogram copy_bytes;
	CountHistogram upload_bytes;
	CountHistogram draws;
	CountHistogram pipeline_binds;
	String shader_dump_dir;
	String last_passes; // The last counted present's passes, for the report.

	bool _counting() const;
	uint32_t _shader_index(const String &p_name);
	void _collect(Slot &p_slot);

public:
	void initialize(RenderingDeviceDriver *p_driver, uint32_t p_frame_count);
	void finalize();

	// Frame boundaries: the slot's fence has been waited on; its queries are read and reset.
	void frame_begin(uint32_t p_frame, RDD::CommandBufferID p_command_buffer);

	// Recording, on the render thread.
	void pipeline_named(RDD::PipelineID p_pipeline, const String &p_name);
	void render_pass(uint64_t p_pixels, uint64_t p_bytes_loaded, uint64_t p_bytes_stored, bool p_screen);
	void copy(uint64_t p_bytes);
	void upload(uint64_t p_bytes);
	// Writes a canvas shader variant's GLSL under GODOT_PERF_SHADER_DUMP, once per name and variant.
	void dump_shader(const String &p_name, int p_variant, uint32_t p_specialization, const Vector<String> &p_stage_sources);

	// Replay, from the graph: each render pass and draw of a primary command buffer.
	void pass_replayed(const Size2i &p_size);
	void pipeline_bound(RDD::PipelineID p_pipeline);
	void draw_begin(RDD::CommandBufferID p_command_buffer);
	void draw_end(RDD::CommandBufferID p_command_buffer);

	void report();
};

#endif // GODOT_USE_PERF_UNITS
