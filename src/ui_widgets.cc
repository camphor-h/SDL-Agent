// SPDX-License-Identifier: GPL-3.0-only

#include "ui_widgets.hpp"

#include "icon_bar_data.inc"

#include <algorithm>
#include <cmath>

namespace sdlagent::ui
{
namespace sg = sdlgui;

void Panel::setColors(sg::Color bg, sg::Color border, int borderWidth)
{
    bg_ = bg;
    border_ = border;
    borderWidth_ = borderWidth;
    markDirty();
}

void Panel::setBackground(sg::Color bg)
{
    bg_ = bg;
    markDirty();
}

void Panel::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;

    sg::Rect r = absoluteRect();
    fillRect(renderer, r, bg_);

    if (borderWidth_ > 0 && border_.a > 0)
    {
        SDL_SetRenderDrawColor(renderer, border_.r, border_.g, border_.b, border_.a);
        for (int i = 0; i < borderWidth_; ++i)
        {
            SDL_FRect fr = { (float)(r.x + i), (float)(r.y + i),
                             (float)(r.w - i * 2), (float)(r.h - i * 2) };
            SDL_RenderRect(renderer, &fr);
        }
    }

    sg::Widget::onRender(renderer);
}

void VLine::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;
    fillRect(renderer, absoluteRect(), color_);
    dirty_ = false;
}

sg::Font* BoldFont::base() const
{
    sg::Font* f = sg::currentFont();
    return (f == this) ? nullptr : f;
}

int BoldFont::lineHeight() const
{
    sg::Font* f = base();
    return f ? f->lineHeight() : 16;
}

int BoldFont::ascent() const
{
    sg::Font* f = base();
    return f ? f->ascent() : 16;
}

int BoldFont::measure(const char* text) const
{
    sg::Font* f = base();
    return f ? f->measure(text) : 0;
}

int BoldFont::measureTo(const char* text, int bytePos) const
{
    sg::Font* f = base();
    return f ? f->measureTo(text, bytePos) : 0;
}

int BoldFont::advance(uint32_t codepoint) const
{
    sg::Font* f = base();
    return f ? f->advance(codepoint) : 8;
}

void BoldFont::draw(SDL_Renderer* renderer, const char* text, int x, int y,
                    sg::Color color)
{
    sg::Font* f = base();
    if (!f)
        return;
    f->draw(renderer, text, x, y, color);
    f->draw(renderer, text, x + 1, y, color);
}

std::shared_ptr<sg::Font> boldFont()
{
    static std::shared_ptr<sg::Font> font = std::make_shared<BoldFont>();
    return font;
}

ChatView::ChatView(sg::Widget* parent)
    : sg::RichTextView(parent)
{
    setPadding(16);
    setLineSpacing(3);
    scrollbarV(true);
    scrollbarH(false);
    setSelectable(true);

    //Right-click opens a context menu; it must not clear the current
    //selection the way a left-button drag does.
    auto baseDown = onMouseDown;
    onMouseDown = [baseDown](sg::MouseEvent& e)
    {
        if (e.button == SDL_BUTTON_RIGHT)
            return;
        if (baseDown)
            baseDown(e);
    };
}

ChatView::~ChatView()
{
    if (barTexture_)
        SDL_DestroyTexture(barTexture_);
}

void ChatView::setEmptyState(const std::string& text)
{
    if (emptyText_ == text)
        return;
    emptyText_ = text;
    markDirty();
}

void ChatView::setEmptyStateColor(sg::Color color)
{
    emptyColor_ = color;
    markDirty();
}

void ChatView::onRender(SDL_Renderer* renderer)
{
    sg::RichTextView::onRender(renderer);

    if (emptyText_.empty() || !blocks().empty())
        return;

    if (!barTexture_)
    {
        SDL_IOStream* io =
            SDL_IOFromConstMem(icon::kBarPng, icon::kBarPngSize);
        SDL_Surface* surface = io ? SDL_LoadPNG_IO(io, true) : nullptr;
        if (!surface)
            return;
        barTexture_ = SDL_CreateTextureFromSurface(renderer, surface);
        barW_ = surface->w;
        barH_ = surface->h;
        SDL_DestroySurface(surface);
        if (!barTexture_)
            return;
    }

    sg::Rect r = absoluteRect();
    if (r.w <= 0 || r.h <= 0 || barW_ <= 0 || barH_ <= 0)
        return;

    //Scale with the view: at most 58% wide and 30% tall.
    float imgW = (float)barW_;
    float imgH = (float)barH_;
    float maxW = r.w * 0.58f;
    float maxH = r.h * 0.30f;
    float scale = std::min(maxW / imgW, maxH / imgH);
    if (scale < 1.0f)
    {
        imgW *= scale;
        imgH *= scale;
    }

    int lineH = sg::currentFont() ? sg::currentFont()->lineHeight() : 16;
    float gap = std::max(12.0f, r.h * 0.03f);
    float groupH = imgH + gap + lineH;

    //Centered horizontally, slightly above the vertical center.
    float top = r.y + (r.h - groupH) * 0.5f - r.h * 0.07f;

    SDL_FRect dst = { r.x + (r.w - imgW) * 0.5f, top, imgW, imgH };
    SDL_RenderTexture(renderer, barTexture_, nullptr, &dst);

    int tw = textWidth(emptyText_);
    int tx = r.x + (r.w - tw) / 2;
    int ty = (int)(top + imgH + gap);
    drawClipped(renderer, emptyText_, tx, ty, std::max(0, r.w - 32),
                emptyColor_);
}

bool ChatView::nearBottom(int slack) const
{
    int maxY = std::max(0, contentH_ - height_);
    return (maxY - scrollY_) <= slack;
}

void ChatView::scrollToBottom()
{
    setScrollOffset(0, std::max(0, contentH_ - height_));
}

ImageWidget::ImageWidget(sg::Widget* parent)
    : sg::Widget(parent)
{
}

ImageWidget::~ImageWidget()
{
    if (texture_)
        SDL_DestroyTexture(texture_);
}

bool ImageWidget::setImage(const void* data, std::size_t size)
{
    if (texture_)
    {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        texW_ = texH_ = 0;
    }
    data_ = static_cast<const unsigned char*>(data);
    size_ = size;
    markDirty();
    return data_ && size_ > 0;
}

void ImageWidget::onRender(SDL_Renderer* renderer)
{
    if (!visible() || !data_ || size_ == 0)
        return;

    if (!texture_)
    {
        SDL_IOStream* io = SDL_IOFromConstMem(data_, size_);
        SDL_Surface* surface = io ? SDL_LoadPNG_IO(io, true) : nullptr;
        if (!surface)
            return;
        texture_ = SDL_CreateTextureFromSurface(renderer, surface);
        texW_ = surface->w;
        texH_ = surface->h;
        SDL_DestroySurface(surface);
        if (!texture_)
            return;
    }

    sg::Rect r = absoluteRect();
    if (r.w <= 0 || r.h <= 0 || texW_ <= 0 || texH_ <= 0)
        return;

    float scale = std::min((float)r.w / (float)texW_,
                           (float)r.h / (float)texH_);
    float w = (float)texW_ * scale;
    float h = (float)texH_ * scale;

    SDL_FRect dst = { (float)r.x + ((float)r.w - w) * 0.5f,
                      (float)r.y + ((float)r.h - h) * 0.5f, w, h };
    SDL_RenderTexture(renderer, texture_, nullptr, &dst);

    dirty_ = false;
}

ProgressBar::ProgressBar(sg::Widget* parent)
    : sg::Widget(parent)
{
}

void ProgressBar::setValue(float value)
{
    value = std::max(0.0f, std::min(1.0f, value));
    if (value_ == value)
        return;
    value_ = value;
    markDirty();
}

void ProgressBar::setColors(sg::Color background, sg::Color fill,
                            sg::Color border)
{
    bg_ = background;
    fill_ = fill;
    border_ = border;
    markDirty();
}

void ProgressBar::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;

    sg::Rect r = absoluteRect();
    fillRect(renderer, r, bg_);

    int fillW = (int)((r.w - 2) * value_);
    if (fillW > 0)
    {
        sg::Rect f{ r.x + 1, r.y + 1, fillW, r.h - 2 };
        fillRect(renderer, f, fill_);
    }

    if (border_.a != 0)
    {
        SDL_SetRenderDrawColor(renderer, border_.r, border_.g, border_.b,
                               border_.a);
        SDL_FRect fr = { (float)r.x, (float)r.y, (float)r.w, (float)r.h };
        SDL_RenderRect(renderer, &fr);
    }
}

ArrowButton::ArrowButton(sg::Widget* parent)
    : sg::Widget(parent)
{
    onMouseEnter = [this](sg::MouseEvent&)
    {
        hover_ = true;
        markDirty();
    };
    onMouseLeave = [this](sg::MouseEvent&)
    {
        hover_ = false;
        pressed_ = false;
        markDirty();
    };
    onMouseDown = [this](sg::MouseEvent&)
    {
        pressed_ = true;
        markDirty();
    };
    onMouseUp = [this](sg::MouseEvent&)
    {
        pressed_ = false;
        markDirty();
    };
}

void ArrowButton::setDirection(bool left)
{
    setDirection(left ? Direction::Left : Direction::Right);
}

void ArrowButton::setDirection(Direction direction)
{
    if (direction_ == direction)
        return;
    direction_ = direction;
    markDirty();
}

void ArrowButton::setColors(sg::Color bg, sg::Color hover, sg::Color pressed,
                            sg::Color arrow, sg::Color border)
{
    bg_ = bg;
    hoverBg_ = hover;
    pressedBg_ = pressed;
    arrow_ = arrow;
    border_ = border;
    markDirty();
}

void ArrowButton::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;

    sg::Rect r = absoluteRect();
    sg::Color bg = pressed_ ? pressedBg_ : (hover_ ? hoverBg_ : bg_);

    fillRect(renderer, r, bg);

    if (border_.a != 0)
    {
        SDL_SetRenderDrawColor(renderer, border_.r, border_.g, border_.b,
                               border_.a);
        SDL_FRect fr = { (float)r.x, (float)r.y, (float)r.w, (float)r.h };
        SDL_RenderRect(renderer, &fr);
    }

    float cx = (float)r.x + r.w * 0.5f;
    float cy = (float)r.y + r.h * 0.5f;
    float s = std::min(r.w, r.h) * 0.22f;
    if (s < 3.0f)
        s = 3.0f;

    SDL_SetRenderDrawColor(renderer, arrow_.r, arrow_.g, arrow_.b, arrow_.a);

    if (direction_ == Direction::Left || direction_ == Direction::Right)
    {
        bool left = direction_ == Direction::Left;
        float tipX = left ? cx - s : cx + s;
        float armX = left ? cx + s : cx - s;
        for (int offset = -1; offset <= 1; ++offset)
        {
            float oy = (float)offset;
            SDL_RenderLine(renderer, armX, cy - s + oy, tipX, cy + oy);
            SDL_RenderLine(renderer, tipX, cy + oy, armX, cy + s + oy);
        }
    }
    else
    {
        bool up = direction_ == Direction::Up;
        float tipY = up ? cy - s : cy + s;
        float armY = up ? cy + s : cy - s;
        for (int offset = -1; offset <= 1; ++offset)
        {
            float ox = (float)offset;
            SDL_RenderLine(renderer, cx - s + ox, armY, cx + ox, tipY);
            SDL_RenderLine(renderer, cx + ox, tipY, cx + s + ox, armY);
        }
    }

    dirty_ = false;
}

GlyphButton::GlyphButton(sg::Widget* parent)
    : sg::Widget(parent)
{
    onMouseEnter = [this](sg::MouseEvent&)
    {
        hover_ = true;
        markDirty();
    };
    onMouseLeave = [this](sg::MouseEvent&)
    {
        hover_ = false;
        pressed_ = false;
        markDirty();
    };
    onMouseDown = [this](sg::MouseEvent&)
    {
        pressed_ = true;
        markDirty();
    };
    onMouseUp = [this](sg::MouseEvent&)
    {
        pressed_ = false;
        markDirty();
    };
}

void GlyphButton::setGlyph(Glyph glyph)
{
    glyph_ = glyph;
    markDirty();
}

void GlyphButton::setColors(sg::Color bg, sg::Color hover, sg::Color pressed,
                            sg::Color glyph, sg::Color border)
{
    bg_ = bg;
    hoverBg_ = hover;
    pressedBg_ = pressed;
    glyphColor_ = glyph;
    border_ = border;
    markDirty();
}

void GlyphButton::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;

    sg::Rect r = absoluteRect();
    sg::Color bg = pressed_ ? pressedBg_ : (hover_ ? hoverBg_ : bg_);
    fillRect(renderer, r, bg);

    if (border_.a != 0)
    {
        SDL_SetRenderDrawColor(renderer, border_.r, border_.g, border_.b,
                               border_.a);
        SDL_FRect fr = { (float)r.x, (float)r.y, (float)r.w, (float)r.h };
        SDL_RenderRect(renderer, &fr);
    }

    SDL_SetRenderDrawColor(renderer, glyphColor_.r, glyphColor_.g,
                           glyphColor_.b, glyphColor_.a);

    float cx = (float)r.x + r.w * 0.5f;
    float cy = (float)r.y + r.h * 0.5f;
    float s = std::min(r.w, r.h) * 0.20f;
    if (s < 4.0f)
        s = 4.0f;

    if (glyph_ == Glyph::Plus)
    {
        for (int offset = -1; offset <= 1; ++offset)
        {
            float o = (float)offset;
            SDL_RenderLine(renderer, cx - s, cy + o, cx + s, cy + o);
            SDL_RenderLine(renderer, cx + o, cy - s, cx + o, cy + s);
        }
        dirty_ = false;
        return;
    }

    if (glyph_ == Glyph::Camera)
    {
        //Geometry from a 24x24 outline icon:
        //body M3 8 H7 L9 4 H15 L17 8 H21 V19 H3 Z, lens at (12,13.5) r=3.8
        float box = std::min((float)r.w, (float)r.h) * 0.72f;
        float u = box / 24.0f;
        float ox = cx - 12.0f * u;
        float oy = cy - 12.0f * u;

        auto px = [&](float v) { return ox + v * u; };
        auto py = [&](float v) { return oy + v * u; };
        auto line = [&](float x1, float y1, float x2, float y2)
        {
            SDL_RenderLine(renderer, px(x1), py(y1), px(x2), py(y2));
        };

        line(3, 8, 7, 8);
        line(7, 8, 9, 4);
        line(9, 4, 15, 4);
        line(15, 4, 17, 8);
        line(17, 8, 21, 8);
        line(21, 8, 21, 19);
        line(21, 19, 3, 19);
        line(3, 19, 3, 8);

        float lx = px(12);
        float ly = py(13.5f);
        float lr = 3.8f * u;
        float prevX = lx + lr;
        float prevY = ly;
        for (int i = 1; i <= 20; ++i)
        {
            float a = (float)i * 6.2831853f / 20.0f;
            float nx = lx + std::cos(a) * lr;
            float ny = ly + std::sin(a) * lr;
            SDL_RenderLine(renderer, prevX, prevY, nx, ny);
            prevX = nx;
            prevY = ny;
        }

        dirty_ = false;
        return;
    }

    //Return symbol: up the right side, across the bottom, arrow at the left.
    float tipX = cx - s * 1.15f;
    float tailX = cx + s * 0.85f;
    float topY = cy - s * 0.95f;
    float botY = cy + s * 0.35f;
    float head = s * 0.65f;

    for (int offset = -1; offset <= 1; ++offset)
    {
        float o = (float)offset;
        SDL_RenderLine(renderer, tailX + o, topY, tailX + o, botY);
        SDL_RenderLine(renderer, tailX, botY + o, tipX, botY + o);
        SDL_RenderLine(renderer, tipX + o, botY, tipX + head, botY - head);
        SDL_RenderLine(renderer, tipX + o, botY, tipX + head, botY + head);
    }

    dirty_ = false;
}

Modal::Modal(sg::Widget* parent)
    : sg::Widget(parent)
{
}

void Modal::setResponsivePanel(bool on, int marginX, int marginY, int minW,
                               int minH, int maxW, int maxH)
{
    responsive_ = on;
    marginX_ = marginX;
    marginY_ = marginY;
    minW_ = minW;
    minH_ = minH;
    maxW_ = maxW;
    maxH_ = maxH;
    relayout();
}

void Modal::showModal()
{
    setVisible(true);
    relayout();
    markDirty();
    if (getWindow())
        getWindow()->requestRedraw();
}

void Modal::hideModal()
{
    setVisible(false);
    markDirty();
    if (getWindow())
        getWindow()->requestRedraw();
}

void Modal::layoutChildren()
{
    if (panel_)
    {
        if (responsive_)
        {
            int pw = std::min(maxW_, std::max(minW_, width_ - marginX_ * 2));
            int ph = std::min(maxH_, std::max(minH_, height_ - marginY_ * 2));
            if (panel_->width() != pw || panel_->height() != ph)
            {
                panel_->setSize(pw, ph);
                if (onPanelSize)
                    onPanelSize(pw, ph);
            }
        }

        int pw = panel_->width();
        int ph = panel_->height();
        int x = std::max(0, (width_ - pw) / 2);
        int y = std::max(0, (height_ - ph) / 2);
        panel_->setPosition(x, y);
    }
    sg::Widget::layoutChildren();
}

void Modal::onRender(SDL_Renderer* renderer)
{
    if (!visible())
        return;

    fillRect(renderer, absoluteRect(), overlay_);
    sg::Widget::onRender(renderer);
}

int textWidth(const std::string& text)
{
    sdlgui::Font* font = sdlgui::currentFont();
    return font ? font->measure(text.c_str()) : 0;
}

namespace
{

uint32_t decodeUtf8(const char*& p)
{
    const unsigned char* s = reinterpret_cast<const unsigned char*>(p);
    uint32_t cp = 0;
    int len = 1;

    if (s[0] < 0x80)
    {
        cp = s[0];
        len = 1;
    }
    else if ((s[0] & 0xE0) == 0xC0)
    {
        cp = s[0] & 0x1F;
        len = 2;
    }
    else if ((s[0] & 0xF0) == 0xE0)
    {
        cp = s[0] & 0x0F;
        len = 3;
    }
    else if ((s[0] & 0xF8) == 0xF0)
    {
        cp = s[0] & 0x07;
        len = 4;
    }
    else
    {
        p += 1;
        return 0xFFFD;
    }

    for (int i = 1; i < len; ++i)
    {
        if ((s[i] & 0xC0) != 0x80)
        {
            p += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }

    p += len;
    return cp;
}

} //namespace

std::string ellipsize(const std::string& text, int maxWidth)
{
    if (maxWidth <= 0)
        return {};
    sdlgui::Font* font = sdlgui::currentFont();
    if (!font || font->measure(text.c_str()) <= maxWidth)
        return text;

    const std::string dots = "…";
    int dotsW = font->measure(dots.c_str());
    if (dotsW >= maxWidth)
        return {};

    std::string out;
    const char* p = text.c_str();
    int w = 0;
    while (*p)
    {
        const char* prev = p;
        uint32_t cp = decodeUtf8(p);
        int adv = font->advance(cp);
        if (w + adv + dotsW > maxWidth)
            break;
        out.append(prev, (std::size_t)(p - prev));
        w += adv;
    }
    out += dots;
    return out;
}

void drawClipped(SDL_Renderer* renderer, const std::string& text, int x, int y,
                 int maxWidth, sg::Color color)
{
    if (!renderer || text.empty() || maxWidth <= 0)
        return;

    sdlgui::Font* font = sdlgui::currentFont();
    if (!font)
        return;

    SDL_Rect want{ x, y, maxWidth, font->lineHeight() + 2 };
    SDL_Rect prev{};
    bool hadClip = SDL_GetRenderClipRect(renderer, &prev) &&
                   SDL_RenderClipEnabled(renderer);

    if (hadClip)
    {
        SDL_Rect isect;
        SDL_GetRectIntersection(&want, &prev, &isect);
        want = isect;
        if (want.w <= 0 || want.h <= 0)
            return;
    }

    SDL_SetRenderClipRect(renderer, &want);
    font->draw(renderer, text.c_str(), x, y, color);
    SDL_SetRenderClipRect(renderer, hadClip ? &prev : nullptr);
}

void fillRect(SDL_Renderer* renderer, const sg::Rect& r, sg::Color color)
{
    if (!renderer || color.a == 0 || r.w <= 0 || r.h <= 0)
        return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_FRect fr = { (float)r.x, (float)r.y, (float)r.w, (float)r.h };
    SDL_RenderFillRect(renderer, &fr);
}

} //namespace sdlagent::ui
