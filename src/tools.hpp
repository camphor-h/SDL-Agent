// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "json.hpp"
#include "session.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sdlagent
{

struct ToolContext
{
    std::filesystem::path workspace;
    std::filesystem::path sessionsRoot;
    std::string           sessionId;
    std::atomic<bool>*    cancel = nullptr;
    bool                  imageInput = false;
    int*                  imageCount = nullptr;
    int                   maxImages = 9;
};

struct ToolResult
{
    bool        ok = true;
    std::string output;
    std::vector<Attachment> attachments;
};

struct ToolApproval
{
    std::string summary;
    std::string detail;
};

struct ToolSpec
{
    std::string name;
    std::string description;
    Json        parameters; //JSON schema object


    //reserved tools are kept in the registry but not advertised to the model
    bool available = true;

    std::function<ToolResult(const Json&, ToolContext&)> run;
    std::function<ToolApproval(const Json&)>             describe;
};

class ToolRegistry
{
public:
    void add(ToolSpec spec);

    const ToolSpec* find(const std::string& name) const;
    const std::vector<ToolSpec>& all() const { return tools_; }

    //JSON array accepted by the Chat Completions "tools" field
    std::string schemaJson() const;

private:
    std::vector<ToolSpec> tools_;
};

ToolRegistry makeBuiltinTools();

} //namespace sdlagent
