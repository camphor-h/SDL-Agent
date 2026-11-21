// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace sdlgui { class Font; }

namespace sdlagent
{
struct Config;
}

namespace sdlagent::font
{

struct FontEntry
{
    std::string display;
    std::string path;
    bool        builtin = false;
};

//Initialize SDL3_ttf on first use.
bool initialize(std::string& err);

//Built-in + fonts found in the platform font directories.
const std::vector<FontEntry>& enumerate();

//Pick a CJK-capable system font; empty when none is found.
std::string detectSystemFontPath();

//Best available emoji fallback font for the current platform.
std::string detectEmojiFontPath();

//Load a TTF and wrap it as an sdlgui::Font.
std::shared_ptr<sdlgui::Font> load(const std::string& path, int pointSize,
                                   std::string& err);

//Open and close the font without installing it (used to validate settings).
bool probe(const std::string& path, int pointSize, std::string& err);

//Install the font configured in cfg; falls back to the built-in font.
bool apply(const Config& cfg, std::string& err);

//Release the installed font and shut SDL3_ttf down (call after App).
void shutdown();

} //namespace sdlagent::font
