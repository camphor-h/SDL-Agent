// SPDX-License-Identifier: GPL-3.0-only

#include "font.hpp"

#include "config.hpp"
#include "paths.hpp"
#include "sdlgui.hpp"
#include "util.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sdlagent::font
{
namespace
{

bool gTtfInit = false;

class TtfFont : public sdlgui::Font
{
public:
    TtfFont(TTF_Font* font, TTF_Font* emoji, int pointSize)
        : font_(font)
        , emoji_(emoji)
        , pointSize_(pointSize)
    {
        lineHeight_ = TTF_GetFontLineSkip(font_);
        if (lineHeight_ <= 0)
            lineHeight_ = TTF_GetFontHeight(font_);
        if (lineHeight_ <= 0)
            lineHeight_ = pointSize_;

        ascent_ = TTF_GetFontAscent(font_);
        if (ascent_ < 0)
            ascent_ = lineHeight_;

        if (emoji_)
        {
            int eh = TTF_GetFontLineSkip(emoji_);
            int ea = TTF_GetFontAscent(emoji_);
            if (eh > lineHeight_)
                lineHeight_ = eh;
            if (ea > ascent_)
                ascent_ = ea;

            //Color emoji must not be tinted by the requested text color.
            static const Uint32 probes[] = { 0x1F600, 0x1F642, 0x2764, 0x2600 };
            for (Uint32 cp : probes)
            {
                TTF_ImageType type = TTF_IMAGE_INVALID;
                SDL_Surface* s = TTF_GetGlyphImage(emoji_, cp, &type);
                if (!s)
                    continue;
                emojiColor_ = (type == TTF_IMAGE_COLOR);
                SDL_DestroySurface(s);
                break;
            }
        }
    }

    ~TtfFont() override
    {
        clearCache();
        if (font_)
            TTF_CloseFont(font_);
        if (emoji_)
            TTF_CloseFont(emoji_);
    }

    int lineHeight() const override { return lineHeight_; }
    int ascent() const override { return ascent_; }

    int measure(const char* text) const override
    {
        if (!text || !*text)
            return 0;
        return measureRange(text, std::strlen(text));
    }

    int measureTo(const char* text, int bytePos) const override
    {
        if (!text || bytePos <= 0)
            return 0;
        size_t len = std::strlen(text);
        if ((size_t)bytePos < len)
            len = (size_t)bytePos;
        return measureRange(text, len);
    }

    int advance(uint32_t codepoint) const override
    {
        TTF_Font* f = fontFor(codepoint);
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        if (TTF_GetGlyphMetrics(f, (Uint32)codepoint, &minx, &maxx, &miny,
                                &maxy, &adv))
            return adv;
        return std::max(1, pointSize_ / 2);
    }

    void draw(SDL_Renderer* renderer, const char* text, int x, int y,
              sdlgui::Color color) override
    {
        if (!renderer || !text || !*text)
            return;

        std::vector<Segment> segments =
            split(text, std::strlen(text));

        int penX = x;
        for (const Segment& seg : segments)
        {
            if (seg.len == 0)
                continue;

            TextureKey key{ renderer, std::string(seg.text, seg.len),
                            seg.emoji ? emoji_ : font_ };
            auto it = cache_.find(key);

            if (it == cache_.end())
            {
                if (cache_.size() >= 1024)
                    clearCache();

                SDL_Color white{ 255, 255, 255, 255 };
                SDL_Surface* surface = TTF_RenderText_Blended(
                    key.font, seg.text, seg.len, white);
                if (!surface)
                    continue;

                SDL_Texture* texture =
                    SDL_CreateTextureFromSurface(renderer, surface);
                Entry entry;
                entry.texture = texture;
                entry.w = surface->w;
                entry.h = surface->h;
                SDL_DestroySurface(surface);

                if (!texture)
                    continue;
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                it = cache_.emplace(key, entry).first;
            }

            SDL_Texture* texture = it->second.texture;
            if (!texture)
                continue;

            bool colorGlyph = seg.emoji && emojiColor_;
            if (colorGlyph)
            {
                SDL_SetTextureColorMod(texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(texture, color.a);
            }
            else
            {
                SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(texture, color.a);
            }

            int dy = y;
            if (it->second.h < lineHeight_)
                dy = y + (lineHeight_ - it->second.h) / 2;

            SDL_FRect dst = { (float)penX, (float)dy,
                              (float)it->second.w, (float)it->second.h };
            SDL_RenderTexture(renderer, texture, nullptr, &dst);

            penX += it->second.w;
        }
    }

private:
    struct Segment
    {
        const char* text = nullptr;
        size_t      len = 0;
        bool        emoji = false;
    };

    struct TextureKey
    {
        SDL_Renderer* renderer = nullptr;
        std::string   text;
        TTF_Font*     font = nullptr;

        bool operator==(const TextureKey& o) const
        {
            return renderer == o.renderer && font == o.font && text == o.text;
        }
    };

    struct TextureKeyHash
    {
        std::size_t operator()(const TextureKey& k) const
        {
            std::size_t h = std::hash<std::string>()(k.text);
            h ^= std::hash<SDL_Renderer*>()(k.renderer) + 0x9e3779b9u;
            h ^= std::hash<TTF_Font*>()(k.font) + 0x9e3779b9u;
            return h;
        }
    };

    struct Entry
    {
        SDL_Texture* texture = nullptr;
        int          w = 0;
        int          h = 0;
    };

    TTF_Font* font_ = nullptr;
    TTF_Font* emoji_ = nullptr;
    bool      emojiColor_ = false;
    int       pointSize_ = 16;
    int       lineHeight_ = 16;
    int       ascent_ = 16;
    std::unordered_map<TextureKey, Entry, TextureKeyHash> cache_;

    static bool hasGlyph(TTF_Font* f, uint32_t cp)
    {
        return f && TTF_FontHasGlyph(f, (Uint32)cp);
    }

    TTF_Font* fontFor(uint32_t cp) const
    {
        if (hasGlyph(font_, cp))
            return font_;
        if (hasGlyph(emoji_, cp))
            return emoji_;
        return font_;
    }

    static uint32_t decodeUtf8(const char*& p, const char* end)
    {
        if (p >= end)
            return 0;

        unsigned char c = (unsigned char)*p;
        if (c < 0x80)
        {
            ++p;
            return c;
        }

        int extra = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0)
        {
            extra = 1;
            cp = c & 0x1F;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            extra = 2;
            cp = c & 0x0F;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            extra = 3;
            cp = c & 0x07;
        }
        else
        {
            ++p;
            return 0xFFFD;
        }

        if (p + extra >= end)
        {
            ++p;
            return 0xFFFD;
        }

        for (int i = 1; i <= extra; ++i)
        {
            unsigned char cc = (unsigned char)p[i];
            if ((cc & 0xC0) != 0x80)
            {
                ++p;
                return 0xFFFD;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }

        p += extra + 1;
        return cp;
    }

    std::vector<Segment> split(const char* text, size_t len) const
    {
        std::vector<Segment> out;
        if (!text || len == 0)
            return out;

        if (!emoji_)
        {
            out.push_back({ text, len, false });
            return out;
        }

        const char* p = text;
        const char* end = text + len;

        while (p < end)
        {
            const char* start = p;
            uint32_t cp = decodeUtf8(p, end);
            bool useEmoji = !hasGlyph(font_, cp) && hasGlyph(emoji_, cp);

            if (!out.empty() && out.back().emoji == useEmoji)
            {
                out.back().len = (size_t)(p - out.back().text);
            }
            else
            {
                out.push_back({ start, (size_t)(p - start), useEmoji });
            }
        }
        return out;
    }

    int measureRange(const char* text, size_t len) const
    {
        int total = 0;
        for (const Segment& seg : split(text, len))
        {
            if (!seg.text || seg.len == 0)
                continue;
            TTF_Font* f = seg.emoji ? emoji_ : font_;
            int w = 0, h = 0;
            if (TTF_GetStringSize(f, seg.text, seg.len, &w, &h))
                total += w;
        }
        return total;
    }

    void clearCache()
    {
        for (auto& kv : cache_)
            if (kv.second.texture)
                SDL_DestroyTexture(kv.second.texture);
        cache_.clear();
    }
};

} //namespace

const std::vector<FontEntry>& enumerate()
{
    static std::vector<FontEntry> entries;
    static bool done = false;
    if (done)
        return entries;
    done = true;

    std::string err;
    if (!initialize(err))
        return entries;

    std::vector<std::string> dirs;
#ifdef _WIN32
    const char* windir = SDL_getenv("WINDIR");
    std::string winFonts = (windir && *windir) ? windir : "C:\\Windows";
    dirs.push_back(winFonts + "\\Fonts");
    const char* local = SDL_getenv("LOCALAPPDATA");
    if (local && *local)
        dirs.push_back(std::string(local) + "\\Microsoft\\Windows\\Fonts");
#elif defined(__APPLE__)
    dirs.push_back("/System/Library/Fonts");
    dirs.push_back("/Library/Fonts");
    const char* home = SDL_getenv("HOME");
    if (home && *home)
        dirs.push_back(std::string(home) + "/Library/Fonts");
#else
    dirs.push_back("/usr/share/fonts");
    dirs.push_back("/usr/local/share/fonts");
    const char* home = SDL_getenv("HOME");
    if (home && *home)
    {
        dirs.push_back(std::string(home) + "/.fonts");
        dirs.push_back(std::string(home) + "/.local/share/fonts");
    }
#endif

    std::unordered_set<std::string> seenPaths;
    std::unordered_set<std::string> seenNames;

    for (const std::string& dir : dirs)
    {
        std::error_code ec;
        std::filesystem::path base = paths::fromUtf8(dir);
        if (!std::filesystem::exists(base, ec))
            continue;

        std::filesystem::recursive_directory_iterator it(
            base, std::filesystem::directory_options::skip_permission_denied,
            ec);
        std::filesystem::recursive_directory_iterator end;
        if (ec)
            continue;

        while (it != end)
        {
            if (ec)
            {
                ec.clear();
                it.increment(ec);
                continue;
            }

            std::error_code fec;
            if (it->is_regular_file(fec))
            {
                std::string ext = toLower(paths::toUtf8(it->path().extension()));
                if (ext == ".ttf" || ext == ".ttc" || ext == ".otf" ||
                    ext == ".otc")
                {
                    std::string full = paths::toUtf8(it->path());
                    if (!seenPaths.insert(full).second)
                    {
                        it.increment(ec);
                        continue;
                    }

                    std::string name = paths::toUtf8(it->path().stem());
                    TTF_Font* probeFont = TTF_OpenFont(full.c_str(), 10.0f);
                    if (probeFont)
                    {
                        const char* family = TTF_GetFontFamilyName(probeFont);
                        const char* style = TTF_GetFontStyleName(probeFont);
                        if (family && *family)
                        {
                            name = family;
                            if (style && *style &&
                                toLower(style) != "regular" &&
                                toLower(style) != "normal")
                                name += std::string(" ") + style;
                        }
                        TTF_CloseFont(probeFont);
                    }
                    if (name.empty())
                        name = paths::toUtf8(it->path().filename());

                    std::string key = toLower(name);
                    if (seenNames.insert(key).second)
                        entries.push_back({ name, full, false });
                }
            }
            it.increment(ec);
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const FontEntry& a, const FontEntry& b)
              { return toLower(a.display) < toLower(b.display); });
    return entries;
}

bool initialize(std::string& err)
{
    if (gTtfInit)
        return true;
    if (!TTF_Init())
    {
        const char* e = SDL_GetError();
        err = "TTF_Init 失败";
        if (e && *e)
            err += std::string(": ") + e;
        return false;
    }
    gTtfInit = true;
    return true;
}

std::string detectSystemFontPath()
{
    std::string err;
    if (!initialize(err))
        return {};

    std::vector<std::string> candidates;

#ifdef _WIN32
    const char* windir = SDL_getenv("WINDIR");
    std::string base = (windir && *windir) ? windir : "C:\\Windows";
    //Prefer Microsoft YaHei Bold for Simplified Chinese by default.
    candidates.push_back(base + "\\Fonts\\msyhbd.ttc");
    candidates.push_back(base + "\\Fonts\\msyh.ttc");
    candidates.push_back(base + "\\Fonts\\simhei.ttf");
    candidates.push_back(base + "\\Fonts\\simsun.ttc");
    candidates.push_back(base + "\\Fonts\\segoeui.ttf");
#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Fonts/STHeiti Medium.ttc");
    candidates.push_back("/System/Library/Fonts/STHeiti Light.ttc");
    candidates.push_back("/System/Library/Fonts/PingFang.ttc");
    candidates.push_back("/System/Library/Fonts/Helvetica.ttc");
    candidates.push_back("/Library/Fonts/Arial Unicode.ttf");
#else
    candidates.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    candidates.push_back("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");
    candidates.push_back("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
#endif

    for (const auto& path : candidates)
    {
        TTF_Font* probeFont = TTF_OpenFont(path.c_str(), 16.0f);
        if (probeFont)
        {
            TTF_CloseFont(probeFont);
            return path;
        }
    }
    return {};
}

std::string detectEmojiFontPath()
{
    std::string err;
    if (!initialize(err))
        return {};

    std::vector<std::string> candidates;

#ifdef _WIN32
    const char* windir = SDL_getenv("WINDIR");
    std::string base = (windir && *windir) ? windir : "C:\\Windows";
    candidates.push_back(base + "\\Fonts\\seguiemj.ttf");
    const char* local = SDL_getenv("LOCALAPPDATA");
    if (local && *local)
        candidates.push_back(std::string(local) +
                             "\\Microsoft\\Windows\\Fonts\\seguiemj.ttf");
    candidates.push_back(base + "\\Fonts\\seguisym.ttf");
#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Fonts/Apple Color Emoji.ttc");
    candidates.push_back("/System/Library/Fonts/Apple Color Emoji.ttf");
    candidates.push_back("/System/Library/Fonts/Apple Symbols.ttf");
#elif defined(__ANDROID__)
    candidates.push_back("/system/fonts/NotoColorEmoji.ttf");
    candidates.push_back("/system/fonts/NotoEmoji-Regular.ttf");
#else
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf");
    candidates.push_back("/usr/share/fonts/noto/NotoColorEmoji.ttf");
    candidates.push_back("/usr/share/fonts/noto/NotoColorEmoji.ttf");
    candidates.push_back("/usr/share/fonts/truetype/twemoji/TwemojiMozilla.ttf");
    candidates.push_back("/usr/share/fonts/twemoji/TwemojiMozilla.ttf");
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf");
    candidates.push_back("/usr/share/fonts/TTF/Symbola.ttf");
#endif

    for (const auto& path : candidates)
    {
        TTF_Font* probeFont = TTF_OpenFont(path.c_str(), 16.0f);
        if (!probeFont)
            probeFont = TTF_OpenFont(path.c_str(), 32.0f);
        if (probeFont)
        {
            TTF_CloseFont(probeFont);
            return path;
        }
    }
    return {};
}

std::shared_ptr<sdlgui::Font> load(const std::string& path, int pointSize,
                                   std::string& err)
{
    if (!initialize(err))
        return {};

    TTF_Font* f = TTF_OpenFont(path.c_str(), (float)pointSize);
    if (!f)
    {
        const char* e = SDL_GetError();
        err = "无法加载字体: " + path;
        if (e && *e)
            err += std::string(" (") + e + ")";
        return {};
    }

    TTF_Font* emoji = nullptr;
    std::string emojiPath = detectEmojiFontPath();
    if (!emojiPath.empty() && emojiPath != path)
        emoji = TTF_OpenFont(emojiPath.c_str(), (float)pointSize);

    return std::make_shared<TtfFont>(f, emoji, pointSize);
}

bool probe(const std::string& path, int pointSize, std::string& err)
{
    if (!initialize(err))
        return false;

    TTF_Font* f = TTF_OpenFont(path.c_str(), (float)pointSize);
    if (!f)
    {
        const char* e = SDL_GetError();
        err = "无法加载字体: " + path;
        if (e && *e)
            err += std::string(" (") + e + ")";
        return false;
    }
    TTF_CloseFont(f);
    return true;
}

bool apply(const Config& cfg, std::string& err)
{
    //The built-in bitmap font is disabled; always resolve a system font.
    std::string path = trim(cfg.fontPath);
    if (path.empty())
        path = detectSystemFontPath();
    if (path.empty())
    {
        err = "没有找到可用的系统字体，请手动选择字体文件。";
        sdlgui::setFont(nullptr);
        return false;
    }

    int size = cfg.fontPointSize;
    if (size < 10) size = 10;
    if (size > 24) size = 24;

    auto f = load(path, size, err);
    if (!f)
    {
        sdlgui::setFont(nullptr);
        return false;
    }

    sdlgui::setFont(f);
    return true;
}

void shutdown()
{
    sdlgui::setFont(nullptr);
    if (gTtfInit)
    {
        TTF_Quit();
        gTtfInit = false;
    }
}

} //namespace sdlagent::font
