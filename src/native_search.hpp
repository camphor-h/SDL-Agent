// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace sdlagent::native_search
{

//How the active profile enables the provider's own web search.
struct Plan
{
    enum class Mode
    {
        None,       //model has no built-in web search
        RawTool,    //append toolJson to the request's tools array
        ChatOptions //top-level "web_search_options" (OpenAI search models)
    };

    Mode        mode = Mode::None;
    std::string toolJson; //RawTool only
};

bool supported(const Plan& plan);

//Built-in capability table: base URL host + model family + wire protocol.
//No network probing; proxies and unknown hosts intentionally resolve to None.
Plan resolve(const std::string& baseUrl, const std::string& model,
             const std::string& protocol);

} //namespace sdlagent::native_search
