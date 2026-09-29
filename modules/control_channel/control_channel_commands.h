/**************************************************************************/
/*  control_channel_commands.h                                            */
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

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class Node;
class SceneTree;

// Scene inspection and input commands of the control channel.
// Each command takes JSON-RPC params and runs on the main thread against the
// current SceneTree. Values cross the wire in `JSON.from_native()` form.
class ControlChannelCommands {
public:
	// JSON-RPC 2.0 error codes.
	enum ErrorCode {
		PARSE_ERROR = -32700,
		INVALID_REQUEST = -32600,
		METHOD_NOT_FOUND = -32601,
		INVALID_PARAMS = -32602,
		// Implementation-defined server errors.
		NODE_NOT_FOUND = -32001,
		CALL_FAILED = -32002,
		NOT_AVAILABLE = -32003,
	};

	struct Result {
		Variant value;
		int error_code = 0;
		String error_message;

		bool is_error() const { return error_code != 0; }
		static Result ok(const Variant &p_value = Variant()) { return { p_value, 0, String() }; }
		static Result error(int p_code, const String &p_message) { return { Variant(), p_code, p_message }; }
	};

private:
	SceneTree *tree = nullptr;

	Result _find_node(const Dictionary &p_params, Node *&r_node) const;
	Dictionary _describe_node(const Node *p_node, int p_depth) const;

public:
	// Nesting limit for `tree`, so a deep scene can't produce unbounded replies.
	static constexpr int MAX_TREE_DEPTH = 64;

	// {path = "/root", depth = 1} -> {name, class, path, children}
	Result tree_describe(const Dictionary &p_params) const;
	// {path, property} -> value
	Result node_get(const Dictionary &p_params) const;
	// {path, property, value} -> null
	Result node_set(const Dictionary &p_params) const;
	// {path, method, args = []} -> return value
	Result node_call(const Dictionary &p_params) const;
	// {action, pressed = true, strength = 1.0} -> null. Queued like device input.
	Result input_action(const Dictionary &p_params) const;
	// {type = "InputEventKey", properties = {...}} -> null. Queued like device input.
	Result input_event(const Dictionary &p_params) const;
	// {} -> {width, height, png = base64}. The main viewport's last rendered frame.
	Result capture(const Dictionary &p_params) const;

	explicit ControlChannelCommands(SceneTree *p_tree);
};
