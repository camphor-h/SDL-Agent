// SPDX-License-Identifier: GPL-3.0-only

#include "session_list.hpp"

#include "i18n.hpp"
#include "ui_widgets.hpp"

#include <algorithm>

namespace sdlagent::ui
{
namespace sg = sdlgui;

namespace
{
constexpr int kPaddingTop = 4;
constexpr int kHeaderMinH = 28;
constexpr int kRowMinH = 38;
constexpr int kSepH = 11;
}

SessionList::SessionList(sg::Widget* parent)
    : sg::Widget(parent)
{
    onMouseMove = [this](sg::MouseEvent& e)
    {
        int row = rowAt(e.y);
        if (row != hoverRow_)
        {
            hoverRow_ = row;
            markDirty();
        }
    };
    onMouseLeave = [this](sg::MouseEvent&)
    {
        if (hoverRow_ != -1)
        {
            hoverRow_ = -1;
            markDirty();
        }
    };
    onClick = [this](sg::MouseEvent& e)
    {
        int row = rowAt(e.y);
        if (row < 0 || row >= (int)rows_.size())
            return;
        const Row& r = rows_[(size_t)row];
        if (r.type == Row::Type::Session && onSelect)
            onSelect(r.id);
    };
    onContextMenu = [this](sg::MouseEvent& e, sg::ContextMenu& menu)
    {
        int row = rowAt(e.y);
        if (row < 0 || row >= (int)rows_.size())
            return;
        const Row& r = rows_[(size_t)row];
        if (r.type != Row::Type::Session || !onSessionContextMenu)
            return;
        contextId_ = r.id;
        onSessionContextMenu(r.id, menu);
    };
}

void SessionList::setItems(std::vector<SessionListItem> items,
                           const std::string& activeId)
{
    items_ = std::move(items);
    activeId_ = activeId;
    rebuild();
}

void SessionList::setActive(const std::string& id)
{
    activeId_ = id;

    for (const Row& row : rows_)
    {
        if (row.type != Row::Type::Session || row.id != id)
            continue;

        int top = row.y;
        int bottom = row.y + row.h;
        if (top < scrollY_)
            scrollY_ = top;
        else if (bottom > scrollY_ + height_)
            scrollY_ = bottom - height_;
        clampScroll();
        break;
    }
    markDirty();
}

void SessionList::setThemeColors(sg::Color text, sg::Color muted,
                                 sg::Color hover, sg::Color selected,
                                 sg::Color selectedBar,
                                 sg::Color selectedText, sg::Color border,
                                 sg::Color scrollTrack, sg::Color scrollThumb)
{
    textColor_ = text;
    mutedColor_ = muted;
    hoverColor_ = hover;
    selectedColor_ = selected;
    selectedBarColor_ = selectedBar;
    selectedTextColor_ = selectedText;
    borderColor_ = border;
    scrollTrack_ = scrollTrack;
    scrollThumb_ = scrollThumb;
    markDirty();
}

void SessionList::rebuild()
{
    rows_.clear();

    std::vector<const SessionListItem*> pinned;
    std::vector<const SessionListItem*> normal;
    for (const auto& it : items_)
        (it.pinned ? pinned : normal).push_back(&it);

    int y = kPaddingTop;
    int lineH = sg::currentFont() ? sg::currentFont()->lineHeight() : 16;
    int headerH = std::max(kHeaderMinH, lineH + 10);
    int rowH = std::max(kRowMinH, lineH + 15);

    auto addHeader = [&](const char* text)
    {
        Row r;
        r.type = Row::Type::Header;
        r.text = text;
        r.y = y;
        r.h = headerH;
        rows_.push_back(std::move(r));
        y += headerH;
    };

    auto addSession = [&](const SessionListItem& item)
    {
        Row r;
        r.type = Row::Type::Session;
        r.id = item.id;
        r.text = item.title.empty() ? tr().untitled : item.title;
        r.pinned = item.pinned;
        r.y = y;
        r.h = rowH;
        rows_.push_back(std::move(r));
        y += rowH;
    };

    if (!pinned.empty())
    {
        addHeader(tr().pinned);
        for (const auto* item : pinned)
            addSession(*item);

        if (!normal.empty())
        {
            Row r;
            r.type = Row::Type::Separator;
            r.y = y;
            r.h = kSepH;
            rows_.push_back(std::move(r));
            y += kSepH;
        }
    }

    for (const auto* item : normal)
        addSession(*item);

    contentH_ = y + kPaddingTop;
    clampScroll();
    markDirty();
}

void SessionList::clampScroll()
{
    int maxScroll = std::max(0, contentH_ - height_);
    if (scrollY_ > maxScroll)
        scrollY_ = maxScroll;
    if (scrollY_ < 0)
        scrollY_ = 0;
}

int SessionList::rowAt(int localY) const
{
    int y = localY + scrollY_;
    for (int i = 0; i < (int)rows_.size(); ++i)
    {
        const Row& r = rows_[(size_t)i];
        if (y >= r.y && y < r.y + r.h)
            return r.type == Row::Type::Session ? i : -1;
    }
    return -1;
}

void SessionList::onMouseWheel(int delta)
{
    int maxScroll = std::max(0, contentH_ - height_);
    if (maxScroll <= 0)
        return;
    scrollY_ = std::max(0, std::min(scrollY_ - delta * 40, maxScroll));
    markDirty();
}

void SessionList::drawScrollbar(SDL_Renderer* renderer, const sg::Rect& r)
{
    if (contentH_ <= height_)
        return;

    const int w = 6;
    int x = r.x + r.w - w;

    fillRect(renderer, { x, r.y, w, r.h }, scrollTrack_);

    float ratio = (float)r.h / (float)contentH_;
    int thumbH = std::max(24, (int)(r.h * ratio));
    int maxScroll = std::max(1, contentH_ - r.h);
    int thumbY = r.y + (int)((float)scrollY_ / (float)maxScroll *
                             (float)(r.h - thumbH));

    fillRect(renderer, { x + 1, thumbY, w - 2, thumbH }, scrollThumb_);
}

void SessionList::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;

    sg::Rect r = absoluteRect();
    SDL_Rect prev{};
    bool hadClip = SDL_GetRenderClipRect(renderer, &prev) &&
                   SDL_RenderClipEnabled(renderer);
    SDL_Rect clip{ r.x, r.y, r.w, r.h };
    if (hadClip)
    {
        SDL_Rect isect;
        SDL_GetRectIntersection(&clip, &prev, &isect);
        clip = isect;
    }
    if (clip.w > 0 && clip.h > 0)
        SDL_SetRenderClipRect(renderer, &clip);

    int lineH = sg::currentFont() ? sg::currentFont()->lineHeight() : 16;

    for (int i = 0; i < (int)rows_.size(); ++i)
    {
        const Row& row = rows_[(size_t)i];
        int y = r.y + row.y - scrollY_;
        if (y + row.h < r.y || y > r.y + r.h)
            continue;

        if (row.type == Row::Type::Header)
        {
            drawClipped(renderer, row.text, r.x + 12,
                        y + (row.h - lineH) / 2,
                        std::max(0, r.w - 24), mutedColor_);
            continue;
        }

        if (row.type == Row::Type::Separator)
        {
            fillRect(renderer, { r.x + 10, y + row.h / 2,
                                 std::max(0, r.w - 20), 1 },
                     borderColor_);
            continue;
        }

        bool active = (row.id == activeId_);
        bool hover = (i == hoverRow_);

        if (active)
        {
            fillRect(renderer, { r.x + 6, y + 1, r.w - 12, row.h - 2 },
                     selectedColor_);
            fillRect(renderer, { r.x + 6, y + 1, 4, row.h - 2 },
                     selectedBarColor_);
        }
        else if (hover)
            fillRect(renderer, { r.x + 6, y + 1, r.w - 12, row.h - 2 },
                     hoverColor_);

        int tx = r.x + 14;
        int maxText = r.w - 28;

        if (row.pinned)
        {
            sg::Color mark = active ? selectedTextColor_
                                    : sg::Color(226, 184, 92, 255);
            drawClipped(renderer, "★", tx, y + (row.h - lineH) / 2, 18, mark);
            tx += 20;
            maxText -= 20;
        }

        sg::Color color = active ? selectedTextColor_ : textColor_;
        std::string title = ellipsize(row.text, std::max(0, maxText));
        drawClipped(renderer, title, tx, y + (row.h - lineH) / 2,
                    std::max(0, maxText), color);
    }

    drawScrollbar(renderer, r);

    SDL_SetRenderClipRect(renderer, hadClip ? &prev : nullptr);
    dirty_ = false;
}

} //namespace sdlagent::ui
