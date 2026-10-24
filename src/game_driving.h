#pragma once

namespace wdc::driving {
struct Pedals { float throttle{}, brake{}; };
// SDL thread publishes normalized pedals; guest hooks only read atomics.
// Ports are zero-based. Invalid ports read neutral and ignore writes.
void publish(int port, Pedals pedals);
Pedals read(int port);
}
