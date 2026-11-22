// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace sdlagent::ime
{

//Delivers the on-screen keyboard height from the Android UI thread to the
//SDL event loop. The value is in physical window pixels; 0 means hidden.
void setEventType(std::uint32_t type);
void deliverInset(int px);
bool takeInset(int& px);

} //namespace sdlagent::ime
