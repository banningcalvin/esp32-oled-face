#pragma once

#include <stdint.h>

// Shared by every face's updateAndDraw().
//   Sleeping: unread == 0. Eyes closed, slow breathing, "z" animation.
//   Awake:    unread > 0. Natural idle animation.
//   Talking:  a notification is being announced. Energetic animation.
enum class FaceState : uint8_t { Sleeping, Awake, Talking };
