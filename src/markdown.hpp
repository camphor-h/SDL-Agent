// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "sdlgui.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sdlagent
{
struct Message;
}

namespace sdlagent::ui
{

//Markdown -> RichTextView blocks (md4c).
std::vector<sdlgui::RichTextBlock> renderMarkdown(const std::string& text);

//One chat message, including tool calls/results and reasoning.
//When toolResults is non-null, matching tool results are rendered directly
//under each tool call with a divider between them.
std::vector<sdlgui::RichTextBlock> renderMessage(
    const Message& m, int messageIndex,
    const std::vector<std::pair<int, const Message*>>* toolResults = nullptr);

} //namespace sdlagent::ui
