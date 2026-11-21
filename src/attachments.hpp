// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "session.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sdlagent::attachment
{

//Limits are deliberately conservative: attachments are copied into the
//session directory and re-sent with every request that includes them.
constexpr uint64_t kMaxTextFile  = 1024ull * 1024ull;
constexpr uint64_t kMaxTextTotal = 2ull * 1024ull * 1024ull;
constexpr uint64_t kMaxImageFile = 4ull * 1024ull * 1024ull;
constexpr uint64_t kMaxImageTotal = 12ull * 1024ull * 1024ull;
constexpr int      kMaxFiles = 8;
constexpr int      kMaxImages = 4;

//Creates <dataDir>/attachments.json with the default extension lists when it
//does not exist yet. The file is re-read automatically when edited.
void ensureConfig();

//Inspects a user-selected file and fills `out` (kind/name/size/mime/path).
//The path is the original absolute path and is not persisted by itself.
bool inspect(const std::filesystem::path& source, bool imageAllowed,
             Attachment& out, std::string& err);

//Copies the file into <sessionsRoot>/attachments/<sessionId>/ and rewrites
//out.path to a path relative to sessionsRoot.
bool importFile(const std::filesystem::path& source, const Attachment& info,
                const std::filesystem::path& sessionsRoot,
                const std::string& sessionId, Attachment& out,
                std::string& err);

//Removes <sessionsRoot>/attachments/<sessionId> if it exists.
void removeSession(const std::filesystem::path& sessionsRoot,
                   const std::string& sessionId);

bool isImage(const Attachment& a);

//True when the extension is listed under `image` in attachments.json.
bool isImageExtension(const std::filesystem::path& path);

//Reads an image fully into Attachment::data as base64. No file copy is made.
bool loadInlineImage(const std::filesystem::path& source, Attachment& out,
                     std::string& err);

//Absolute path for a stored attachment.
std::filesystem::path storedPath(const std::filesystem::path& sessionsRoot,
                                 const Attachment& a);

//Reads a stored text attachment as UTF-8. Returns false and fills `err` when
//the file is missing or unreadable.
bool readText(const std::filesystem::path& sessionsRoot, const Attachment& a,
              std::string& out, std::string& err);

//Reads a stored image and returns a data: URL, or an empty string on failure.
std::string imageDataUrl(const std::filesystem::path& sessionsRoot,
                         const Attachment& a, std::string& err);

//Decodes inline base64 into a temporary file and returns its path.
bool materializeInline(const Attachment& a, std::filesystem::path& out,
                       std::string& err);

//One-line label such as "notes.md · 12.0 KB".
std::string label(const Attachment& a);

//"附件" prefix used in chat history.
std::string historyLine(const Attachment& a);

} //namespace sdlagent::attachment
