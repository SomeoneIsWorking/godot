/**************************************************************************/
/*  control_channel_transport.h                                           */
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

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

// Newline-delimited UTF-8 messages over loopback TCP.
// Owns the listening socket and every accepted connection; never blocks.
class ControlChannelTransport {
public:
	// Lines longer than this close the connection that sent them.
	static constexpr int MAX_LINE_BYTES = 16 * 1024 * 1024;

	struct Message {
		int peer_id = 0;
		String text;
	};

private:
	struct Peer {
		Ref<StreamPeerTCP> stream;
		LocalVector<uint8_t> pending;
	};

	Ref<TCPServer> server;
	HashMap<int, Peer> peers;
	int next_peer_id = 1;

	// Returns false when the peer must be dropped.
	bool _read_lines(int p_peer_id, Peer &p_peer, LocalVector<Message> &r_messages);

public:
	Error listen(uint16_t p_port);
	void stop();
	bool is_listening() const;
	uint16_t get_local_port() const;
	int get_peer_count() const { return peers.size(); }

	// Accepts pending connections, drops closed ones and appends every complete line received.
	void poll(LocalVector<Message> &r_messages);
	// Sends one line (a newline is appended). Unknown or closed peers are ignored.
	void send(int p_peer_id, const String &p_line);
};
