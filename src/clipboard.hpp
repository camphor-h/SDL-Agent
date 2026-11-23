// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <vector>

namespace sdlagent::clipboard
{

//Absolute paths of files currently on the clipboard. Empty when the clipboard
//holds text, an image, or nothing usable.
std::vector<std::string> files();

} //namespace sdlagent::clipboard
