#pragma once

#if defined(TESTING)
#include "rots_net.h"
#include <string_view>

struct descriptor_data;

namespace comm_testing {
// Substitutes one synchronous socket operation; callbacks keep the platform last-error contract.
using socket_read_fn = rots_net::ssize_type (*)(SocketType, void*, size_t);
using socket_write_fn = rots_net::ssize_type (*)(SocketType, const void*, size_t);
// Installs a borrowed callback and returns the previous callback for scoped restoration.
socket_read_fn set_socket_read_override(socket_read_fn callback);
socket_write_fn set_socket_write_override(socket_write_fn callback);
// Requires a non-null initialized descriptor. Flush returns >0 after consuming
// queued output, <0 on fatal failure, or 0 for absent/deferred output.
// Prompt writers honor game/editor eligibility and mark bare output only after
// a nonempty prompt succeeds on a valid nonzero socket.
int flush_pending_output(descriptor_data* descriptor, bool writable);
void write_prompt(descriptor_data* descriptor);
void write_bare_prompt(descriptor_data* descriptor, std::string_view prompt);
}
#endif
