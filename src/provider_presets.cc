// SPDX-License-Identifier: GPL-3.0-only

#include "provider_presets.hpp"

#include "json.hpp"
#include "paths.hpp"
#include "util.hpp"
#include "version.hpp"

#include <cjson/cJSON.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>

namespace sdlagent::provider
{
namespace
{

struct Rule
{
    const char* host;
    std::vector<HttpHeader> headers;
};

const std::vector<Rule>& builtins()
{
    static const std::vector<Rule> rules = {
        { "opencode.ai", { { "x-opencode-session", "${sessionId}" } } },
        { "openrouter.ai", { { "X-Title", "SDL Agent" } } },
    };
    return rules;
}

std::string lower(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string hostOf(const std::string& url)
{
    std::size_t start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    std::size_t end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos
                                                  ? std::string::npos
                                                  : end - start);

    std::size_t at = authority.rfind('@');
    if (at != std::string::npos)
        authority.erase(0, at + 1);

    if (!authority.empty() && authority[0] == '[')
    {
        std::size_t close = authority.find(']');
        if (close != std::string::npos)
            authority = authority.substr(1, close - 1);
    }
    else
    {
        std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos)
            authority.erase(colon);
    }

    while (!authority.empty() && authority.back() == '.')
        authority.pop_back();
    return lower(authority);
}

bool hostMatches(const std::string& host, std::string pattern)
{
    pattern = lower(trim(pattern));
    while (!pattern.empty() && pattern[0] == '.')
        pattern.erase(pattern.begin());
    if (pattern.empty())
        return false;
    if (host == pattern)
        return true;
    if (host.size() <= pattern.size())
        return false;
    return host.compare(host.size() - pattern.size(), pattern.size(),
                        pattern) == 0 &&
           host[host.size() - pattern.size() - 1] == '.';
}

std::string substitute(const std::string& value, const std::string& model,
                       const std::string& sessionId)
{
    std::string out = value;
    out = replaceAll(out, "${sessionId}", sessionId);
    out = replaceAll(out, "${model}", model);
    out = replaceAll(out, "${appName}", "SDL Agent");
    out = replaceAll(out, "${appVersion}", kAppVersion);
    return out;
}

bool validHeaderPart(const std::string& s)
{
    return s.find('\r') == std::string::npos &&
           s.find('\n') == std::string::npos;
}

void mergeHeader(std::vector<HttpHeader>& out,
                 std::map<std::string, std::size_t>& index,
                 const std::string& rawName, const std::string& rawValue,
                 const std::string& model, const std::string& sessionId)
{
    std::string name = trim(rawName);
    if (name.empty() || !validHeaderPart(name))
        return;

    std::string value = substitute(rawValue, model, sessionId);
    if (value.empty() || !validHeaderPart(value))
        return;

    std::string key = lower(name);
    auto it = index.find(key);
    if (it != index.end())
    {
        out[it->second].value = std::move(value);
        return;
    }

    index[key] = out.size();
    out.push_back({ std::move(name), std::move(value) });
}

void loadUserRules(const std::string& host, const std::string& model,
                   const std::string& sessionId,
                   std::vector<HttpHeader>& out,
                   std::map<std::string, std::size_t>& index)
{
    std::ifstream in(paths::gatewaysFile(), std::ios::binary);
    if (!in)
        return;

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    Json root = Json::parse(text);
    if (!root.isObject())
        return;

    const cJSON* providers =
        cJSON_GetObjectItemCaseSensitive(root.raw(), "providers");
    if (!cJSON_IsObject(providers))
        return;

    for (const cJSON* item = providers->child; item; item = item->next)
    {
        if (!cJSON_IsObject(item) || !item->string)
            continue;
        if (!hostMatches(host, item->string))
            continue;

        const cJSON* headers =
            cJSON_GetObjectItemCaseSensitive(item, "headers");
        if (!cJSON_IsObject(headers))
            continue;

        for (const cJSON* h = headers->child; h; h = h->next)
        {
            if (!cJSON_IsString(h) || !h->string || !h->valuestring)
                continue;
            mergeHeader(out, index, h->string, h->valuestring, model,
                        sessionId);
        }
    }
}

} //namespace

std::vector<HttpHeader> headersFor(const std::string& baseUrl,
                                   const std::string& model,
                                   const std::string& sessionId)
{
    std::vector<HttpHeader> out;
    std::map<std::string, std::size_t> index;

    std::string host = hostOf(baseUrl);
    if (host.empty())
        return out;

    for (const Rule& rule : builtins())
    {
        if (!hostMatches(host, rule.host))
            continue;
        for (const HttpHeader& header : rule.headers)
            mergeHeader(out, index, header.name, header.value, model,
                        sessionId);
    }

    //User overrides win over built-ins with the same header name.
    loadUserRules(host, model, sessionId, out, index);
    return out;
}

} //namespace sdlagent::provider
