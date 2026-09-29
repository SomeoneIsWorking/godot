/**************************************************************************/
/*  control_channel.cpp                                                   */
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

#include "control_channel.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "scene/main/scene_tree.h"

// How long a held game sleeps between polls of the channel.
static constexpr uint64_t HOLD_POLL_USEC = 1000;

int ControlChannel::configure(bool &r_wait_for_client) {
	GLOBAL_DEF("debug/control_channel/enabled", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "debug/control_channel/port", PROPERTY_HINT_RANGE, "0,65535,1"), DEFAULT_PORT);
	GLOBAL_DEF("debug/control_channel/wait_for_client", false);

	int port = GLOBAL_GET("debug/control_channel/enabled") ? int(GLOBAL_GET("debug/control_channel/port")) : -1;
	r_wait_for_client = GLOBAL_GET("debug/control_channel/wait_for_client");

	if (port_override >= 0) {
		port = port_override;
	}
	r_wait_for_client = r_wait_for_client || wait_override;
	return port;
}

void ControlChannel::start_deferred(int p_port, bool p_wait_for_client) {
	callable_mp(this, &ControlChannel::_start).call_deferred(p_port, p_wait_for_client);
}

void ControlChannel::_start(int p_port, bool p_wait_for_client) {
	tree = SceneTree::get_singleton();
	ERR_FAIL_NULL_MSG(tree, "The control channel needs a SceneTree main loop.");
	tree_id = tree->get_instance_id();

	const Error err = transport.listen(p_port);
	if (err != OK) {
		// The channel was requested explicitly; a driver waiting on it would hang, so don't run without it.
		ERR_PRINT(vformat("Control channel could not listen on 127.0.0.1:%d (%s); quitting.", p_port, error_names[err]));
		tree->quit(EXIT_FAILURE);
		return;
	}
	// Test drivers read the port from this line when they asked for port 0.
	print_line(vformat("Control channel listening on 127.0.0.1:%d", transport.get_local_port()));

	tree->connect(SNAME("process_frame"), callable_mp(this, &ControlChannel::_on_process_frame));
	if (p_wait_for_client) {
		run_state = RunState::PAUSED;
	}
}

void ControlChannel::stop() {
	// The main loop is usually gone by the time the module shuts down.
	if (tree && ObjectDB::get_instance(tree_id) == tree) {
		tree->disconnect(SNAME("process_frame"), callable_mp(this, &ControlChannel::_on_process_frame));
	}
	tree = nullptr;
	transport.stop();
	run_state = RunState::RUNNING;
}

ControlChannel::~ControlChannel() {
	transport.stop();
}

void ControlChannel::_on_process_frame() {
	if (run_state == RunState::STEPPING) {
		steps_remaining--;
		if (steps_remaining <= 0) {
			run_state = RunState::PAUSED;
		}
	}
	_service();
	if (run_state == RunState::PAUSED) {
		_hold();
	}
}

void ControlChannel::_hold() {
	_answer_held_reply();
	while (run_state == RunState::PAUSED) {
		OS::get_singleton()->delay_usec(HOLD_POLL_USEC);
		_service();
		if (had_client && transport.get_peer_count() == 0) {
			WARN_PRINT("Control channel: the last client disconnected while the game was paused; resuming.");
			run_state = RunState::RUNNING;
		}
	}
}

void ControlChannel::_service() {
	LocalVector<ControlChannelTransport::Message> messages;
	transport.poll(messages);
	if (transport.get_peer_count() > 0) {
		had_client = true;
	}
	for (const ControlChannelTransport::Message &message : messages) {
		_dispatch(message);
	}
}

void ControlChannel::_dispatch(const ControlChannelTransport::Message &p_message) {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_message.text) != OK) {
		_reply(p_message.peer_id, Variant(), ControlChannelCommands::Result::error(ControlChannelCommands::PARSE_ERROR, vformat("Line %d: %s", json->get_error_line(), json->get_error_message())));
		return;
	}
	const Variant request = json->get_data();
	if (request.get_type() != Variant::DICTIONARY) {
		_reply(p_message.peer_id, Variant(), ControlChannelCommands::Result::error(ControlChannelCommands::INVALID_REQUEST, "A request must be a JSON object."));
		return;
	}
	const Dictionary dict = request;
	const Variant id = dict.get("id", Variant());
	const Variant method = dict.get("method", Variant());
	const Variant params = dict.get("params", Dictionary());
	if (dict.get("jsonrpc", Variant()) != Variant("2.0") || method.get_type() != Variant::STRING) {
		_reply(p_message.peer_id, id, ControlChannelCommands::Result::error(ControlChannelCommands::INVALID_REQUEST, "Expected {\"jsonrpc\": \"2.0\", \"method\": <string>, ...}."));
		return;
	}
	if (params.get_type() != Variant::DICTIONARY) {
		_reply(p_message.peer_id, id, ControlChannelCommands::Result::error(ControlChannelCommands::INVALID_PARAMS, "\"params\" must be an object."));
		return;
	}
	bool deferred = false;
	const ControlChannelCommands::Result result = _run(method, params, p_message.peer_id, id, deferred);
	if (!deferred) {
		_reply(p_message.peer_id, id, result);
	}
}

ControlChannelCommands::Result ControlChannel::_run(const String &p_method, const Dictionary &p_params, int p_peer_id, const Variant &p_id, bool &r_deferred) {
	using Result = ControlChannelCommands::Result;
	const ControlChannelCommands commands(tree);

	if (p_method == "state") {
		return Result::ok(_state());
	}
	if (p_method == "pause" || p_method == "step") {
		int frames = 0;
		if (p_method == "step") {
			const Variant frames_value = p_params.get("frames", 1);
			if ((frames_value.get_type() != Variant::INT && frames_value.get_type() != Variant::FLOAT) || int(frames_value) < 1) {
				return Result::error(ControlChannelCommands::INVALID_PARAMS, "\"frames\" must be a positive integer.");
			}
			frames = frames_value;
		}
		if (held_reply.active) {
			return Result::error(ControlChannelCommands::INVALID_REQUEST, "Another pause or step request is still in progress.");
		}
		run_state = frames > 0 ? RunState::STEPPING : RunState::PAUSED;
		steps_remaining = frames;
		held_reply = { p_peer_id, p_id, true };
		r_deferred = true;
		return Result::ok();
	}
	if (p_method == "resume") {
		run_state = RunState::RUNNING;
		return Result::ok(_state());
	}
	if (p_method == "quit") {
		const Variant code = p_params.get("code", 0);
		if (code.get_type() != Variant::INT && code.get_type() != Variant::FLOAT) {
			return Result::error(ControlChannelCommands::INVALID_PARAMS, "\"code\" must be an integer.");
		}
		tree->quit(code);
		run_state = RunState::RUNNING;
		return Result::ok();
	}
	if (p_method == "tree") {
		return commands.tree_describe(p_params);
	}
	if (p_method == "get") {
		return commands.node_get(p_params);
	}
	if (p_method == "set") {
		return commands.node_set(p_params);
	}
	if (p_method == "call") {
		return commands.node_call(p_params);
	}
	if (p_method == "action") {
		return commands.input_action(p_params);
	}
	if (p_method == "input") {
		return commands.input_event(p_params);
	}
	if (p_method == "capture") {
		return commands.capture(p_params);
	}
	return Result::error(ControlChannelCommands::METHOD_NOT_FOUND, vformat("Unknown method \"%s\".", p_method));
}

Dictionary ControlChannel::_state() const {
	Dictionary state;
	state["frame"] = int64_t(Engine::get_singleton()->get_process_frames());
	state["physics_frame"] = int64_t(Engine::get_singleton()->get_physics_frames());
	state["paused"] = run_state == RunState::PAUSED;
	return state;
}

void ControlChannel::_answer_held_reply() {
	if (!held_reply.active) {
		return;
	}
	held_reply.active = false;
	_reply(held_reply.peer_id, held_reply.request_id, ControlChannelCommands::Result::ok(_state()));
}

void ControlChannel::_reply(int p_peer_id, const Variant &p_id, const ControlChannelCommands::Result &p_result) {
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;
	if (p_result.is_error()) {
		Dictionary error;
		error["code"] = p_result.error_code;
		error["message"] = p_result.error_message;
		response["error"] = error;
	} else {
		response["result"] = p_result.value;
	}
	transport.send(p_peer_id, JSON::stringify(response, "", false));
}
