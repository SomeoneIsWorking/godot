/**************************************************************************/
/*  control_channel_commands.cpp                                          */
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

#include "control_channel_commands.h"

#include "core/crypto/crypto_core.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/input/input_map.h"
#include "core/io/image.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/texture.h"

namespace {

bool _get_string(const Dictionary &p_params, const String &p_key, String &r_value) {
	const Variant value = p_params.get(p_key, Variant());
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME && value.get_type() != Variant::NODE_PATH) {
		return false;
	}
	r_value = value;
	return true;
}

// Key properties also accept key names such as "Space" or "A".
Variant _event_property_value(const StringName &p_property, const Variant &p_value) {
	if (p_value.get_type() == Variant::STRING && (p_property == SNAME("keycode") || p_property == SNAME("physical_keycode") || p_property == SNAME("key_label"))) {
		return int64_t(find_keycode(String(p_value)));
	}
	return JSON::to_native(p_value);
}

} // namespace

ControlChannelCommands::ControlChannelCommands(SceneTree *p_tree) :
		tree(p_tree) {
}

ControlChannelCommands::Result ControlChannelCommands::_find_node(const Dictionary &p_params, Node *&r_node) const {
	String path;
	if (!_get_string(p_params, "path", path)) {
		return Result::error(INVALID_PARAMS, "\"path\" must be a string.");
	}
	r_node = tree->get_root()->get_node_or_null(NodePath(path));
	if (!r_node) {
		return Result::error(NODE_NOT_FOUND, vformat("No node at \"%s\".", path));
	}
	return Result::ok();
}

Dictionary ControlChannelCommands::_describe_node(const Node *p_node, int p_depth) const {
	Dictionary description;
	description["name"] = String(p_node->get_name());
	description["class"] = p_node->get_class();
	description["path"] = String(p_node->get_path());
	description["child_count"] = p_node->get_child_count();
	if (p_depth > 0) {
		Array children;
		for (int i = 0; i < p_node->get_child_count(); i++) {
			children.push_back(_describe_node(p_node->get_child(i), p_depth - 1));
		}
		description["children"] = children;
	}
	return description;
}

ControlChannelCommands::Result ControlChannelCommands::tree_describe(const Dictionary &p_params) const {
	Dictionary params = p_params.duplicate();
	if (!params.has("path")) {
		params["path"] = "/root";
	}
	Node *node = nullptr;
	const Result found = _find_node(params, node);
	if (found.is_error()) {
		return found;
	}
	const Variant depth = params.get("depth", 1);
	if (depth.get_type() != Variant::INT && depth.get_type() != Variant::FLOAT) {
		return Result::error(INVALID_PARAMS, "\"depth\" must be a number.");
	}
	const int depth_value = int(depth);
	if (depth_value < 0 || depth_value > MAX_TREE_DEPTH) {
		return Result::error(INVALID_PARAMS, vformat("\"depth\" must be between 0 and %d.", MAX_TREE_DEPTH));
	}
	return Result::ok(_describe_node(node, depth_value));
}

ControlChannelCommands::Result ControlChannelCommands::node_get(const Dictionary &p_params) const {
	Node *node = nullptr;
	const Result found = _find_node(p_params, node);
	if (found.is_error()) {
		return found;
	}
	String property;
	if (!_get_string(p_params, "property", property)) {
		return Result::error(INVALID_PARAMS, "\"property\" must be a string.");
	}
	bool valid = false;
	const Variant value = node->get_indexed(NodePath(property).get_as_property_path().get_subnames(), &valid);
	if (!valid) {
		return Result::error(INVALID_PARAMS, vformat("%s has no property \"%s\".", node->get_class(), property));
	}
	return Result::ok(JSON::from_native(value));
}

ControlChannelCommands::Result ControlChannelCommands::node_set(const Dictionary &p_params) const {
	Node *node = nullptr;
	const Result found = _find_node(p_params, node);
	if (found.is_error()) {
		return found;
	}
	String property;
	if (!_get_string(p_params, "property", property)) {
		return Result::error(INVALID_PARAMS, "\"property\" must be a string.");
	}
	if (!p_params.has("value")) {
		return Result::error(INVALID_PARAMS, "\"value\" is required.");
	}
	bool valid = false;
	node->set_indexed(NodePath(property).get_as_property_path().get_subnames(), JSON::to_native(p_params["value"]), &valid);
	if (!valid) {
		return Result::error(INVALID_PARAMS, vformat("Can't set \"%s\" on %s.", property, node->get_class()));
	}
	return Result::ok();
}

ControlChannelCommands::Result ControlChannelCommands::node_call(const Dictionary &p_params) const {
	Node *node = nullptr;
	const Result found = _find_node(p_params, node);
	if (found.is_error()) {
		return found;
	}
	String method;
	if (!_get_string(p_params, "method", method)) {
		return Result::error(INVALID_PARAMS, "\"method\" must be a string.");
	}
	const Variant args_value = p_params.get("args", Array());
	if (args_value.get_type() != Variant::ARRAY) {
		return Result::error(INVALID_PARAMS, "\"args\" must be an array.");
	}
	const Array json_args = args_value;
	Vector<Variant> args;
	for (int i = 0; i < json_args.size(); i++) {
		args.push_back(JSON::to_native(json_args[i]));
	}
	Vector<const Variant *> arg_pointers;
	for (const Variant &arg : args) {
		arg_pointers.push_back(&arg);
	}
	Callable::CallError call_error;
	const Variant ret = node->callp(method, arg_pointers.ptrw(), arg_pointers.size(), call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		return Result::error(CALL_FAILED, Variant::get_call_error_text(node, method, arg_pointers.ptrw(), arg_pointers.size(), call_error));
	}
	return Result::ok(JSON::from_native(ret));
}

ControlChannelCommands::Result ControlChannelCommands::input_action(const Dictionary &p_params) const {
	String action;
	if (!_get_string(p_params, "action", action)) {
		return Result::error(INVALID_PARAMS, "\"action\" must be a string.");
	}
	if (!InputMap::get_singleton()->has_action(action)) {
		return Result::error(INVALID_PARAMS, vformat("No input action \"%s\".", action));
	}
	Ref<InputEventAction> event;
	event.instantiate();
	event->set_action(action);
	event->set_pressed(p_params.get("pressed", true));
	event->set_strength(p_params.get("strength", 1.0));
	Input::get_singleton()->parse_input_event(event);
	return Result::ok();
}

ControlChannelCommands::Result ControlChannelCommands::input_event(const Dictionary &p_params) const {
	String type;
	if (!_get_string(p_params, "type", type)) {
		return Result::error(INVALID_PARAMS, "\"type\" must be a string.");
	}
	// Only concrete InputEvent classes may be created, so the channel can't instantiate arbitrary objects.
	if (!ClassDB::class_exists(type) || !ClassDB::is_parent_class(type, "InputEvent") || !ClassDB::can_instantiate(type)) {
		return Result::error(INVALID_PARAMS, vformat("\"%s\" is not an instantiable InputEvent class.", type));
	}
	const Variant properties_value = p_params.get("properties", Dictionary());
	if (properties_value.get_type() != Variant::DICTIONARY) {
		return Result::error(INVALID_PARAMS, "\"properties\" must be an object.");
	}
	Ref<InputEvent> event = Object::cast_to<InputEvent>(ClassDB::instantiate(type));
	ERR_FAIL_COND_V(event.is_null(), Result::error(CALL_FAILED, vformat("Could not create %s.", type)));
	const Dictionary properties = properties_value;
	for (const KeyValue<Variant, Variant> &E : properties) {
		const StringName property = E.key;
		bool valid = false;
		event->set(property, _event_property_value(property, E.value), &valid);
		if (!valid) {
			return Result::error(INVALID_PARAMS, vformat("%s has no property \"%s\".", type, property));
		}
	}
	Input::get_singleton()->parse_input_event(event);
	return Result::ok();
}

ControlChannelCommands::Result ControlChannelCommands::capture(const Dictionary &p_params) const {
	Ref<ViewportTexture> texture = tree->get_root()->get_texture();
	ERR_FAIL_COND_V(texture.is_null(), Result::error(NOT_AVAILABLE, "The main viewport has no texture."));
	Ref<Image> image = texture->get_image();
	if (image.is_null() || image->is_empty()) {
		return Result::error(NOT_AVAILABLE, "No frame has been rendered; the dummy renderer can't capture frames (use --offscreen).");
	}
	Dictionary result;
	result["width"] = image->get_width();
	result["height"] = image->get_height();
	const Vector<uint8_t> png = image->save_png_to_buffer();
	result["png"] = CryptoCore::b64_encode_str(png.ptr(), png.size());
	return Result::ok(result);
}
