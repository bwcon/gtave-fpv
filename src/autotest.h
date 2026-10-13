// Unattended test scenario, see autotest.cpp.
#pragma once

namespace autotest {

// Once per script tick, before drone::tick().
void tick();

// Stops a running scenario and releases its targets.
void abort();

}  // namespace autotest
