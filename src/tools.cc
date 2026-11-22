// SPDX-License-Identifier: GPL-3.0-only

#include "tools.hpp"

#include "attachments.hpp"
#include "i18n.hpp"
#include "paths.hpp"
#include "util.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sdlagent
{
namespace
{

constexpr size_t kMaxReadBytes   = 4 * 1024 * 1024;
//Match opencode's tool-output defaults: at most 2000 lines / 50 KiB per read.
constexpr size_t kMaxReadOutput  = 50 * 1024;
constexpr size_t kMaxWriteBytes  = 8 * 1024 * 1024;
constexpr size_t kMaxShellOutput = 50 * 1024;
constexpr int    kDefaultTimeout = 60000;

std::string argString(const Json& args, const char* key,
                      const std::string& fallback = {})
{
    return args[key].str(fallback);
}

int argInt(const Json& args, const char* key, int fallback)
{
    return (int)args[key].integer(fallback);
}

bool argBool(const Json& args, const char* key, bool fallback)
{
    return args[key].boolean(fallback);
}

Json schemaString(const char* description)
{
    Json j = Json::object();
    j.set("type", "string").set("description", description);
    return j;
}

Json schemaInteger(const char* description)
{
    Json j = Json::object();
    j.set("type", "integer").set("description", description);
    return j;
}

Json schemaBoolean(const char* description)
{
    Json j = Json::object();
    j.set("type", "boolean").set("description", description);
    return j;
}

std::filesystem::path resolvePath(const std::string& raw,
                                  const std::filesystem::path& workspace)
{
    std::string value = trim(paths::expandUser(raw));
    if (value.empty())
        return {};

    std::filesystem::path p = paths::fromUtf8(value);
    if (p.is_relative())
        p = workspace / p;

    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec)
        return p;
    return abs.lexically_normal();
}

bool isProbablyBinary(const std::string& data)
{
    size_t n = std::min<size_t>(data.size(), 8192);
    for (size_t i = 0; i < n; ++i)
    {
        if (data[i] == '\0')
            return true;
    }
    return false;
}

std::string readWholeFile(const std::filesystem::path& p, std::string& err,
                          size_t maxBytes)
{
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec)
    {
        err = "文件不存在：" + paths::toUtf8(p);
        return {};
    }
    if (!std::filesystem::is_regular_file(p, ec) || ec)
    {
        err = "不是普通文件：" + paths::toUtf8(p);
        return {};
    }

    uintmax_t size = std::filesystem::file_size(p, ec);
    if (ec)
    {
        err = "无法读取文件大小：" + paths::toUtf8(p);
        return {};
    }
    if (size > maxBytes)
    {
        err = "文件过大（" + formatBytes((uint64_t)size) + "），最大允许 " +
              formatBytes(maxBytes);
        return {};
    }

    std::ifstream in(p, std::ios::binary);
    if (!in)
    {
        err = "无法打开文件：" + paths::toUtf8(p);
        return {};
    }

    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (!in && !in.eof())
    {
        err = "读取文件失败：" + paths::toUtf8(p);
        return {};
    }
    return data;
}

bool writeWholeFile(const std::filesystem::path& p, const std::string& data,
                    std::string& err)
{
    std::error_code ec;
    if (!p.parent_path().empty())
        std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
    {
        err = "无法创建目录：" + paths::toUtf8(p.parent_path());
        return false;
    }

    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err = "无法写入文件：" + paths::toUtf8(p);
        return false;
    }
    out.write(data.data(), (std::streamsize)data.size());
    if (!out)
    {
        err = "写入文件失败：" + paths::toUtf8(p);
        return false;
    }
    return true;
}

size_t lineNumberAt(const std::string& text, size_t offset)
{
    size_t line = 1;
    for (size_t i = 0; i < offset && i < text.size(); ++i)
        if (text[i] == '\n')
            ++line;
    return line;
}

//==============================================================================
// read_file
//==============================================================================
ToolResult toolReadFile(const Json& args, ToolContext& ctx)
{
    ToolResult r;
    std::string raw = argString(args, "path");
    if (trim(raw).empty())
    {
        r.ok = false;
        r.output = "缺少参数 path。";
        return r;
    }

    std::filesystem::path p = resolvePath(raw, ctx.workspace);

    if (attachment::isImageExtension(p))
    {
        if (!ctx.imageInput)
        {
            r.ok = false;
            r.output = tr().attachImageUnsupported;
            return r;
        }
        if (ctx.imageCount && ctx.maxImages > 0 &&
            *ctx.imageCount >= ctx.maxImages)
        {
            r.ok = false;
            r.output = "本轮图片数量已达上限（" +
                       std::to_string(ctx.maxImages) + " 张）。";
            return r;
        }

        Attachment a;
        std::string err;
        if (!attachment::loadInlineImage(p, a, err))
        {
            r.ok = false;
            r.output = err;
            return r;
        }
        if (ctx.imageCount)
            ++*ctx.imageCount;

        std::ostringstream out;
        out << "文件：" << paths::toUtf8(p) << "\n";
        out << "类型：" << a.mime << "\n";
        out << "大小：" << formatBytes(a.size) << "\n";
        out << "图片已附加，见下一条消息。";
        r.output = out.str();
        r.attachments.push_back(std::move(a));
        return r;
    }

    std::string err;
    std::string data = readWholeFile(p, err, kMaxReadBytes);
    if (!err.empty())
    {
        r.ok = false;
        r.output = err;
        return r;
    }

    if (isProbablyBinary(data))
    {
        r.ok = false;
        r.output = "看起来是二进制文件，无法按文本读取：" + paths::toUtf8(p);
        return r;
    }

    int offset = std::max(1, argInt(args, "offset", 1));
    int limit = argInt(args, "limit", 2000);
    if (limit <= 0)
        limit = 2000;
    limit = std::min(limit, 5000);

    std::vector<std::string> lines;
    std::string current;
    for (char c : data)
    {
        if (c == '\n')
        {
            if (!current.empty() && current.back() == '\r')
                current.pop_back();
            lines.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    if (!current.empty())
    {
        if (current.back() == '\r')
            current.pop_back();
        lines.push_back(current);
    }

    std::ostringstream out;
    out << "文件：" << paths::toUtf8(p) << "\n";
    out << "共 " << lines.size() << " 行";
    if (offset > (int)lines.size())
    {
        out << "（起始行超出文件末尾）\n";
        r.output = out.str();
        return r;
    }

    int end = std::min((int)lines.size(), offset - 1 + limit);
    out << "，显示第 " << offset << "–" << end << " 行\n\n";

    size_t produced = out.str().size();
    bool truncated = false;
    int  nextLine = end + 1;
    for (int i = offset - 1; i < end; ++i)
    {
        std::string content = lines[(size_t)i];
        if (content.size() > 16000)
            content = utf8Prefix(content, 4000) + "…[行过长，已截断]";

        std::string line =
            std::to_string(i + 1) + " | " + content + "\n";
        if (produced + line.size() > kMaxReadOutput)
        {
            truncated = true;
            nextLine = i + 1;
            break;
        }
        out << line;
        produced += line.size();
    }
    if (truncated)
        out << "\n[输出过大，已截断]\n";
    if (nextLine <= (int)lines.size())
        out << "\n[还有 " << (int)lines.size() - nextLine + 1
            << " 行未显示，可从第 " << nextLine
            << " 行用 offset 继续读取]\n";

    r.output = out.str();
    return r;
}

//==============================================================================
// write_file
//==============================================================================
ToolResult toolWriteFile(const Json& args, ToolContext& ctx)
{
    ToolResult r;
    std::string raw = argString(args, "path");
    if (trim(raw).empty())
    {
        r.ok = false;
        r.output = "缺少参数 path。";
        return r;
    }

    std::string content = argString(args, "content");
    if (content.size() > kMaxWriteBytes)
    {
        r.ok = false;
        r.output = "内容过大（" + formatBytes(content.size()) + "），最大允许 " +
                   formatBytes(kMaxWriteBytes);
        return r;
    }

    std::filesystem::path p = resolvePath(raw, ctx.workspace);
    bool existed = std::filesystem::exists(p);
    std::string err;
    if (!writeWholeFile(p, content, err))
    {
        r.ok = false;
        r.output = err;
        return r;
    }

    r.output = std::string(existed ? "已覆盖" : "已创建") + "文件：" +
               paths::toUtf8(p) + "\n写入 " + formatBytes(content.size()) + "。";
    return r;
}

//==============================================================================
// edit_file
//==============================================================================
ToolResult toolEditFile(const Json& args, ToolContext& ctx)
{
    ToolResult r;
    std::string raw = argString(args, "path");
    std::string oldText = argString(args, "old_string");
    std::string newText = argString(args, "new_string");

    if (trim(raw).empty())
    {
        r.ok = false;
        r.output = "缺少参数 path。";
        return r;
    }
    if (oldText.empty())
    {
        r.ok = false;
        r.output = "参数 old_string 不能为空。";
        return r;
    }
    if (oldText == newText)
    {
        r.ok = false;
        r.output = "old_string 和 new_string 相同，无需修改。";
        return r;
    }

    std::filesystem::path p = resolvePath(raw, ctx.workspace);
    std::string err;
    std::string data = readWholeFile(p, err, kMaxWriteBytes);
    if (!err.empty())
    {
        r.ok = false;
        r.output = err;
        return r;
    }

    bool replaceAllFlag = argBool(args, "replace_all", false);
    size_t count = 0;
    size_t pos = 0;
    size_t firstPos = std::string::npos;
    while ((pos = data.find(oldText, pos)) != std::string::npos)
    {
        if (count == 0)
            firstPos = pos;
        ++count;
        pos += oldText.size();
    }

    if (count == 0)
    {
        r.ok = false;
        r.output = "没有找到 old_string。请先重新读取文件，确认要替换的原文完全一致（包括空格和换行）。";
        return r;
    }
    if (count > 1 && !replaceAllFlag)
    {
        r.ok = false;
        r.output = "old_string 出现了 " + std::to_string(count) +
                   " 次。请提供更长的上下文使其唯一，或设置 replace_all=true。";
        return r;
    }

    size_t line = lineNumberAt(data, firstPos);
    if (replaceAllFlag)
    {
        data = replaceAll(data, oldText, newText);
    }
    else
    {
        data.replace(firstPos, oldText.size(), newText);
    }

    if (!writeWholeFile(p, data, err))
    {
        r.ok = false;
        r.output = err;
        return r;
    }

    r.output = "已修改文件：" + paths::toUtf8(p) + "\n替换 " +
               std::to_string(count) + " 处，首次位于第 " +
               std::to_string(line) + " 行。";
    return r;
}

//==============================================================================
// shell
//==============================================================================
ToolResult toolShell(const Json& args, ToolContext& ctx)
{
    ToolResult r;
    std::string command = argString(args, "command");
    if (trim(command).empty())
    {
        r.ok = false;
        r.output = "缺少参数 command。";
        return r;
    }

    std::filesystem::path cwd = ctx.workspace;
    std::string rawCwd = argString(args, "cwd");
    if (!trim(rawCwd).empty())
        cwd = resolvePath(rawCwd, ctx.workspace);

    std::error_code ec;
    if (!std::filesystem::is_directory(cwd, ec) || ec)
    {
        r.ok = false;
        r.output = "工作目录不存在：" + paths::toUtf8(cwd);
        return r;
    }

    int timeoutMs = argInt(args, "timeout_ms", kDefaultTimeout);
    timeoutMs = std::max(1000, std::min(timeoutMs, 600000));

    std::string tmpName = "sdlagent_shell_" + makeId() + ".log";
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / tmpName;
    if (ec)
        tmp = cwd / tmpName;

    std::string cwdStr = paths::toUtf8(cwd);
    std::string tmpStr = paths::toUtf8(tmp);

    std::string full;
    SDL_Process* proc = nullptr;
#ifdef _WIN32
    full = "chcp 65001>nul & cd /d \"" + replaceAll(cwdStr, "\"", "\\\"") +
           "\" && (" + command + ") > \"" +
           replaceAll(tmpStr, "\"", "\\\"") + "\" 2>&1";
    const char* shell = std::getenv("COMSPEC");
    if (!shell || !*shell)
        shell = "cmd.exe";

    //Pass the command line directly to CreateProcess. SDL_CreateProcess()
    //quotes each argv element, which breaks cmd's /C syntax when the command
    //itself contains quotes, ampersands or redirections.
    std::string cmdline = std::string("\"") + shell + "\" /C " + full;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_CMDLINE_STRING,
                          cmdline.c_str());
    proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
#else
    full = "cd \"" + replaceAll(cwdStr, "\"", "\\\"") + "\" && { " + command +
           " ; } > \"" + replaceAll(tmpStr, "\"", "\\\"") + "\" 2>&1";
#if defined(__ANDROID__)
    const char* argv[] = { "/system/bin/sh", "-c", full.c_str(), nullptr };
#else
    const char* argv[] = { "/bin/sh", "-c", full.c_str(), nullptr };
#endif
    proc = SDL_CreateProcess(argv, false);
#endif

    if (!proc)
    {
        r.ok = false;
        r.output = std::string("无法启动命令：") + SDL_GetError();
        return r;
    }

    Uint64 start = SDL_GetTicks();
    int exitCode = 0;
    bool exited = false;
    bool timedOut = false;
    bool cancelled = false;

    while (true)
    {
        if (ctx.cancel && ctx.cancel->load(std::memory_order_relaxed))
        {
            SDL_KillProcess(proc, true);
            SDL_WaitProcess(proc, true, &exitCode);
            cancelled = true;
            break;
        }
        if (SDL_WaitProcess(proc, false, &exitCode))
        {
            exited = true;
            break;
        }
        if (SDL_GetTicks() - start > (Uint64)timeoutMs)
        {
            SDL_KillProcess(proc, true);
            SDL_WaitProcess(proc, true, &exitCode);
            timedOut = true;
            break;
        }
        SDL_Delay(25);
    }
    SDL_DestroyProcess(proc);

    std::string output;
    {
        std::ifstream in(tmp, std::ios::binary);
        if (in)
        {
            output.assign((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        }
    }
    std::filesystem::remove(tmp, ec);

    if (output.size() > kMaxShellOutput)
    {
        output = output.substr(0, kMaxShellOutput);
        output += "\n[输出过长，已截断]";
    }

    std::ostringstream out;
    if (cancelled)
    {
        out << "[命令已取消]\n";
        r.ok = false;
    }
    else if (timedOut)
    {
        out << "[命令超时（" << timeoutMs / 1000 << " 秒），已终止]\n";
        r.ok = false;
    }
    else if (exited)
    {
        out << "退出码: " << exitCode << "\n";
    }

    if (output.empty())
        out << "(无输出)";
    else
        out << output;

    r.output = out.str();
    return r;
}

//==============================================================================
// web_search (reserved)
//==============================================================================
ToolResult toolWebSearch(const Json&, ToolContext&)
{
    ToolResult r;
    r.ok = false;
    r.output = tr().webSearchReserved;
    return r;
}

Json objectSchema(Json properties, std::initializer_list<const char*> required)
{
    Json params = Json::object();
    params.set("type", "object")
          .set("properties", std::move(properties))
          .set("required", jsonStringArray(required));
    return params;
}

ToolSpec makeReadTool()
{
    ToolSpec t;
    t.name = "read_file";
    t.description =
        "读取文本文件或图片。文本返回带行号的内容；图片作为图片内容附加。"
        "用 offset/limit 读取大文本文件的片段。";

    Json props = Json::object();
    props.set("path", schemaString("文件路径，相对路径基于工作区"))
         .set("offset", schemaInteger("起始行号（1-based），可选"))
         .set("limit", schemaInteger("最多读取的行数，默认 2000"));
    t.parameters = objectSchema(std::move(props), { "path" });

    t.run = toolReadFile;
    t.describe = [](const Json& args) -> ToolApproval
    {
        return { "read_file", argString(args, "path") };
    };
    return t;
}

ToolSpec makeWriteTool()
{
    ToolSpec t;
    t.name = "write_file";
    t.description = "创建或整体覆盖一个文本文件。修改已有文件时优先使用 edit_file。";

    Json props = Json::object();
    props.set("path", schemaString("文件路径，相对路径基于工作区"))
         .set("content", schemaString("完整的文件内容"));
    t.parameters = objectSchema(std::move(props), { "path", "content" });

    t.run = toolWriteFile;
    t.describe = [](const Json& args) -> ToolApproval
    {
        std::string path = argString(args, "path");
        std::string content = argString(args, "content");
        std::string preview = content.size() > 1200
                                  ? content.substr(0, 1200) + "\n…[已截断]"
                                  : content;
        return { "write_file · " + path, "路径：" + path + "\n\n内容预览：\n" + preview };
    };
    return t;
}

ToolSpec makeEditTool()
{
    ToolSpec t;
    t.name = "edit_file";
    t.description =
        "在文件中做精确字符串替换。old_string 必须与文件内容完全一致且唯一；"
        "replace_all=true 时替换所有匹配。";

    Json props = Json::object();
    props.set("path", schemaString("文件路径"))
         .set("old_string", schemaString("要替换的原文"))
         .set("new_string", schemaString("替换后的文本"))
         .set("replace_all", schemaBoolean("是否替换所有匹配，默认 false"));
    t.parameters = objectSchema(std::move(props),
                                { "path", "old_string", "new_string" });

    t.run = toolEditFile;
    t.describe = [](const Json& args) -> ToolApproval
    {
        std::string path = argString(args, "path");
        std::string oldText = argString(args, "old_string");
        std::string newText = argString(args, "new_string");
        auto clip = [](std::string s)
        {
            return s.size() > 800 ? s.substr(0, 800) + "\n…[已截断]" : s;
        };
        return { "edit_file · " + path,
                 "路径：" + path + "\n\n--- 原文 ---\n" + clip(oldText) +
                     "\n\n+++ 新文 +++\n" + clip(newText) };
    };
    return t;
}

ToolSpec makeShellTool()
{
    ToolSpec t;
    t.name = "shell";
    t.description =
        "在系统 shell 中执行一条命令并返回输出（stdout + stderr）。"
        "Windows 使用 cmd.exe，其他平台使用 /bin/sh。";

    Json props = Json::object();
    props.set("command", schemaString("要执行的命令"))
         .set("cwd", schemaString("工作目录，默认为工作区"))
         .set("timeout_ms", schemaInteger("超时毫秒数，默认 60000"));
    t.parameters = objectSchema(std::move(props), { "command" });

    t.run = toolShell;
    t.describe = [](const Json& args) -> ToolApproval
    {
        std::string command = argString(args, "command");
        std::string cwd = argString(args, "cwd");
        std::string summary = "shell · " + firstLine(command, 70);
        std::string detail = "$ " + command;
        if (!cwd.empty())
            detail += "\n\n工作目录：" + cwd;
        return { summary, detail };
    };
    return t;
}

ToolSpec makeWebSearchTool()
{
    //reserved: implement the HTTP search backend here, then flip available=true
    ToolSpec t;
    t.name = "web_search";
    t.description = "在互联网上搜索信息（预留，暂未启用）。";

    Json props = Json::object();
    props.set("query", schemaString("搜索关键词"));
    t.parameters = objectSchema(std::move(props), { "query" });

    t.available = false;
    t.run = toolWebSearch;
    t.describe = [](const Json& args) -> ToolApproval
    {
        return { "web_search · " + argString(args, "query"), argString(args, "query") };
    };
    return t;
}

} //namespace

void ToolRegistry::add(ToolSpec spec)
{
    tools_.push_back(std::move(spec));
}

const ToolSpec* ToolRegistry::find(const std::string& name) const
{
    for (const auto& t : tools_)
        if (t.name == name)
            return &t;
    return nullptr;
}

std::string ToolRegistry::schemaJson() const
{
    Json arr = Json::array();
    for (const auto& t : tools_)
    {
        if (!t.available)
            continue;

        Json fn = Json::object();
        fn.set("name", t.name)
          .set("description", t.description)
          .set("parameters", t.parameters);

        Json item = Json::object();
        item.set("type", "function").set("function", std::move(fn));
        arr.push(std::move(item));
    }
    return arr.dump();
}

ToolRegistry makeBuiltinTools()
{
    ToolRegistry reg;
    reg.add(makeReadTool());
    reg.add(makeWriteTool());
    reg.add(makeEditTool());
    reg.add(makeShellTool());
    reg.add(makeWebSearchTool()); //reserved
    return reg;
}

} //namespace sdlagent
