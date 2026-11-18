/**************************************************************************/
/*  rendering_device_work_counts.cpp                                      */
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

#include "rendering_device_work_counts.h"

#ifdef GODOT_USE_PERF_UNITS

#include "core/io/file_access.h"
#include "core/os/os.h"

#include <cstdio>

bool RenderingDeviceWorkCounts::_counting() const {
	return enabled && PerfZones::counting.load(std::memory_order_relaxed);
}

uint32_t RenderingDeviceWorkCounts::_shader_index(const String &p_name) {
	if (const uint32_t *index = shader_by_name.getptr(p_name)) {
		return *index;
	}
	Shader *shader = new Shader;
	shader->name = p_name;
	// Frames counted before it was named had none of its work.
	for (uint64_t i = 0; i < frames; i++) {
		shader->fragments.add(0);
		shader->draws.add(0);
	}
	const uint32_t index = shaders.size();
	shaders.push_back(shader);
	shader_by_name[p_name] = index;
	return index;
}

void RenderingDeviceWorkCounts::initialize(RenderingDeviceDriver *p_driver, uint32_t p_frame_count) {
	if (OS::get_singleton()->get_environment("GODOT_PERF_ZONES").is_empty()) {
		return;
	}
	driver = p_driver;
	slots.resize(p_frame_count);
	for (Slot &slot : slots) {
		slot.pool = driver->pipeline_statistics_query_pool_create(QUERIES_PER_FRAME);
		if (!slot.pool) {
			fprintf(stderr, "perf gpu: the driver has no pipeline statistics queries; GPU work counts are off\n");
			finalize();
			return;
		}
	}
	results.resize(QUERIES_PER_FRAME * STATISTIC_MAX);
	shader_dump_dir = OS::get_singleton()->get_environment("GODOT_PERF_SHADER_DUMP");
	enabled = true;
}

void RenderingDeviceWorkCounts::finalize() {
	for (Slot &slot : slots) {
		if (slot.pool) {
			driver->pipeline_statistics_query_pool_free(slot.pool);
			slot.pool = RDD::QueryPoolID();
		}
	}
	slots.clear();
	enabled = false;
}

void RenderingDeviceWorkCounts::_collect(Slot &p_slot) {
	std::lock_guard<std::mutex> lock(names_mutex);
	const uint32_t query_count = p_slot.query_shaders.size();
	if (query_count > 0) {
		driver->pipeline_statistics_query_pool_get_results(p_slot.pool, query_count, results.ptr());
	}
	uint64_t totals[STATISTIC_MAX] = {};
	for (uint32_t q = 0; q < query_count; q++) {
		const uint64_t *values = &results[q * STATISTIC_MAX];
		for (int s = 0; s < STATISTIC_MAX; s++) {
			totals[s] += values[s];
		}
		Shader *shader = shaders[p_slot.query_shaders[q]];
		shader->frame_fragments += values[STATISTIC_FRAGMENT_INVOCATIONS];
		shader->frame_draws++;
	}
	for (int s = 0; s < STATISTIC_MAX; s++) {
		statistics[s].add(totals[s]);
	}
	last_passes = String();
	for (uint32_t i = 0; i < p_slot.passes.size(); i++) {
		const Pass &pass = p_slot.passes[i];
		uint64_t fragments = 0;
		HashMap<uint32_t, uint64_t> by_shader;
		for (uint32_t q = pass.first_query; q < pass.first_query + pass.draws && q < query_count; q++) {
			fragments += results[q * STATISTIC_MAX + STATISTIC_FRAGMENT_INVOCATIONS];
			by_shader[p_slot.query_shaders[q]] += results[q * STATISTIC_MAX + STATISTIC_FRAGMENT_INVOCATIONS];
		}
		String most;
		uint64_t most_fragments = 0;
		for (const KeyValue<uint32_t, uint64_t> &E : by_shader) {
			if (E.value >= most_fragments) {
				most_fragments = E.value;
				most = shaders[E.key]->name;
			}
		}
		last_passes += vformat("[perf] gpu pass %d: %dx%d, %d draws, %d px shaded, most by %s\n", i, pass.size.x, pass.size.y, pass.draws, fragments, most);
	}
	for (Shader *shader : shaders) {
		shader->fragments.add(shader->frame_fragments);
		shader->draws.add(shader->frame_draws);
		shader->frame_fragments = 0;
		shader->frame_draws = 0;
	}
	const FrameCounts &counts = p_slot.counts;
	render_passes.add(counts.render_passes);
	screen_passes.add(counts.screen_passes);
	pass_bytes_loaded.add(counts.pass_bytes_loaded);
	pass_bytes_stored.add(counts.pass_bytes_stored);
	copy_bytes.add(counts.copy_bytes);
	upload_bytes.add(counts.upload_bytes);
	draws.add(counts.draws);
	pipeline_binds.add(counts.pipeline_binds);
	frames++;
}

void RenderingDeviceWorkCounts::frame_begin(uint32_t p_frame, RDD::CommandBufferID p_command_buffer) {
	if (!enabled) {
		return;
	}
	Slot &slot = slots[p_frame];
	// A frame without a render pass presented nothing (a flush while loading or quitting).
	if (slot.counting && !slot.passes.is_empty()) {
		_collect(slot);
	}
	recording = p_frame;
	slot.counting = _counting();
	slot.counts = FrameCounts();
	slot.query_shaders.clear();
	slot.passes.clear();
	if (slot.counting) {
		driver->command_pipeline_statistics_query_pool_reset(p_command_buffer, slot.pool, QUERIES_PER_FRAME);
	}
}

void RenderingDeviceWorkCounts::pipeline_named(RDD::PipelineID p_pipeline, const String &p_name) {
	if (!enabled) {
		return;
	}
	std::lock_guard<std::mutex> lock(names_mutex);
	// perf_compare names: no colons.
	pipeline_shaders[p_pipeline.id] = _shader_index(p_name.trim_prefix("res://").replace(":", "."));
}

void RenderingDeviceWorkCounts::render_pass(uint64_t p_pixels, uint64_t p_bytes_loaded, uint64_t p_bytes_stored, bool p_screen) {
	if (!enabled) {
		return;
	}
	if (p_screen) {
		screen_pixels = p_pixels;
	}
	FrameCounts &counts = slots[recording].counts;
	counts.render_passes++;
	// A pass over the whole screen: the swap chain's own, or one as large as it.
	if (screen_pixels > 0 && p_pixels >= screen_pixels) {
		counts.screen_passes++;
	}
	counts.pass_bytes_loaded += p_bytes_loaded;
	counts.pass_bytes_stored += p_bytes_stored;
}

void RenderingDeviceWorkCounts::copy(uint64_t p_bytes) {
	if (enabled) {
		slots[recording].counts.copy_bytes += p_bytes;
	}
}

void RenderingDeviceWorkCounts::upload(uint64_t p_bytes) {
	if (enabled) {
		slots[recording].counts.upload_bytes += p_bytes;
	}
}

void RenderingDeviceWorkCounts::dump_shader(const String &p_name, int p_variant, uint32_t p_specialization, const Vector<String> &p_stage_sources) {
	if (!enabled || shader_dump_dir.is_empty() || p_stage_sources.size() <= RDD::SHADER_STAGE_FRAGMENT) {
		return;
	}
	const String base = shader_dump_dir.path_join(p_name.replace("res://", "").replace("/", "_").replace(" ", "_") + vformat(".v%d.s%x", p_variant, p_specialization));
	if (FileAccess::exists(base + ".frag")) {
		return;
	}
	const char *extensions[] = { ".vert", ".frag" };
	const int stages[] = { RDD::SHADER_STAGE_VERTEX, RDD::SHADER_STAGE_FRAGMENT };
	for (int i = 0; i < 2; i++) {
		Ref<FileAccess> file = FileAccess::open(base + extensions[i], FileAccess::WRITE);
		ERR_FAIL_COND_MSG(file.is_null(), "perf gpu: cannot write " + base + extensions[i]);
		file->store_string(p_stage_sources[stages[i]]);
	}
}

void RenderingDeviceWorkCounts::pass_replayed(const Size2i &p_size) {
	if (!enabled || !slots[recording].counting) {
		return;
	}
	Slot &slot = slots[recording];
	Pass pass;
	pass.size = p_size;
	pass.first_query = slot.query_shaders.size();
	slot.passes.push_back(pass);
}

void RenderingDeviceWorkCounts::pipeline_bound(RDD::PipelineID p_pipeline) {
	if (!enabled || !slots[recording].counting) {
		return;
	}
	slots[recording].counts.pipeline_binds++;
	std::lock_guard<std::mutex> lock(names_mutex);
	const uint32_t *shader = pipeline_shaders.getptr(p_pipeline.id);
	open_shader = shader ? *shader : _shader_index("unnamed pipeline");
}

void RenderingDeviceWorkCounts::draw_begin(RDD::CommandBufferID p_command_buffer) {
	if (!enabled || !slots[recording].counting) {
		return;
	}
	Slot &slot = slots[recording];
	slot.counts.draws++;
	if (slot.query_shaders.size() == QUERIES_PER_FRAME) {
		dropped_queries++;
		return;
	}
	open_query = slot.query_shaders.size();
	slot.query_shaders.push_back(open_shader);
	if (!slot.passes.is_empty()) {
		slot.passes[slot.passes.size() - 1].draws++;
	}
	driver->command_begin_pipeline_statistics_query(p_command_buffer, slot.pool, open_query);
}

void RenderingDeviceWorkCounts::draw_end(RDD::CommandBufferID p_command_buffer) {
	if (open_query == UINT32_MAX) {
		return;
	}
	driver->command_end_pipeline_statistics_query(p_command_buffer, slots[recording].pool, open_query);
	open_query = UINT32_MAX;
}

void RenderingDeviceWorkCounts::report() {
	if (!enabled || frames == 0) {
		return;
	}
	printf("[perf] gpu run-end: %llu presents, pipeline statistics queries per draw and counts recorded per present\n", (unsigned long long)frames);
	statistics[STATISTIC_FRAGMENT_INVOCATIONS].print("gpu fragment invocations", "px", "present");
	statistics[STATISTIC_VERTEX_INVOCATIONS].print("gpu vertex invocations", "vertices", "present");
	statistics[STATISTIC_CLIPPING_INVOCATIONS].print("gpu clipping invocations", "primitives", "present");
	statistics[STATISTIC_CLIPPING_PRIMITIVES].print("gpu clipping primitives", "primitives", "present");
	draws.print("gpu draws", "draws", "present");
	pipeline_binds.print("gpu pipeline binds", "binds", "present");
	render_passes.print("gpu render passes", "passes", "present");
	screen_passes.print("gpu screen-size passes", "passes", "present");
	pass_bytes_loaded.print("gpu pass bytes loaded", "bytes", "present");
	pass_bytes_stored.print("gpu pass bytes stored", "bytes", "present");
	copy_bytes.print("gpu copy bytes", "bytes", "present");
	upload_bytes.print("gpu upload bytes", "bytes", "present");
	for (const Shader *shader : shaders) {
		if (shader->draws.total == 0) {
			continue;
		}
		const CharString name = shader->name.utf8();
		char line[320];
		snprintf(line, sizeof(line), "gpu shader %s fragments", name.get_data());
		shader->fragments.print(line, "px", "present");
		snprintf(line, sizeof(line), "gpu shader %s draws", name.get_data());
		shader->draws.print(line, "draws", "present");
	}
	printf("%s", last_passes.utf8().get_data());
	if (dropped_queries > 0) {
		printf("[perf] gpu: %llu draws over %u a present went unqueried\n", (unsigned long long)dropped_queries, QUERIES_PER_FRAME);
	}
	fflush(stdout);
}

#endif // GODOT_USE_PERF_UNITS
