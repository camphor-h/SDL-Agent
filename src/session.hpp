// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sdlagent
{

enum class Role
{
    System,
    User,
    Assistant,
    Tool
};

const char* roleToString(Role r);
Role        roleFromString(const std::string& s);

//A file supplied by the user with a message.
//`path` is relative to the sessions root: attachments/<sessionId>/<file>.
struct Attachment
{
    std::string kind; //"text" | "image"
    std::string name;
    std::string path;
    std::string mime;
    //Inline base64 for images read by tools. When set, `path` is empty.
    std::string data;
    uint64_t    size = 0;
};

struct Message
{
    Role        role = Role::User;
    std::string content;

    //assistant only
    std::string reasoning;
    std::string toolCalls; //serialized OpenAI tool_calls array

    //assistant only: token usage reported by the provider for this step
    int promptTokens = 0;
    int completionTokens = 0;

    //tool result only
    std::string toolCallId;
    std::string toolName;

    //files supplied together with a user message
    std::vector<Attachment> attachments;

    uint64_t timestamp = 0;

    //UI-only flags, never persisted
    bool streaming = false;
    bool error     = false;
    bool reasoningExpanded = false;
    bool reasoningExplicit = false;

    bool isEmpty() const
    {
        return content.empty() && reasoning.empty() && toolCalls.empty() &&
               attachments.empty();
    }
};

struct Session
{
    std::string id;
    std::string title;
    bool        pinned = false;
    uint64_t    createdAt = 0;
    uint64_t    updatedAt = 0;

    //model used for the latest request (informational)
    std::string model;

    std::vector<Message> messages;

    //Runtime-only state. `messages` is only populated for loaded sessions;
    //the rest keep metadata so the sidebar can render without reading the
    //whole history.
    bool loaded = false;
    bool hasMessages = false;

    std::string preview() const;
};

Json    toJson(const Session& s);
Session sessionFromJson(const Json& j);

} //namespace sdlagent
