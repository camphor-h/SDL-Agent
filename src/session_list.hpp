// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "sdlgui.hpp"

#include <functional>
#include <string>
#include <vector>

namespace sdlagent::ui
{

struct SessionListItem
{
    std::string id;
    std::string title;
    bool        pinned = false;
};

//Custom-painted session list with pinned section, hover and context menu.
class SessionList : public sdlgui::Widget
{
public:
    explicit SessionList(sdlgui::Widget* parent = nullptr);

    void setItems(std::vector<SessionListItem> items, const std::string& activeId);
    void setActive(const std::string& id);

    void setThemeColors(sdlgui::Color text, sdlgui::Color muted,
                        sdlgui::Color hover, sdlgui::Color selected,
                        sdlgui::Color selectedBar,
                        sdlgui::Color selectedText, sdlgui::Color border,
                        sdlgui::Color scrollTrack, sdlgui::Color scrollThumb);

    std::function<void(const std::string&)> onSelect;
    std::function<void(const std::string&, sdlgui::ContextMenu&)> onSessionContextMenu;

    void onRender(SDL_Renderer* renderer) override;
    void onMouseWheel(int delta) override;

private:
    struct Row
    {
        enum class Type { Header, Separator, Session };
        Type        type = Type::Session;
        std::string id;
        std::string text;
        bool        pinned = false;
        int         y = 0;
        int         h = 0;
    };

    void rebuild();
    int  rowAt(int localY) const;
    void clampScroll();
    void drawScrollbar(SDL_Renderer* renderer, const sdlgui::Rect& r);

    std::vector<SessionListItem> items_;
    std::vector<Row>             rows_;
    std::string                  activeId_;
    std::string                  contextId_;

    int scrollY_ = 0;
    int contentH_ = 0;
    int hoverRow_ = -1;

    sdlgui::Color textColor_{ 220, 222, 226, 255 };
    sdlgui::Color mutedColor_{ 145, 148, 155, 255 };
    sdlgui::Color hoverColor_{ 40, 43, 50, 255 };
    sdlgui::Color selectedColor_{ 46, 76, 122, 255 };
    sdlgui::Color selectedBarColor_{ 88, 150, 230, 255 };
    sdlgui::Color selectedTextColor_{ 255, 255, 255, 255 };
    sdlgui::Color borderColor_{ 55, 57, 64, 255 };
    sdlgui::Color scrollTrack_{ 30, 31, 35, 255 };
    sdlgui::Color scrollThumb_{ 80, 84, 92, 255 };
};

} //namespace sdlagent::ui
