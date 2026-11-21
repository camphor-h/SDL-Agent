// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sdlagent::catalog
{

struct Model
{
    std::string id;
    std::string name;
    int         context = 0;
    int         output = 0;
    bool        toolCall = false;
    bool        imageInput = false;
    std::vector<std::string> efforts; //api.json reasoning effort values
    double      costInput = 0.0;
    double      costOutput = 0.0;
};

struct Provider
{
    std::string id;
    std::string name;
    std::string api;
    std::string npm;
    bool        compatible = false;
    std::vector<Model> models;
};

struct Catalog
{
    bool loaded = false;
    std::string error;
    std::filesystem::path path;
    std::vector<Provider> providers;

    std::filesystem::file_time_type mtime{};
    std::uintmax_t size = 0;
};

//Reads <dataDir>/api.json. Cached until the file changes; force re-parses.
const Catalog& load(bool force = false);

//Looks up a model id across all providers. Returns true only when the model
//was found and declares image input; `found` reports whether it was found.
bool supportsImage(const std::string& modelId, bool* found = nullptr);

//Reasoning effort values declared by api.json for a model, in catalog order.
//Empty when the model is unknown or only supports a reasoning toggle.
std::vector<std::string> reasoningEfforts(const std::string& modelId,
                                          const std::string& baseUrl);

} //namespace sdlagent::catalog
