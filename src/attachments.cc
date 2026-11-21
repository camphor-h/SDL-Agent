// SPDX-License-Identifier: GPL-3.0-only

#include "attachments.hpp"

#include "i18n.hpp"
#include "json.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "util.hpp"

#define BASE64_STATIC_DEFINE
#include "libbase64.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace sdlagent::attachment
{
namespace
{

struct TypeConfig
{
    bool        loaded = false;
    std::string path;
    std::filesystem::file_time_type mtime{};
    std::uintmax_t size = 0;
    std::vector<std::string> text;
    std::vector<std::string> images;
};

TypeConfig gTypes;

const char* kDefaultTextTypes =
    "txt,md,markdown,mdx,rst,json,jsonc,json5,csv,tsv,log,xml,yaml,yml,"
    "ini,cfg,conf,toml,env,properties,c,h,cc,cpp,cxx,hpp,hxx,inl,ipp,m,mm,"
    "cs,java,kt,kts,swift,go,rs,py,pyi,rb,php,pl,lua,js,mjs,cjs,jsx,ts,tsx,"
    "vue,svelte,html,htm,css,scss,sass,less,sql,sh,bash,zsh,fish,ps1,bat,cmd,"
    "cmake,mk,gradle,tex,bib,proto,graphql,gql,diff,patch,srt,vtt,ass";

const char* kDefaultImageTypes =
    "png,jpg,jpeg,webp,gif,bmp";

std::string normalizeExt(std::string ext)
{
    ext = trim(ext);
    if (!ext.empty() && ext[0] == '.')
        ext.erase(ext.begin());
    for (char& c : ext)
        c = (char)std::tolower((unsigned char)c);
    return ext;
}

std::vector<std::string> splitCsv(const std::string& text)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t comma = text.find(',', start);
        std::string item = normalizeExt(text.substr(
            start, comma == std::string::npos ? std::string::npos
                                              : comma - start));
        if (!item.empty())
            out.push_back(std::move(item));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

void readList(const Json& root, const char* key,
              const std::vector<std::string>& fallback,
              std::vector<std::string>& out)
{
    Json arr = root[key];
    if (!arr.isArray())
    {
        out = fallback;
        return;
    }

    out.clear();
    for (std::size_t i = 0; i < arr.size(); ++i)
    {
        Json item = arr[i];
        if (!item.isString())
            continue;
        std::string ext = normalizeExt(item.str());
        if (ext.empty())
            continue;
        if (std::find(out.begin(), out.end(), ext) == out.end())
            out.push_back(std::move(ext));
    }
}

void writeDefaultConfig(const std::filesystem::path& path)
{
    Json text = Json::array();
    for (const std::string& ext : splitCsv(kDefaultTextTypes))
        text.push(ext);

    Json images = Json::array();
    for (const std::string& ext : splitCsv(kDefaultImageTypes))
        images.push(ext);

    Json root = Json::object();
    root.set("_comment",
             "按扩展名决定附件类型。text 作为 UTF-8 文本读取，image 作为图片"
             "发送；未列出的扩展名会被拒绝。空字符串表示无扩展名文件。");
    root.set("text", std::move(text));
    root.set("image", std::move(images));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return;
    std::string dump = root.dump(true);
    out.write(dump.data(), (std::streamsize)dump.size());
}

const TypeConfig& typeConfig()
{
    std::filesystem::path path = paths::attachmentsConfigFile();
    std::string pathText = paths::toUtf8(path);

    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec) && !ec;

    if (!exists)
    {
        writeDefaultConfig(path);
        exists = std::filesystem::exists(path, ec) && !ec;
    }

    std::filesystem::file_time_type mtime{};
    std::uintmax_t size = 0;
    if (exists)
    {
        mtime = std::filesystem::last_write_time(path, ec);
        size = std::filesystem::file_size(path, ec);
    }

    if (gTypes.loaded && gTypes.path == pathText && exists &&
        gTypes.mtime == mtime && gTypes.size == size)
        return gTypes;

    gTypes = TypeConfig{};
    gTypes.path = pathText;

    std::vector<std::string> defaultText = splitCsv(kDefaultTextTypes);
    std::vector<std::string> defaultImages = splitCsv(kDefaultImageTypes);
    gTypes.text = defaultText;
    gTypes.images = defaultImages;

    if (exists)
    {
        std::ifstream in(path, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        Json root = Json::parse(data);
        if (root.isObject())
        {
            readList(root, "text", defaultText, gTypes.text);
            readList(root, "image", defaultImages, gTypes.images);
            gTypes.loaded = true;
        }
        else
        {
            log::writef("attachments.json invalid, using defaults: %s",
                        pathText.c_str());
        }
        gTypes.mtime = mtime;
        gTypes.size = size;
    }
    else
    {
        gTypes.loaded = true;
    }

    return gTypes;
}

bool hasExt(const std::vector<std::string>& list, const std::string& ext)
{
    return std::find(list.begin(), list.end(), ext) != list.end();
}

std::string safeId(const std::string& id)
{
    std::string out;
    out.reserve(id.size());
    for (char c : id)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out += c;
    }
    return out.empty() ? "session" : out;
}

std::string safeFileName(const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        unsigned char u = (unsigned char)c;
        if (u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
            continue;
        out += c;
    }
    out = trim(out);
    if (out.empty())
        out = "file";
    if (utf8Length(out) > 60)
        out = utf8Prefix(out, 60);
    return out;
}

std::string imageMime(const std::string& ext)
{
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "webp") return "image/webp";
    if (ext == "gif")  return "image/gif";
    if (ext == "bmp")  return "image/bmp";
    if (ext == "tif" || ext == "tiff") return "image/tiff";
    return "image/" + ext;
}

std::string encodeBase64(const std::string& bytes)
{
    std::size_t outLen = (bytes.size() + 2) / 3 * 4;
    std::string encoded(outLen + 1, '\0');
    std::size_t written = 0;
    base64_encode(bytes.data(), bytes.size(), encoded.data(), &written, 0);
    encoded.resize(written);
    return encoded;
}

std::string extensionForMime(const std::string& mime)
{
    if (mime == "image/png") return "png";
    if (mime == "image/jpeg") return "jpg";
    if (mime == "image/webp") return "webp";
    if (mime == "image/gif") return "gif";
    if (mime == "image/bmp") return "bmp";
    if (mime == "image/tiff") return "tif";
    return "img";
}

//Rejects malformed UTF-8 and embedded NUL bytes.
bool validUtf8(const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = (unsigned char)s[i];
        if (c == 0)
            return false;
        if (c < 0x80)
        {
            ++i;
            continue;
        }

        int need = 0;
        unsigned int cp = 0;
        if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else return false;

        if (i + (std::size_t)need >= s.size())
            return false;

        for (int k = 1; k <= need; ++k)
        {
            unsigned char cc = (unsigned char)s[i + (std::size_t)k];
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }

        if (need == 1 && cp < 0x80) return false;
        if (need == 2 && cp < 0x800) return false;
        if (need == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;

        i += (std::size_t)need + 1;
    }
    return true;
}

bool readAll(const std::filesystem::path& p, std::string& out,
             std::string& err)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
    {
        err = std::string(tr().attachInvalid) + " " + paths::toUtf8(p);
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof())
    {
        err = std::string(tr().attachInvalid) + " " + paths::toUtf8(p);
        return false;
    }
    return true;
}

} //namespace

void ensureConfig()
{
    (void)typeConfig();
}

bool inspect(const std::filesystem::path& source, bool imageAllowed,
             Attachment& out, std::string& err)
{
    err.clear();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec) || ec)
    {
        err = tr().attachInvalid;
        return false;
    }

    uint64_t size = std::filesystem::file_size(source, ec);
    if (ec)
    {
        err = tr().attachInvalid;
        return false;
    }

    std::string name = paths::toUtf8(source.filename());
    if (name.empty())
        name = paths::toUtf8(source);

    std::string ext = normalizeExt(paths::toUtf8(source.extension()));
    const TypeConfig& types = typeConfig();

    Attachment a;
    a.name = name;
    a.size = size;
    a.path = paths::toUtf8(source);

    if (hasExt(types.images, ext))
    {
        if (!imageAllowed)
        {
            err = tr().attachImageUnsupported;
            return false;
        }
        if (size > kMaxImageFile)
        {
            err = std::string(tr().attachTooLarge) + " (" +
                  std::string(tr().attachImage) + " " + formatBytes(size) +
                  " > " + formatBytes(kMaxImageFile) + ")";
            return false;
        }
        a.kind = "image";
        a.mime = imageMime(ext);
        out = std::move(a);
        return true;
    }

    if (hasExt(types.text, ext))
    {
        if (size > kMaxTextFile)
        {
            err = std::string(tr().attachTooLarge) + " (" +
                  std::string(tr().attachText) + " " + formatBytes(size) +
                  " > " + formatBytes(kMaxTextFile) + ")";
            return false;
        }

        std::string content;
        if (!readAll(source, content, err))
            return false;

        if (!validUtf8(content))
        {
            err = tr().attachTextEncoding;
            return false;
        }

        a.kind = "text";
        a.mime = "text/plain";
        out = std::move(a);
        return true;
    }

    err = std::string(tr().attachUnsupported) + (ext.empty() ? "" : " ." + ext);
    return false;
}

bool importFile(const std::filesystem::path& source, const Attachment& info,
                const std::filesystem::path& sessionsRoot,
                const std::string& sessionId, Attachment& out,
                std::string& err)
{
    err.clear();

    std::string sid = safeId(sessionId);
    std::filesystem::path dir =
        sessionsRoot / "attachments" / paths::fromUtf8(sid);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        err = "无法创建附件目录：" + paths::toUtf8(dir);
        return false;
    }

    std::string fileName = makeId() + "-" + safeFileName(info.name);
    std::filesystem::path dest = dir / paths::fromUtf8(fileName);

    std::filesystem::copy_file(source, dest,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec)
    {
        err = "无法复制附件：" + paths::toUtf8(source) + " (" + ec.message() + ")";
        return false;
    }

    out = info;
    out.path = "attachments/" + sid + "/" + fileName;
    out.size = std::filesystem::file_size(dest, ec);
    if (ec)
        out.size = info.size;
    return true;
}

void removeSession(const std::filesystem::path& sessionsRoot,
                   const std::string& sessionId)
{
    std::error_code ec;
    std::filesystem::remove_all(
        sessionsRoot / "attachments" / paths::fromUtf8(safeId(sessionId)), ec);
}

bool isImage(const Attachment& a)
{
    return a.kind == "image";
}

bool isImageExtension(const std::filesystem::path& path)
{
    std::string ext = normalizeExt(paths::toUtf8(path.extension()));
    return hasExt(typeConfig().images, ext);
}

bool loadInlineImage(const std::filesystem::path& source, Attachment& out,
                     std::string& err)
{
    Attachment a;
    if (!inspect(source, true, a, err))
        return false;
    if (!isImage(a))
    {
        err = tr().attachInvalid;
        return false;
    }

    std::string bytes;
    if (!readAll(source, bytes, err))
        return false;

    a.data = encodeBase64(bytes);
    a.path.clear();
    out = std::move(a);
    return true;
}

std::filesystem::path storedPath(const std::filesystem::path& sessionsRoot,
                                 const Attachment& a)
{
    return sessionsRoot / paths::fromUtf8(a.path);
}

bool readText(const std::filesystem::path& sessionsRoot, const Attachment& a,
              std::string& out, std::string& err)
{
    out.clear();
    err.clear();

    if (!readAll(storedPath(sessionsRoot, a), out, err))
        return false;

    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

std::string imageDataUrl(const std::filesystem::path& sessionsRoot,
                         const Attachment& a, std::string& err)
{
    err.clear();

    std::string mime = a.mime.empty() ? "image/png" : a.mime;
    if (!a.data.empty())
        return "data:" + mime + ";base64," + a.data;

    std::string bytes;
    if (!readAll(storedPath(sessionsRoot, a), bytes, err))
        return {};

    return "data:" + mime + ";base64," + encodeBase64(bytes);
}

bool materializeInline(const Attachment& a, std::filesystem::path& out,
                       std::string& err)
{
    err.clear();
    if (a.data.empty())
    {
        err = tr().attachInvalid;
        return false;
    }

    std::size_t outLen = a.data.size() / 4 * 3 + 3;
    std::string bytes(outLen, '\0');
    std::size_t written = 0;
    if (base64_decode(a.data.data(), a.data.size(), bytes.data(), &written, 0) == 0)
    {
        err = tr().attachInvalid;
        return false;
    }
    bytes.resize(written);

    std::error_code ec;
    std::filesystem::path dir =
        std::filesystem::temp_directory_path(ec) / "sdlagent-inline";
    if (ec)
        dir = paths::prefDir() / "inline";
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        err = "无法创建临时目录：" + paths::toUtf8(dir);
        return false;
    }

    std::string ext = paths::toUtf8(paths::fromUtf8(a.name).extension());
    if (!ext.empty() && ext[0] == '.')
        ext.erase(ext.begin());
    ext = normalizeExt(ext);
    if (ext.empty())
        ext = extensionForMime(a.mime);

    std::filesystem::path file =
        dir / paths::fromUtf8(makeId() + "." + ext);
    std::ofstream fileOut(file, std::ios::binary | std::ios::trunc);
    if (!fileOut)
    {
        err = "无法写入临时图片：" + paths::toUtf8(file);
        return false;
    }
    fileOut.write(bytes.data(), (std::streamsize)bytes.size());
    if (!fileOut)
    {
        err = "写入临时图片失败：" + paths::toUtf8(file);
        return false;
    }

    out = file;
    return true;
}

std::string label(const Attachment& a)
{
    return a.name + " · " + formatBytes(a.size);
}

std::string historyLine(const Attachment& a)
{
    std::string prefix = isImage(a) ? tr().attachImage : tr().attachText;
    return prefix + " · " + label(a);
}

} //namespace sdlagent::attachment
