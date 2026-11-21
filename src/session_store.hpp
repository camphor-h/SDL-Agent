// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "session.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace sdlagent
{

//one JSON file per session inside a user-configurable directory
class SessionStore
{
public:
    explicit SessionStore(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    void setRoot(std::filesystem::path root);

    //Load sidebar metadata from index.json, reconciling it with the session
    //files. A missing/corrupt index is rebuilt by scanning every file once.
    bool loadAll(std::string& err);

    //Read one session's messages into memory. Returns false when the file is
    //missing/corrupt, or when it cannot be parsed.
    bool loadSession(const std::string& id, std::string& err);

    //Drop messages but keep the sidebar metadata.
    void unload(const std::string& id);
    void unloadAllExcept(const std::set<std::string>& keep);

    //Remove an in-memory entry without touching its file (used when the file
    //disappeared underneath us).
    void forget(const std::string& id);

    bool fileExists(const std::string& id) const;

    bool save(const Session& s, std::string& err);
    bool remove(const std::string& id, std::string& err);

    Session& create();
    void     add(Session s);
    Session* find(const std::string& id);

    std::vector<Session>&       sessions() { return sessions_; }
    const std::vector<Session>& sessions() const { return sessions_; }

    //copy all session files into dst; returns the number copied
    int copyAllTo(const std::filesystem::path& dst, std::string& err) const;

private:
    std::filesystem::path fileFor(const std::string& id) const;
    bool saveIndex(std::string& err) const;

    std::filesystem::path root_;
    std::vector<Session> sessions_;
};

} //namespace sdlagent
