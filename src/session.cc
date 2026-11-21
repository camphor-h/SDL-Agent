// SPDX-License-Identifier: GPL-3.0-only

#include "session.hpp"

#include "json.hpp"

namespace sdlagent
{

const char* roleToString(Role r)
{
    switch (r)
    {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "user";
}

Role roleFromString(const std::string& s)
{
    if (s == "system")    return Role::System;
    if (s == "assistant") return Role::Assistant;
    if (s == "tool")      return Role::Tool;
    return Role::User;
}

std::string Session::preview() const
{
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
    {
        if (!it->content.empty() && it->role != Role::System)
            return it->content;
    }
    return {};
}

Json toJson(const Session& s)
{
    Json j = Json::object();
    j.set("version", 1)
        .set("id", s.id)
        .set("title", s.title)
        .set("pinned", s.pinned)
        .set("createdAt", (long long)s.createdAt)
        .set("updatedAt", (long long)s.updatedAt);
    if (!s.model.empty())
        j.set("model", s.model);

    Json msgs = Json::array();
    for (const Message& m : s.messages)
    {
        //transient streaming placeholder is not worth persisting
        if (m.streaming)
            continue;

        Json jm = Json::object();
        jm.set("role", roleToString(m.role))
          .set("content", m.content)
          .set("timestamp", (long long)m.timestamp);

        if (!m.reasoning.empty())
            jm.set("reasoning", m.reasoning);
        if (!m.toolCalls.empty() && m.toolCalls != "[]")
            jm.set("toolCalls", m.toolCalls);
        if (!m.toolCallId.empty())
            jm.set("toolCallId", m.toolCallId);
        if (!m.toolName.empty())
            jm.set("toolName", m.toolName);
        if (m.error)
            jm.set("error", true);

        if (!m.attachments.empty())
        {
            Json atts = Json::array();
            for (const Attachment& a : m.attachments)
            {
                Json ja = Json::object();
                ja.set("kind", a.kind)
                  .set("name", a.name)
                  .set("path", a.path)
                  .set("mime", a.mime)
                  .set("size", (long long)a.size);
                if (!a.data.empty())
                    ja.set("data", a.data);
                atts.push(std::move(ja));
            }
            jm.set("attachments", std::move(atts));
        }

        msgs.push(std::move(jm));
    }
    j.set("messages", std::move(msgs));
    return j;
}

Session sessionFromJson(const Json& j)
{
    Session s;
    if (!j.isObject())
        return s;

    s.id = j["id"].str();
    s.title = j["title"].str();
    s.pinned = j["pinned"].boolean(false);
    s.createdAt = (uint64_t)j["createdAt"].integer(0);
    s.updatedAt = (uint64_t)j["updatedAt"].integer(0);
    s.model = j["model"].str();

    Json msgs = j["messages"];
    if (msgs.isArray())
    {
        for (std::size_t i = 0; i < msgs.size(); ++i)
        {
            Json jm = msgs[i];
            if (!jm.isObject())
                continue;

            Message m;
            m.role = roleFromString(jm["role"].str("user"));
            m.content = jm["content"].str();
            m.reasoning = jm["reasoning"].str();
            m.toolCalls = jm["toolCalls"].str();
            if (m.toolCalls == "[]")
                m.toolCalls.clear();
            m.toolCallId = jm["toolCallId"].str();
            m.toolName = jm["toolName"].str();
            m.timestamp = (uint64_t)jm["timestamp"].integer(0);
            m.error = jm["error"].boolean(false);

            Json atts = jm["attachments"];
            if (atts.isArray())
            {
                for (std::size_t k = 0; k < atts.size(); ++k)
                {
                    Json ja = atts[k];
                    if (!ja.isObject())
                        continue;
                    Attachment a;
                    a.kind = ja["kind"].str();
                    a.name = ja["name"].str();
                    a.path = ja["path"].str();
                    a.mime = ja["mime"].str();
                    a.size = (uint64_t)ja["size"].integer(0);
                    a.data = ja["data"].str();
                    if (a.kind.empty() || (a.path.empty() && a.data.empty()))
                        continue;
                    if (a.name.empty())
                        a.name = a.path.empty() ? a.kind : a.path;
                    m.attachments.push_back(std::move(a));
                }
            }

            s.messages.push_back(std::move(m));
        }
    }

    if (s.id.empty())
        s.id = "imported";
    if (s.createdAt == 0)
        s.createdAt = s.updatedAt;
    return s;
}

} //namespace sdlagent
