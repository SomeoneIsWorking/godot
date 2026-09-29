/**************************************************************************/
/*  audio_driver_dummy.h                                                  */
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

#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "servers/audio/audio_server.h"

class AudioDriverDummy : public AudioDriver {
	Thread thread;
	Mutex mutex;

	int32_t *samples_in = nullptr;

	static void thread_func(void *p_udata);

	uint32_t buffer_frames = 4096;
	int32_t mix_rate = -1;
	SpeakerMode speaker_mode = SPEAKER_MODE_STEREO;

	int channels;

	SafeFlag active;
	SafeFlag exit_thread;

	bool use_threads = true;

	// Frame clock: no thread runs and the main loop mixes exactly the audio that
	// elapsed during each frame, so playback cursors follow the game's frame time.
	bool frame_clocked = false;
	double frame_clock_carry = 0.0;
	int last_mix_frames = 0;
	uint64_t mixed_frames = 0;

	static AudioDriverDummy *singleton;

	void _mix(uint32_t p_frames, int32_t *p_buffer);

public:
	virtual const char *get_name() const override {
		return "Dummy";
	}

	virtual Error init() override;
	virtual void start() override;
	virtual int get_mix_rate() const override;
	virtual SpeakerMode get_speaker_mode() const override;

	virtual void lock() override;
	virtual void unlock() override;
	virtual void finish() override;

	void set_use_threads(bool p_use_threads);

	// Must be set before init(). Implies no threads.
	void set_frame_clocked(bool p_frame_clocked);
	bool is_frame_clocked() const { return frame_clocked; }
	// Mixes the audio that plays during `p_seconds` of frame time (frame-clocked mode only).
	void advance(double p_seconds);
	// Frames mixed by the main loop (frame clock or movie writer).
	uint64_t get_mixed_frames() const { return mixed_frames; }

	// Whole frames to mix for `p_seconds` at `p_mix_rate`; the fraction left over is kept in `r_carry`
	// so that no audio is lost or duplicated across calls.
	static uint32_t frames_for_duration(double p_seconds, int p_mix_rate, double &r_carry);

	// In frame-clocked mode, the latest mix covers the current frame, so these are expressed in frame time.
	virtual float get_latency() override;
	virtual double get_time_since_last_mix() override;
	virtual double get_time_to_next_mix() override;
	void set_speaker_mode(SpeakerMode p_mode);
	void set_mix_rate(int p_rate);

	uint32_t get_channels() const;

	void mix_audio(int p_frames, int32_t *p_buffer);

	static AudioDriverDummy *get_dummy_singleton() { return singleton; }

	AudioDriverDummy();
	~AudioDriverDummy() {}
};
