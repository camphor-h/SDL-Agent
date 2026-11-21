// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "sdlgui.hpp"

#include <memory>
#include <functional>
#include <string>

namespace sdlagent::ui
{

//filled rectangle with an optional border
class Panel : public sdlgui::Widget
{
public:
    using sdlgui::Widget::Widget;

    void setColors(sdlgui::Color bg, sdlgui::Color border = sdlgui::Color(0, 0, 0, 0),
                   int borderWidth = 0);
    void setBackground(sdlgui::Color bg);

    void onRender(SDL_Renderer* renderer) override;

private:
    sdlgui::Color bg_{ 30, 31, 35, 255 };
    sdlgui::Color border_{ 0, 0, 0, 0 };
    int           borderWidth_ = 0;
};

//thin separator line
class VLine : public sdlgui::Widget
{
public:
    using sdlgui::Widget::Widget;

    void setColor(sdlgui::Color c) { color_ = c; markDirty(); }
    void onRender(SDL_Renderer* renderer) override;

private:
    sdlgui::Color color_{ 55, 57, 64, 255 };
};

//Fake-bold by drawing the base font twice with a 1px offset.
class BoldFont : public sdlgui::Font
{
public:
    int  lineHeight() const override;
    int  ascent() const override;
    int  measure(const char* text) const override;
    int  measureTo(const char* text, int bytePos) const override;
    int  advance(uint32_t codepoint) const override;
    void draw(SDL_Renderer* renderer, const char* text, int x, int y,
              sdlgui::Color color) override;

private:
    sdlgui::Font* base() const;
};

class ChatView : public sdlgui::RichTextView
{
public:
    explicit ChatView(sdlgui::Widget* parent = nullptr);

    bool nearBottom(int slack = 56) const;
    void scrollToBottom();
    int  contentHeight() const { return contentH_; }
};

//Small self-drawn arrow button (no font glyphs involved).
//Simple horizontal progress bar (0..1).
class ProgressBar : public sdlgui::Widget
{
public:
    explicit ProgressBar(sdlgui::Widget* parent = nullptr);

    void  setValue(float value);
    float value() const { return value_; }
    void  setColors(sdlgui::Color background, sdlgui::Color fill,
                    sdlgui::Color border);

    void onRender(SDL_Renderer* renderer) override;

private:
    float value_ = 0.0f;
    sdlgui::Color bg_{ 30, 31, 35, 255 };
    sdlgui::Color fill_{ 80, 130, 200, 255 };
    sdlgui::Color border_{ 80, 82, 90, 255 };
};

class ArrowButton : public sdlgui::Widget
{
public:
    explicit ArrowButton(sdlgui::Widget* parent = nullptr);

    void setDirection(bool left);
    bool left() const { return left_; }

    void setColors(sdlgui::Color bg, sdlgui::Color hover,
                   sdlgui::Color pressed, sdlgui::Color arrow,
                   sdlgui::Color border = sdlgui::Color(0, 0, 0, 0));

    void onRender(SDL_Renderer* renderer) override;

private:
    bool  left_ = true;
    bool  hover_ = false;
    bool  pressed_ = false;
    sdlgui::Color bg_{ 30, 31, 35, 255 };
    sdlgui::Color hoverBg_{ 45, 47, 53, 255 };
    sdlgui::Color pressedBg_{ 38, 40, 45, 255 };
    sdlgui::Color arrow_{ 200, 205, 215, 255 };
    sdlgui::Color border_{ 0, 0, 0, 0 };
};

//full-window dimming layer that centers one fixed-size panel
class Modal : public sdlgui::Widget
{
public:
    explicit Modal(sdlgui::Widget* parent = nullptr);

    void setPanel(sdlgui::Widget* panel) { panel_ = panel; }
    void setOverlayColor(sdlgui::Color c) { overlay_ = c; }
    void setResponsivePanel(bool on, int marginX = 40, int marginY = 40,
                            int minW = 600, int minH = 480,
                            int maxW = 1200, int maxH = 820);
    void showModal();
    void hideModal();
    bool isOpen() const { return visible(); }

    std::function<void(int, int)> onPanelSize;

    void onRender(SDL_Renderer* renderer) override;
    void layoutChildren() override;

private:
    sdlgui::Widget* panel_ = nullptr;
    sdlgui::Color   overlay_{ 0, 0, 0, 64 };
    bool  responsive_ = false;
    int   marginX_ = 40, marginY_ = 40;
    int   minW_ = 600, minH_ = 480;
    int   maxW_ = 1200, maxH_ = 820;
};

//helpers
int         textWidth(const std::string& text);
std::string ellipsize(const std::string& text, int maxWidth);
void        drawClipped(SDL_Renderer* renderer, const std::string& text,
                        int x, int y, int maxWidth, sdlgui::Color color);
void        fillRect(SDL_Renderer* renderer, const sdlgui::Rect& r,
                     sdlgui::Color color);

std::shared_ptr<sdlgui::Font> boldFont();

} //namespace sdlagent::ui
