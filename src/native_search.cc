// SPDX-License-Identifier: GPL-3.0-only

#include "native_search.hpp"

#include <algorithm>
#include <cctype>

namespace sdlagent::native_search
{
namespace
{

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string hostOf(const std::string& url)
{
    std::string s = url;
    std::size_t pos = s.find("://");
    if (pos != std::string::npos)
        s = s.substr(pos + 3);

    std::size_t slash = s.find('/');
    if (slash != std::string::npos)
        s = s.substr(0, slash);

    std::size_t at = s.rfind('@');
    if (at != std::string::npos)
        s = s.substr(at + 1);

    //Drop a port (the supported providers all use DNS names, not IPv6).
    std::size_t colon = s.rfind(':');
    if (colon != std::string::npos && s.find(':') == colon)
        s = s.substr(0, colon);

    return lower(s);
}

bool hostMatches(const std::string& host, const char* domain)
{
    const std::string d = domain;
    if (host == d)
        return true;
    return host.size() > d.size() + 1 &&
           host.compare(host.size() - d.size() - 1, d.size() + 1,
                        "." + d) == 0;
}

bool startsWith(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

bool contains(const std::string& s, const char* needle)
{
    return s.find(needle) != std::string::npos;
}

bool openaiResponsesModel(const std::string& model)
{
    return startsWith(model, "gpt-5") || startsWith(model, "gpt-6") ||
           startsWith(model, "o3") || startsWith(model, "o4");
}

Plan rawTool(const char* json)
{
    Plan plan;
    plan.mode = Plan::Mode::RawTool;
    plan.toolJson = json;
    return plan;
}

} //namespace

bool supported(const Plan& plan)
{
    return plan.mode != Plan::Mode::None;
}

Plan resolve(const std::string& baseUrl, const std::string& model,
             const std::string& protocol)
{
    const std::string host = hostOf(baseUrl);
    const std::string m = lower(model);
    const std::string proto = lower(protocol);

    //Anthropic: web_search_20250305 is the basic server-side tool.
    if (hostMatches(host, "anthropic.com"))
    {
        if (startsWith(m, "claude"))
            return rawTool("{\"type\":\"web_search_20250305\","
                           "\"name\":\"web_search\",\"max_uses\":5}");
        return {};
    }

    //OpenAI: Responses has the hosted web_search tool; Chat Completions only
    //supports the dedicated search models through web_search_options.
    if (hostMatches(host, "openai.com") ||
        hostMatches(host, "openai.azure.com"))
    {
        if (proto == "openai" && contains(m, "search"))
        {
            Plan plan;
            plan.mode = Plan::Mode::ChatOptions;
            return plan;
        }
        if (proto == "responses" && openaiResponsesModel(m))
            return rawTool("{\"type\":\"web_search\"}");
        return {};
    }

    //DeepSeek: deepseek_web_search works across all three wire protocols;
    //the Responses API also has a native web_search tool for v4-pro.
    if (hostMatches(host, "deepseek.com"))
    {
        if (!startsWith(m, "deepseek"))
            return {};
        if (proto == "responses" && startsWith(m, "deepseek-v4-pro"))
            return rawTool("{\"type\":\"web_search\"}");
        if (contains(m, "flash"))
        {
            if (proto == "anthropic")
                return rawTool("{\"type\":\"deepseek_web_search\","
                               "\"name\":\"deepseek_web_search\"}");
            return rawTool("{\"type\":\"deepseek_web_search\"}");
        }
        return {};
    }

    return {};
}

} //namespace sdlagent::native_search
