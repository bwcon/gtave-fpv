// Crash effects on what the drone hits: ragdoll and damage peds, damage and
// nudge vehicles, push props.
#pragma once

#include <libfpv.h>

namespace effects {

// Applies effects for the impacts in a fresh frame. Script thread only.
void on_frame(const fpv_frame &frame);

}  // namespace effects
