/**************************************************************************/
/*  test_control_channel.cpp                                              */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_control_channel)

#include "core/input/input_map.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "scene/2d/node_2d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

#include "modules/control_channel/control_channel_commands.h"
#include "modules/control_channel/control_channel_transport.h"

namespace TestControlChannel {

using Result = ControlChannelCommands::Result;

static Dictionary params(std::initializer_list<std::pair<const char *, Variant>> p_values) {
	Dictionary dict;
	for (const std::pair<const char *, Variant> &value : p_values) {
		dict[value.first] = value.second;
	}
	return dict;
}

TEST_CASE("[SceneTree][ControlChannel] Scene commands") {
	Node2D *node = memnew(Node2D);
	node->set_name("Probe");
	SceneTree::get_singleton()->get_root()->add_child(node);
	const ControlChannelCommands commands(SceneTree::get_singleton());

	SUBCASE("tree describes children") {
		const Result result = commands.tree_describe(params({ { "path", "/root" }, { "depth", 1 } }));
		REQUIRE_FALSE(result.is_error());
		const Dictionary root = result.value;
		CHECK(root["class"] == "Window");
		bool found = false;
		const Array children = root["children"];
		for (int i = 0; i < children.size(); i++) {
			const Dictionary child = children[i];
			found = found || (child["name"] == "Probe" && child["class"] == "Node2D");
		}
		CHECK(found);
	}

	SUBCASE("get and set use JSON.from_native values") {
		CHECK_FALSE(commands.node_set(params({ { "path", "/root/Probe" }, { "property", "position" }, { "value", JSON::from_native(Vector2(3, 4)) } })).is_error());
		CHECK(node->get_position() == Vector2(3, 4));
		const Result result = commands.node_get(params({ { "path", "/root/Probe" }, { "property", "position:x" } }));
		REQUIRE_FALSE(result.is_error());
		CHECK(JSON::to_native(result.value) == Variant(3.0));
	}

	SUBCASE("call returns the method's value") {
		const Result result = commands.node_call(params({ { "path", "/root/Probe" }, { "method", "get_child_count" }, { "args", Array() } }));
		REQUIRE_FALSE(result.is_error());
		CHECK(JSON::to_native(result.value) == Variant(0));
	}

	SUBCASE("errors name what is wrong") {
		CHECK(commands.node_get(params({ { "path", "/root/Missing" }, { "property", "name" } })).error_code == ControlChannelCommands::NODE_NOT_FOUND);
		CHECK(commands.node_get(params({ { "path", "/root/Probe" }, { "property", "no_such_property" } })).error_code == ControlChannelCommands::INVALID_PARAMS);
		CHECK(commands.node_get(params({ { "property", "name" } })).error_code == ControlChannelCommands::INVALID_PARAMS);
		CHECK(commands.tree_describe(params({ { "depth", ControlChannelCommands::MAX_TREE_DEPTH + 1 } })).error_code == ControlChannelCommands::INVALID_PARAMS);
		ERR_PRINT_OFF;
		const Result bad_call = commands.node_call(params({ { "path", "/root/Probe" }, { "method", "no_such_method" } }));
		ERR_PRINT_ON;
		CHECK(bad_call.error_code == ControlChannelCommands::CALL_FAILED);
	}

	memdelete(node);
}

TEST_CASE("[SceneTree][ControlChannel] Input commands") {
	const ControlChannelCommands commands(SceneTree::get_singleton());

	SUBCASE("only InputEvent classes can be created") {
		CHECK(commands.input_event(params({ { "type", "Node" } })).error_code == ControlChannelCommands::INVALID_PARAMS);
		CHECK(commands.input_event(params({ { "type", "InputEvent" } })).error_code == ControlChannelCommands::INVALID_PARAMS); // Abstract.
		CHECK(commands.input_event(params({ { "type", "InputEventKey" }, { "properties", params({ { "no_such_property", true } }) } })).error_code == ControlChannelCommands::INVALID_PARAMS);
		CHECK_FALSE(commands.input_event(params({ { "type", "InputEventKey" }, { "properties", params({ { "keycode", "Space" }, { "pressed", true } }) } })).is_error());
	}

	SUBCASE("actions must exist") {
		CHECK(commands.input_action(params({ { "action", "no_such_action" } })).error_code == ControlChannelCommands::INVALID_PARAMS);
		InputMap::get_singleton()->add_action("control_channel_test");
		CHECK_FALSE(commands.input_action(params({ { "action", "control_channel_test" } })).is_error());
		InputMap::get_singleton()->erase_action("control_channel_test");
	}
}

TEST_CASE("[ControlChannel] Transport splits newline-delimited messages") {
	ControlChannelTransport transport;
	REQUIRE(transport.listen(0) == OK);
	const uint16_t port = transport.get_local_port();
	REQUIRE(port != 0);

	Ref<StreamPeerTCP> client;
	client.instantiate();
	REQUIRE(client->connect_to_host(IPAddress("127.0.0.1"), port) == OK);

	LocalVector<ControlChannelTransport::Message> messages;
	const CharString payload = String("first\n\nsecond\npartial").utf8();
	bool sent = false;
	for (int attempt = 0; attempt < 2000 && messages.size() < 2; attempt++) {
		client->poll();
		if (!sent && client->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
			client->put_data((const uint8_t *)payload.get_data(), payload.length());
			sent = true;
		}
		transport.poll(messages);
		OS::get_singleton()->delay_usec(1000);
	}
	REQUIRE(messages.size() == 2);
	CHECK(messages[0].text == "first");
	CHECK(messages[1].text == "second");
	CHECK(messages[0].peer_id == messages[1].peer_id);

	// The unterminated line stays buffered until its newline arrives.
	client->put_data((const uint8_t *)"\n", 1);
	messages.clear();
	for (int attempt = 0; attempt < 2000 && messages.is_empty(); attempt++) {
		transport.poll(messages);
		OS::get_singleton()->delay_usec(1000);
	}
	REQUIRE(messages.size() == 1);
	CHECK(messages[0].text == "partial");

	transport.stop();
	CHECK_FALSE(transport.is_listening());
}

TEST_CASE("[ControlChannel] Transport refuses a second listen") {
	ControlChannelTransport transport;
	REQUIRE(transport.listen(0) == OK);
	ERR_PRINT_OFF;
	CHECK(transport.listen(0) == ERR_ALREADY_IN_USE);
	ERR_PRINT_ON;
	transport.stop();
}

} // namespace TestControlChannel
