// SPDX-License-Identifier: GPL-3.0-only

#include "config.hpp"

#include "json.hpp"
#include "paths.hpp"
#include "util.hpp"
#include "version.hpp"

#include <cmath>
#include <fstream>

namespace sdlagent
{

bool Config::apiReady() const
{
    return !trim(baseUrl).empty() && !trim(apiKey).empty() &&
           !trim(model).empty();
}

const char* toolModeToString(ToolMode m)
{
    switch (m)
    {
        case ToolMode::Off:  return "off";
        case ToolMode::Auto: return "auto";
        default:             return "ask";
    }
}

ToolMode toolModeFromString(const std::string& s)
{
    if (s == "off")
        return ToolMode::Off;
    if (s == "auto")
        return ToolMode::Auto;
    return ToolMode::Ask;
}

Config loadConfig(const std::filesystem::path& file)
{
    Config cfg;

    std::ifstream in(paths::toUtf8(file), std::ios::binary);
    if (!in)
    {
        if (cfg.sessionsDir.empty())
            cfg.sessionsDir = paths::toUtf8(paths::defaultSessionsDir());
        return cfg;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    Json j = Json::parse(text);
    if (!j.isObject())
        return cfg;

    cfg.baseUrl = j["baseUrl"].str();
    cfg.apiKey = j["apiKey"].str();
    cfg.model = j["model"].str();
    cfg.reasoningEffort = j["reasoningEffort"].str();
    cfg.apiProtocol = j["apiProtocol"].str("openai");
    cfg.dataDir = j["dataDir"].str();
    cfg.sessionsDir = j["sessionsDir"].str();
    cfg.theme = j["theme"].str("light");

    cfg.temperature = j["temperature"].number(cfg.temperature);
    cfg.contextTokens = (int)j["contextTokens"].integer(cfg.contextTokens);
    cfg.maxTokens = (int)j["maxTokens"].integer(cfg.maxTokens);
    cfg.showReasoning = j["showReasoning"].boolean(false);
    cfg.autoTitle = j["autoTitle"].boolean(true);
    cfg.onboarded = j["onboarded"].boolean(false);
    cfg.uiScale = (float)j["uiScale"].number(cfg.uiScale);

    if (j["toolMode"].str("ask") == "off")
        cfg.toolMode = ToolMode::Off;
    else if (j["toolMode"].str("ask") == "auto")
        cfg.toolMode = ToolMode::Auto;
    else
        cfg.toolMode = ToolMode::Ask;

    cfg.lang = (j["lang"].str("zh") == "en") ? Lang::En : Lang::Zh;

    Json font = j["font"];
    if (font.isObject())
    {
        cfg.fontBackend = font["backend"].str("ttf");
        cfg.fontPath = font["path"].str();
        cfg.fontPointSize = (int)font["size"].integer(16);
    }
    //The built-in bitmap font is no longer offered; always use SDL3_ttf.
    cfg.fontBackend = "ttf";
    if (cfg.fontPointSize < 10) cfg.fontPointSize = 10;
    if (cfg.fontPointSize > 24) cfg.fontPointSize = 24;

    Json profiles = j["profiles"];
    if (profiles.isArray())
    {
        for (std::size_t i = 0; i < profiles.size(); ++i)
        {
            Json p = profiles[i];
            if (!p.isObject())
                continue;

            ApiProfile ap;
            ap.id = p["id"].str();
            if (ap.id.empty())
                ap.id = makeId();
            ap.name = p["name"].str();
            ap.baseUrl = p["baseUrl"].str();
            ap.apiKey = p["apiKey"].str();
            ap.model = p["model"].str();
            ap.reasoningEffort = p["reasoningEffort"].str();
            ap.apiProtocol = p["apiProtocol"].str("openai");
            ap.temperature = p["temperature"].number(0.0);
            ap.contextTokens = (int)p["contextTokens"].integer(1048576);
            ap.maxTokens = (int)p["maxTokens"].integer(524288);

            if (ap.temperature < 0.0) ap.temperature = 0.0;
            if (ap.temperature > 2.0) ap.temperature = 2.0;
            ap.temperature = std::round(ap.temperature * 10.0) / 10.0;
            if (ap.contextTokens < 256) ap.contextTokens = 256;
            if (ap.contextTokens > 1048576) ap.contextTokens = 1048576;
            if (ap.maxTokens < 256) ap.maxTokens = 256;
            if (ap.maxTokens > 1048576) ap.maxTokens = 1048576;

            cfg.profiles.push_back(std::move(ap));
        }
    }
    cfg.activeProfileId = j["activeProfile"].str();

    //migrate the old single-profile config
    if (cfg.profiles.empty() &&
        (!trim(cfg.baseUrl).empty() || !trim(cfg.model).empty() ||
         !trim(cfg.apiKey).empty()))
    {
        ApiProfile ap;
        ap.id = "default";
        ap.name = !trim(cfg.model).empty() ? cfg.model : std::string("默认配置");
        ap.baseUrl = cfg.baseUrl;
        ap.apiKey = cfg.apiKey;
        ap.model = cfg.model;
        ap.temperature = cfg.temperature;
        ap.contextTokens = cfg.contextTokens;
        ap.maxTokens = cfg.maxTokens;
        cfg.profiles.push_back(std::move(ap));
        cfg.activeProfileId = "default";
    }

    if (!cfg.profiles.empty())
    {
        ApiProfile* active = nullptr;
        for (auto& p : cfg.profiles)
        {
            if (p.id == cfg.activeProfileId)
            {
                active = &p;
                break;
            }
        }
        if (!active)
        {
            active = &cfg.profiles.front();
            cfg.activeProfileId = active->id;
        }

        cfg.baseUrl = active->baseUrl;
        cfg.apiKey = active->apiKey;
        cfg.model = active->model;
        cfg.temperature = active->temperature;
        cfg.contextTokens = active->contextTokens;
        cfg.maxTokens = active->maxTokens;
    }

    //temperature is no longer configurable
    for (auto& p : cfg.profiles)
        p.temperature = 0.0;
    cfg.temperature = 0.0;

    if (cfg.theme != "dark")
        cfg.theme = "light";
    if (cfg.temperature < 0.0) cfg.temperature = 0.0;
    if (cfg.temperature > 2.0) cfg.temperature = 2.0;
    cfg.temperature = std::round(cfg.temperature * 10.0) / 10.0;
    if (cfg.contextTokens < 256) cfg.contextTokens = 256;
    if (cfg.contextTokens > 1048576) cfg.contextTokens = 1048576;
    if (cfg.maxTokens < 256) cfg.maxTokens = 256;
    if (cfg.maxTokens > 1048576) cfg.maxTokens = 1048576;
    if (cfg.uiScale < 0.75f) cfg.uiScale = 0.75f;
    if (cfg.uiScale > 2.0f) cfg.uiScale = 2.0f;

    //Mobile sandboxes: ignore any custom paths that may come from a
    //desktop config; always fall back to the app defaults.
    if (paths::isMobile())
    {
        cfg.dataDir.clear();
        cfg.sessionsDir.clear();
    }

    if (cfg.sessionsDir.empty())
        cfg.sessionsDir = paths::toUtf8(paths::defaultSessionsDir());

    return cfg;
}

bool saveConfig(const std::filesystem::path& file, const Config& cfg,
                std::string& err)
{
    Json j = Json::object();
    Json font = Json::object();
    font.set("backend", cfg.fontBackend)
        .set("path", cfg.fontPath)
        .set("size", cfg.fontPointSize);

    Json profiles = Json::array();
    for (const auto& p : cfg.profiles)
    {
        Json item = Json::object();
        item.set("id", p.id)
            .set("name", p.name)
            .set("baseUrl", p.baseUrl)
            .set("apiKey", p.apiKey)
            .set("model", p.model)
            .set("reasoningEffort", p.reasoningEffort)
            .set("apiProtocol", p.apiProtocol)
            .set("temperature", p.temperature)
            .set("contextTokens", p.contextTokens)
            .set("maxTokens", p.maxTokens);
        profiles.push(std::move(item));
    }
    j.set("profiles", std::move(profiles));
    j.set("activeProfile", cfg.activeProfileId);

    j.set("schemaVersion", 1)
        .set("appVersion", kAppVersion)
        .set("baseUrl", cfg.baseUrl)
        .set("apiKey", cfg.apiKey)
        .set("model", cfg.model)
        .set("reasoningEffort", cfg.reasoningEffort)
        .set("apiProtocol", cfg.apiProtocol)
        .set("temperature", cfg.temperature)
        .set("contextTokens", cfg.contextTokens)
        .set("maxTokens", cfg.maxTokens)
        .set("dataDir", cfg.dataDir)
        .set("sessionsDir", cfg.sessionsDir)
        .set("toolMode", toolModeToString(cfg.toolMode))
        .set("showReasoning", cfg.showReasoning)
        .set("autoTitle", cfg.autoTitle)
        .set("theme", cfg.theme)
        .set("lang", cfg.lang == Lang::En ? "en" : "zh")
        .set("uiScale", (double)cfg.uiScale)
        .set("onboarded", cfg.onboarded)
        .set("font", std::move(font));

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream out(paths::toUtf8(file), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err = "无法写入配置文件：" + paths::toUtf8(file);
        return false;
    }

    out << j.dump(true);
    if (!out)
    {
        err = "写入配置文件失败：" + paths::toUtf8(file);
        return false;
    }
    return true;
}

} //namespace sdlagent
