// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sdlagent::filepick
{

//Android: the system document picker returns content:// URIs. Java copies the
//selected files to the app cache and reports real paths through this module.
void setEventType(std::uint32_t type);
void deliver(std::vector<std::string> paths);
bool take(std::vector<std::string>& paths);

#if defined(__ANDROID__)
bool available();
void open();
#endif

} //namespace sdlagent::filepick
