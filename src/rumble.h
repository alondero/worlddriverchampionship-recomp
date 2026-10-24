#pragma once

namespace wdc::rumble {
// Guest threads publish motor commands; the SDL thread samples the lease.
// Port 1 only. A stalled/paused guest cannot keep a physical motor running.
void set_motor(int port, bool on);
bool active();
void stop();
}
