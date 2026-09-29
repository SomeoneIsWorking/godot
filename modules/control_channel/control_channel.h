/**************************************************************************/
/*  control_channel.h                                                     */
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

#include "control_channel_commands.h"
#include "control_channel_transport.h"

#include "core/object/object.h"

class SceneTree;

// Opt-in loopback endpoint that lets a test driver control a running game:
// JSON-RPC 2.0 requests, one per line, over TCP on 127.0.0.1.
//
// The channel is serviced once per frame, when the SceneTree emits
// `process_frame`. While paused, the game is held at that point and only the
// channel runs, so `step` advances an exact number of frames and injected
// input is delivered on the next frame, like device input.
class ControlChannel : public Object {
	GDSOFTCLASS(ControlChannel, Object);

public:
	enum class RunState {
		RUNNING,
		PAUSED,
		STEPPING,
	};

	static constexpr int DEFAULT_PORT = 6070;

private:
	struct PendingReply {
		int peer_id = 0;
		Variant request_id;
		bool active = false;
	};

	ControlChannelTransport transport;
	RunState run_state = RunState::RUNNING;
	int steps_remaining = 0;
	// `pause` and `step` answer once the game is actually held.
	PendingReply held_reply;
	bool had_client = false;
	SceneTree *tree = nullptr;
	ObjectID tree_id;

	void _start(int p_port, bool p_wait_for_client);
	void _on_process_frame();
	void _service();
	void _hold();
	void _dispatch(const ControlChannelTransport::Message &p_message);
	ControlChannelCommands::Result _run(const String &p_method, const Dictionary &p_params, int p_peer_id, const Variant &p_id, bool &r_deferred);
	Dictionary _state() const;
	void _reply(int p_peer_id, const Variant &p_id, const ControlChannelCommands::Result &p_result);
	void _answer_held_reply();

public:
	// Set from the command line (`--control-port <port>`, `--control-wait`); they override the project settings.
	static inline int port_override = -1;
	static inline bool wait_override = false;

	// Registers the project settings. Returns the port to listen on, or -1 when the channel is disabled.
	static int configure(bool &r_wait_for_client);

	RunState get_run_state() const { return run_state; }
	uint16_t get_port() const { return transport.get_local_port(); }

	// Starts listening once the main loop exists.
	void start_deferred(int p_port, bool p_wait_for_client);
	void stop();

	~ControlChannel();
};
