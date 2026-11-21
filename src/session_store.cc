// SPDX-License-Identifier: GPL-3.0-only

#include "session_store.hpp"

#include "paths.hpp"
#include "log.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <system_error>

namespace sdlagent
{

SessionStore::SessionStore(std::filesystem::path root)
    : root_(std::move(root))
{
}

void SessionStore::setRoot(std::filesystem::path root)
{
    root_ = std::move(root);
}

namespace
{

std::string safeSessionId(const std::string& id)
{
    std::string safe;
    safe.reserve(id.size());
    for (char c : id)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            safe += c;
    }
    if (safe.empty())
        safe = "session";
    return safe;
}

bool readSessionFile(const std::filesystem::path& path, Session& out,
                     std::string& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        err = "无法读取会话文件：" + paths::toUtf8(path);
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    Json j = Json::parse(text);
    if (!j.isObject())
    {
        err = "会话文件格式不正确：" + paths::toUtf8(path);
        return false;
    }

    out = sessionFromJson(j);
    return true;
}

const char* kIndexFile = "index.json";

Json indexJson(const std::vector<Session>& sessions)
{
    Json root = Json::object();
    root.set("version", 1);

    Json list = Json::array();
    for (const Session& s : sessions)
    {
        Json item = Json::object();
        item.set("id", s.id)
            .set("title", s.title)
            .set("pinned", s.pinned)
            .set("createdAt", (long long)s.createdAt)
            .set("updatedAt", (long long)s.updatedAt)
            .set("hasMessages", s.hasMessages);
        if (!s.model.empty())
            item.set("model", s.model);
        list.push(std::move(item));
    }
    root.set("sessions", std::move(list));
    return root;
}

bool writeIndexTo(const std::filesystem::path& root,
                  const std::vector<Session>& sessions, std::string& err)
{
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        err = "无法创建会话目录：" + paths::toUtf8(root);
        return false;
    }

    std::filesystem::path file = root / kIndexFile;
    std::filesystem::path tmp = file;
    tmp += ".tmp";

    std::string text = indexJson(sessions).dump(true);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            err = "无法写入会话索引：" + paths::toUtf8(file);
            return false;
        }
        out.write(text.data(), (std::streamsize)text.size());
        if (!out)
        {
            err = "写入会话索引失败：" + paths::toUtf8(file);
            return false;
        }
    }

    std::filesystem::remove(file, ec);
    std::filesystem::rename(tmp, file, ec);
    if (ec)
    {
        err = "保存会话索引失败：" + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool readIndex(const std::filesystem::path& root,
               std::vector<Session>& out, std::string& err)
{
    std::filesystem::path file = root / kIndexFile;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || ec)
    {
        err = "会话索引不存在。";
        return false;
    }

    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        err = "无法读取会话索引：" + paths::toUtf8(file);
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    Json j = Json::parse(text);
    if (!j.isObject())
    {
        err = "会话索引格式不正确。";
        return false;
    }

    Json list = j["sessions"];
    if (!list.isArray())
    {
        err = "会话索引缺少 sessions 数组。";
        return false;
    }

    out.clear();
    std::set<std::string> seen;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        Json item = list[i];
        if (!item.isObject())
            continue;

        Session s;
        s.id = item["id"].str();
        if (s.id.empty() || !seen.insert(s.id).second)
            continue;
        s.title = item["title"].str();
        s.pinned = item["pinned"].boolean(false);
        s.createdAt = (uint64_t)item["createdAt"].integer(0);
        s.updatedAt = (uint64_t)item["updatedAt"].integer(0);
        s.model = item["model"].str();
        s.hasMessages = item["hasMessages"].boolean(false);
        s.loaded = false;
        out.push_back(std::move(s));
    }

    return true;
}

} //namespace

std::filesystem::path SessionStore::fileFor(const std::string& id) const
{
    return root_ / (safeSessionId(id) + ".json");
}

bool SessionStore::loadAll(std::string& err)
{
    sessions_.clear();
    err.clear();

    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec)
    {
        err = "无法创建会话目录：" + paths::toUtf8(root_) + " (" + ec.message() + ")";
        return false;
    }

    //Collect session ids from the directory without parsing the files.
    std::set<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        if (entry.path().filename() == kIndexFile)
            continue;
        files.insert(paths::toUtf8(entry.path().stem()));
    }

    std::string indexErr;
    bool haveIndex = readIndex(root_, sessions_, indexErr);

    //Index missing or corrupt: rebuild it by reading every session once.
    if (!haveIndex)
    {
        int broken = 0;
        for (const std::string& id : files)
        {
            Session s;
            std::string readErr;
            if (!readSessionFile(root_ / (id + ".json"), s, readErr))
            {
                ++broken;
                continue;
            }
            s.id = id;
            s.hasMessages = !s.messages.empty();
            s.loaded = false;
            std::vector<Message>().swap(s.messages);
            sessions_.push_back(std::move(s));
        }

        std::string writeErr;
        if (!writeIndexTo(root_, sessions_, writeErr))
            log::writef("%s", writeErr.c_str());

        if (broken > 0)
            err = "有 " + std::to_string(broken) + " 个会话文件无法解析，已跳过。";
        return true;
    }

    //Reconcile the index with the actual files.
    std::set<std::string> indexed;
    for (const Session& s : sessions_)
        indexed.insert(s.id);

    bool changed = false;

    //Entries whose files disappeared.
    std::size_t before = sessions_.size();
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [&](const Session& s)
                       { return files.find(s.id) == files.end(); }),
        sessions_.end());
    if (sessions_.size() != before)
        changed = true;

    //Files added without an index entry.
    int broken = 0;
    for (const std::string& id : files)
    {
        if (indexed.count(id))
            continue;

        Session s;
        std::string readErr;
        if (!readSessionFile(root_ / (id + ".json"), s, readErr))
        {
            ++broken;
            continue;
        }
        s.id = id;
        s.hasMessages = !s.messages.empty();
        s.loaded = false;
        std::vector<Message>().swap(s.messages);
        sessions_.push_back(std::move(s));
        changed = true;
    }

    if (changed)
    {
        std::string writeErr;
        if (!writeIndexTo(root_, sessions_, writeErr))
            log::writef("%s", writeErr.c_str());
    }

    if (broken > 0)
        err = "有 " + std::to_string(broken) + " 个会话文件无法解析，已跳过。";

    return true;
}

bool SessionStore::loadSession(const std::string& id, std::string& err)
{
    err.clear();
    Session* s = find(id);
    if (!s)
    {
        err = "会话不存在。";
        return false;
    }

    std::filesystem::path file = fileFor(id);
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || ec)
    {
        //A brand-new draft only lives in memory until its first message.
        if (s->messages.empty() && !s->hasMessages)
        {
            s->loaded = true;
            return true;
        }
        err = "会话文件已丢失：" + paths::toUtf8(file);
        return false;
    }

    Session loaded;
    if (!readSessionFile(file, loaded, err))
        return false;

    if (loaded.id.empty() || loaded.id == "imported")
        loaded.id = id;
    //The in-memory entry is keyed by the id the UI holds.
    loaded.id = id;

    loaded.hasMessages = !loaded.messages.empty();
    loaded.loaded = true;

    bool metaChanged =
        s->title != loaded.title || s->pinned != loaded.pinned ||
        s->createdAt != loaded.createdAt || s->updatedAt != loaded.updatedAt ||
        s->model != loaded.model || s->hasMessages != loaded.hasMessages;

    //Keep the same vector slot so pointers held by the UI stay valid.
    *s = std::move(loaded);
    if (metaChanged)
    {
        std::string indexErr;
        if (!saveIndex(indexErr))
            log::writef("%s", indexErr.c_str());
    }
    return true;
}

void SessionStore::unload(const std::string& id)
{
    Session* s = find(id);
    if (!s)
        return;
    std::vector<Message>().swap(s->messages);
    s->loaded = false;
}

void SessionStore::unloadAllExcept(const std::set<std::string>& keep)
{
    for (Session& s : sessions_)
    {
        if (keep.count(s.id))
            continue;
        std::vector<Message>().swap(s.messages);
        s.loaded = false;
    }
}

void SessionStore::forget(const std::string& id)
{
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [&](const Session& s)
                                   { return s.id == id; }),
                    sessions_.end());

    std::string indexErr;
    if (!saveIndex(indexErr))
        log::writef("%s", indexErr.c_str());
}

bool SessionStore::fileExists(const std::string& id) const
{
    std::error_code ec;
    return std::filesystem::is_regular_file(fileFor(id), ec) && !ec;
}

bool SessionStore::save(const Session& s, std::string& err)
{
    if (!s.loaded)
    {
        err = "内部错误：会话尚未加载，拒绝覆盖文件。";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec)
    {
        err = "无法创建会话目录：" + paths::toUtf8(root_);
        return false;
    }

    std::filesystem::path file = fileFor(s.id);
    std::filesystem::path tmp = file;
    tmp += ".tmp";

    std::string text = toJson(s).dump(true);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            err = "无法写入会话文件：" + paths::toUtf8(file);
            return false;
        }
        out.write(text.data(), (std::streamsize)text.size());
        if (!out)
        {
            err = "写入会话文件失败：" + paths::toUtf8(file);
            return false;
        }
    }

    //std::filesystem::rename does not replace on Windows
    std::filesystem::remove(file, ec);
    std::filesystem::rename(tmp, file, ec);
    if (ec)
    {
        err = "保存会话失败：" + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }

    //Metadata mirror used by the sidebar at startup.
    std::string indexErr;
    if (!saveIndex(indexErr))
        log::writef("%s", indexErr.c_str());
    return true;
}

bool SessionStore::saveIndex(std::string& err) const
{
    return writeIndexTo(root_, sessions_, err);
}

bool SessionStore::remove(const std::string& id, std::string& err)
{
    std::error_code ec;
    std::filesystem::remove(fileFor(id), ec);
    if (ec)
    {
        err = "删除会话失败：" + ec.message();
        return false;
    }

    std::error_code aec;
    std::filesystem::remove_all(root_ / "attachments" /
                                    safeSessionId(id),
                                aec);

    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [&](const Session& s) { return s.id == id; }),
                    sessions_.end());

    std::string indexErr;
    if (!saveIndex(indexErr))
        log::writef("%s", indexErr.c_str());
    return true;
}

Session& SessionStore::create()
{
    Session s;
    s.id = makeId();
    s.createdAt = nowMs();
    s.updatedAt = s.createdAt;
    s.loaded = true;
    sessions_.push_back(std::move(s));
    return sessions_.back();
}

void SessionStore::add(Session s)
{
    s.loaded = true;
    s.hasMessages = !s.messages.empty();
    sessions_.push_back(std::move(s));
}

Session* SessionStore::find(const std::string& id)
{
    for (auto& s : sessions_)
        if (s.id == id)
            return &s;
    return nullptr;
}

int SessionStore::copyAllTo(const std::filesystem::path& dst,
                            std::string& err) const
{
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    if (ec)
    {
        err = "无法创建目标目录：" + paths::toUtf8(dst);
        return 0;
    }

    int copied = 0;
    int failed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        if (entry.path().filename() == kIndexFile)
            continue;

        std::filesystem::path target = dst / entry.path().filename();
        std::filesystem::copy_file(entry.path(), target,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec)
        {
            ++failed;
            ec.clear();
        }
        else
        {
            ++copied;
        }
    }

    //Attachments are stored next to the session files and must move with
    //them; sessions read them relative to the sessions root.
    std::filesystem::path srcAtt = root_ / "attachments";
    if (std::filesystem::is_directory(srcAtt, ec) && !ec)
    {
        std::error_code cec;
        std::filesystem::copy(
            srcAtt, dst / "attachments",
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing,
            cec);
        if (cec)
            ++failed;
    }

    //sessions created in memory but not saved yet
    for (const Session& s : sessions_)
    {
        //Never serialize a metadata-only entry: it has no messages loaded.
        if (!s.loaded)
            continue;

        std::filesystem::path target = dst / fileFor(s.id).filename();
        if (!std::filesystem::exists(target, ec))
        {
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                ++failed;
                continue;
            }
            std::string text = toJson(s).dump(true);
            out.write(text.data(), (std::streamsize)text.size());
            ++copied;
        }
    }

    if (failed > 0)
        err = "有 " + std::to_string(failed) + " 个会话复制失败。";

    std::string indexErr;
    if (!writeIndexTo(dst, sessions_, indexErr))
    {
        if (!err.empty())
            err += " ";
        err += indexErr;
    }
    return copied;
}

} //namespace sdlagent
