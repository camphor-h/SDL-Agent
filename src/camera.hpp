// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <string>

namespace sdlagent::camera
{

//SDL user event type used to deliver the result to the UI thread. 0 = none.
void setEventType(std::uint32_t type);

//Whether the platform supports taking a photo.
bool available();

//Launch the system camera. The result arrives asynchronously through the
//registered SDL event; call takeResult() from that event.
void capture();

//Returns true and fills `path` when a photo was captured.
bool takeResult(std::string& path);

//Used by the JNI callback.
void deliverResult(bool ok, const std::string& path);

} //namespace sdlagent::camera
