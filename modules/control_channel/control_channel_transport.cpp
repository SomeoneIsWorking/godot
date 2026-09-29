/**************************************************************************/
/*  control_channel_transport.cpp                                         */
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

#include "control_channel_transport.h"

Error ControlChannelTransport::listen(uint16_t p_port) {
	ERR_FAIL_COND_V_MSG(server.is_valid() && server->is_listening(), ERR_ALREADY_IN_USE, "The control channel is already listening.");
	server.instantiate();
	// Loopback only: the channel can call arbitrary methods, so it must never be reachable from the network.
	const Error err = server->listen(p_port, IPAddress("127.0.0.1"));
	if (err != OK) {
		server.unref();
	}
	return err;
}

void ControlChannelTransport::stop() {
	for (KeyValue<int, Peer> &E : peers) {
		E.value.stream->disconnect_from_host();
	}
	peers.clear();
	if (server.is_valid()) {
		server->stop();
		server.unref();
	}
}

bool ControlChannelTransport::is_listening() const {
	return server.is_valid() && server->is_listening();
}

uint16_t ControlChannelTransport::get_local_port() const {
	return is_listening() ? server->get_local_port() : 0;
}

bool ControlChannelTransport::_read_lines(int p_peer_id, Peer &p_peer, LocalVector<Message> &r_messages) {
	p_peer.stream->poll();
	if (p_peer.stream->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		return false;
	}
	const int available = p_peer.stream->get_available_bytes();
	if (available > 0) {
		const uint32_t start = p_peer.pending.size();
		p_peer.pending.resize(start + available);
		int received = 0;
		const Error err = p_peer.stream->get_partial_data(p_peer.pending.ptr() + start, available, received);
		p_peer.pending.resize(start + received);
		if (err != OK) {
			return false;
		}
	}

	uint32_t line_start = 0;
	for (uint32_t i = 0; i < p_peer.pending.size(); i++) {
		if (p_peer.pending[i] != '\n') {
			continue;
		}
		Message message;
		message.peer_id = p_peer_id;
		message.text = String::utf8((const char *)p_peer.pending.ptr() + line_start, i - line_start).strip_edges();
		if (!message.text.is_empty()) {
			r_messages.push_back(message);
		}
		line_start = i + 1;
	}
	if (line_start > 0) {
		const uint32_t remaining = p_peer.pending.size() - line_start;
		memmove(p_peer.pending.ptr(), p_peer.pending.ptr() + line_start, remaining);
		p_peer.pending.resize(remaining);
	}
	ERR_FAIL_COND_V_MSG(p_peer.pending.size() > uint32_t(MAX_LINE_BYTES), false, vformat("Control channel peer %d sent a line over %d bytes; closing the connection.", p_peer_id, MAX_LINE_BYTES));
	return true;
}

void ControlChannelTransport::poll(LocalVector<Message> &r_messages) {
	if (!is_listening()) {
		return;
	}
	while (server->is_connection_available()) {
		Ref<StreamPeerTCP> stream = server->take_connection();
		if (stream.is_valid()) {
			stream->set_no_delay(true);
			peers[next_peer_id++].stream = stream;
		}
	}

	LocalVector<int> closed;
	for (KeyValue<int, Peer> &E : peers) {
		if (!_read_lines(E.key, E.value, r_messages)) {
			closed.push_back(E.key);
		}
	}
	for (int peer_id : closed) {
		peers[peer_id].stream->disconnect_from_host();
		peers.erase(peer_id);
	}
}

void ControlChannelTransport::send(int p_peer_id, const String &p_line) {
	Peer *peer = peers.getptr(p_peer_id);
	if (!peer) {
		return;
	}
	const CharString utf8 = (p_line + "\n").utf8();
	const Error err = peer->stream->put_data((const uint8_t *)utf8.get_data(), utf8.length());
	if (err != OK) {
		peer->stream->disconnect_from_host();
		peers.erase(p_peer_id);
	}
}
