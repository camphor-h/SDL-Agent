// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "http_header.hpp"

#include <string>
#include <vector>

namespace sdlagent::provider
{

//Headers required by specific OpenAI-compatible gateways. Built-in rules can
//be overridden or extended by <dataDir>/gateways.json.
std::vector<HttpHeader> headersFor(const std::string& baseUrl,
                                   const std::string& model,
                                   const std::string& sessionId);

} //namespace sdlagent::provider
