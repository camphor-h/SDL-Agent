// SPDX-License-Identifier: GPL-3.0-only

#include "model_catalog.hpp"

#include "paths.hpp"
#include "util.hpp"

#include <cjson/cJSON.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace sdlagent::catalog
{
namespace
{

Catalog gCatalog;

std::string lower(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string stringField(const cJSON* obj, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring)
        return item->valuestring;
    if (cJSON_IsNumber(item))
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", item->valuedouble);
        return buf;
    }
    return {};
}

int intField(const cJSON* obj, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item))
        return 0;
    double v = item->valuedouble;
    if (!std::isfinite(v) || v < 0.0)
        return 0;
    if (v > 2147483647.0)
        return 2147483647;
    return (int)std::llround(v);
}

double doubleField(const cJSON* obj, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : 0.0;
}

bool boolField(const cJSON* obj, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) && cJSON_IsTrue(item);
}

void readModels(const cJSON* modelsObj, std::vector<Model>& out)
{
    for (const cJSON* item = modelsObj ? modelsObj->child : nullptr; item;
         item = item->next)
    {
        if (!cJSON_IsObject(item))
            continue;

        Model m;
        m.id = stringField(item, "id");
        if (m.id.empty())
            m.id = item->string ? item->string : "";
        m.name = stringField(item, "name");
        if (m.name.empty())
            m.name = m.id;

        const cJSON* limit =
            cJSON_GetObjectItemCaseSensitive(item, "limit");
        if (cJSON_IsObject(limit))
        {
            m.context = intField(limit, "context");
            m.output = intField(limit, "output");
        }

        m.toolCall = boolField(item, "tool_call");

        const cJSON* modalities =
            cJSON_GetObjectItemCaseSensitive(item, "modalities");
        if (cJSON_IsObject(modalities))
        {
            const cJSON* input =
                cJSON_GetObjectItemCaseSensitive(modalities, "input");
            if (cJSON_IsArray(input))
            {
                for (const cJSON* v = input->child; v; v = v->next)
                {
                    if (cJSON_IsString(v) && v->valuestring &&
                        std::string(v->valuestring) == "image")
                        m.imageInput = true;
                }
            }
        }

        const cJSON* cost = cJSON_GetObjectItemCaseSensitive(item, "cost");
        if (cJSON_IsObject(cost))
        {
            m.costInput = doubleField(cost, "input");
            m.costOutput = doubleField(cost, "output");
        }

        const cJSON* options =
            cJSON_GetObjectItemCaseSensitive(item, "reasoning_options");
        if (cJSON_IsArray(options))
        {
            for (const cJSON* option = options->child; option;
                 option = option->next)
            {
                if (!cJSON_IsObject(option))
                    continue;
                if (stringField(option, "type") != "effort")
                    continue;

                const cJSON* values =
                    cJSON_GetObjectItemCaseSensitive(option, "values");
                if (!cJSON_IsArray(values))
                    continue;

                for (const cJSON* value = values->child; value;
                     value = value->next)
                {
                    if (!cJSON_IsString(value) || !value->valuestring)
                        continue;
                    std::string effort = value->valuestring;
                    if (effort.empty() || effort == "null")
                        continue;
                    if (std::find(m.efforts.begin(), m.efforts.end(), effort) !=
                        m.efforts.end())
                        continue;
                    m.efforts.push_back(std::move(effort));
                }
            }
        }

        if (!m.id.empty())
            out.push_back(std::move(m));
    }

    std::sort(out.begin(), out.end(), [](const Model& a, const Model& b)
              {
                  if (lower(a.name) != lower(b.name))
                      return lower(a.name) < lower(b.name);
                  return a.id < b.id;
              });
}

bool sameFile(const std::filesystem::path& path,
              std::filesystem::file_time_type mtime, std::uintmax_t size)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    if (std::filesystem::file_size(path, ec) != size || ec)
        return false;
    return std::filesystem::last_write_time(path, ec) == mtime && !ec;
}

} //namespace

const Catalog& load(bool force)
{
    std::filesystem::path path = paths::apiCatalogFile();

    if (!force && gCatalog.loaded && gCatalog.path == path &&
        sameFile(path, gCatalog.mtime, gCatalog.size))
    {
        return gCatalog;
    }

    gCatalog = Catalog{};
    gCatalog.path = path;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        gCatalog.error = "未找到 api.json，请放到：" + paths::toUtf8(path);
        return gCatalog;
    }

    gCatalog.mtime = std::filesystem::last_write_time(path, ec);
    gCatalog.size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        gCatalog.error = "无法读取 api.json：" + paths::toUtf8(path);
        return gCatalog;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        gCatalog.error = "无法打开 api.json：" + paths::toUtf8(path);
        return gCatalog;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (text.empty())
    {
        gCatalog.error = "api.json 为空。";
        return gCatalog;
    }

    cJSON* root = cJSON_ParseWithLength(text.data(), text.size());
    if (!root || !cJSON_IsObject(root))
    {
        if (root)
            cJSON_Delete(root);
        gCatalog.error = "api.json 格式不正确。";
        return gCatalog;
    }

    for (const cJSON* item = root->child; item; item = item->next)
    {
        if (!cJSON_IsObject(item))
            continue;

        Provider p;
        p.id = stringField(item, "id");
        if (p.id.empty())
            p.id = item->string ? item->string : "";
        p.name = stringField(item, "name");
        if (p.name.empty())
            p.name = p.id;
        p.api = stringField(item, "api");
        p.npm = stringField(item, "npm");
        p.compatible = p.npm == "@ai-sdk/openai-compatible" ||
                       p.npm == "@ai-sdk/openai";

        const cJSON* models = cJSON_GetObjectItemCaseSensitive(item, "models");
        readModels(models, p.models);

        if (!p.id.empty() && !p.models.empty())
            gCatalog.providers.push_back(std::move(p));
    }

    cJSON_Delete(root);

    std::sort(gCatalog.providers.begin(), gCatalog.providers.end(),
              [](const Provider& a, const Provider& b)
              {
                  if (lower(a.name) != lower(b.name))
                      return lower(a.name) < lower(b.name);
                  return a.id < b.id;
              });

    if (gCatalog.providers.empty())
    {
        gCatalog.error = "api.json 中没有找到供应商数据。";
        return gCatalog;
    }

    gCatalog.loaded = true;
    return gCatalog;
}

bool supportsImage(const std::string& modelId, bool* found)
{
    if (found)
        *found = false;
    if (modelId.empty())
        return false;

    const Catalog& cat = load();
    for (const Provider& p : cat.providers)
    {
        for (const Model& m : p.models)
        {
            if (m.id != modelId)
                continue;
            if (found)
                *found = true;
            return m.imageInput;
        }
    }
    return false;
}

std::vector<std::string> reasoningEfforts(const std::string& modelId,
                                          const std::string& baseUrl)
{
    std::vector<std::string> out;
    if (modelId.empty())
        return out;

    const Catalog& cat = load();
    std::string base = lower(trim(baseUrl));
    while (!base.empty() && base.back() == '/')
        base.pop_back();

    const Model* fallback = nullptr;
    for (const Provider& p : cat.providers)
    {
        std::string api = lower(p.api);
        while (!api.empty() && api.back() == '/')
            api.pop_back();
        bool sameApi = !base.empty() && !api.empty() &&
                       (base == api || startsWith(base, api + "/"));

        for (const Model& m : p.models)
        {
            if (m.id != modelId || m.efforts.empty())
                continue;
            if (sameApi)
                return m.efforts;
            if (!fallback)
                fallback = &m;
        }
    }
    return fallback ? fallback->efforts : out;
}

} //namespace sdlagent::catalog
