// SPDX-License-Identifier: GPL-3.0-only

#include "json.hpp"

#include <cjson/cJSON.h>

#include <cstring>
#include <utility>

namespace sdlagent
{

Json Json::object() { return Json(cJSON_CreateObject()); }
Json Json::array()  { return Json(cJSON_CreateArray()); }
Json Json::string(const std::string& s)
{
    return Json(cJSON_CreateString(s.c_str()));
}
Json Json::num(double d)
{
    return Json(cJSON_CreateNumber(d));
}
Json Json::intValue(long long i)
{
    return Json(cJSON_CreateNumber((double)i));
}
Json Json::boolValue(bool b) { return Json(cJSON_CreateBool(b ? 1 : 0)); }
Json Json::null() { return Json(cJSON_CreateNull()); }

Json Json::parse(const std::string& text)
{
    if (text.empty())
        return {};
    return Json(cJSON_ParseWithLength(text.data(), text.size()));
}

Json Json::parse(const char* text, std::size_t len)
{
    if (!text || len == 0)
        return {};
    return Json(cJSON_ParseWithLength(text, len));
}

Json::Json(const Json& other)
    : v_(other.v_ ? cJSON_Duplicate(other.v_, 1) : nullptr)
{
}

Json::Json(Json&& other) noexcept : v_(other.v_)
{
    other.v_ = nullptr;
}

Json& Json::operator=(const Json& other)
{
    if (this != &other)
    {
        Json tmp(other);
        std::swap(v_, tmp.v_);
    }
    return *this;
}

Json& Json::operator=(Json&& other) noexcept
{
    if (this != &other)
    {
        if (v_)
            cJSON_Delete(v_);
        v_ = other.v_;
        other.v_ = nullptr;
    }
    return *this;
}

Json::~Json()
{
    if (v_)
        cJSON_Delete(v_);
}

bool Json::isObject() const { return v_ && cJSON_IsObject(v_); }
bool Json::isArray() const  { return v_ && cJSON_IsArray(v_); }
bool Json::isString() const { return v_ && cJSON_IsString(v_); }
bool Json::isNumber() const { return v_ && cJSON_IsNumber(v_); }
bool Json::isBool() const   { return v_ && cJSON_IsBool(v_); }
bool Json::isNull() const   { return v_ && cJSON_IsNull(v_); }

std::size_t Json::size() const
{
    if (!v_)
        return 0;
    return (std::size_t)cJSON_GetArraySize(v_);
}

Json Json::operator[](const char* key) const
{
    if (!v_ || !key || !cJSON_IsObject(v_))
        return {};
    cJSON* item = cJSON_GetObjectItemCaseSensitive(v_, key);
    if (!item)
        return {};
    return Json(cJSON_Duplicate(item, 1));
}

Json Json::operator[](std::size_t index) const
{
    if (!v_ || !cJSON_IsArray(v_))
        return {};
    cJSON* item = cJSON_GetArrayItem(v_, (int)index);
    if (!item)
        return {};
    return Json(cJSON_Duplicate(item, 1));
}

bool Json::has(const char* key) const
{
    if (!v_ || !key || !cJSON_IsObject(v_))
        return false;
    return cJSON_GetObjectItemCaseSensitive(v_, key) != nullptr;
}

Json& Json::set(const char* key, Json value)
{
    if (!v_ || !key || !cJSON_IsObject(v_) || !value.v_)
        return *this;

    cJSON* node = value.release();
    if (cJSON_GetObjectItemCaseSensitive(v_, key))
    {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(v_, key, node))
            cJSON_Delete(node);
    }
    else
    {
        if (!cJSON_AddItemToObject(v_, key, node))
            cJSON_Delete(node);
    }
    return *this;
}

Json& Json::set(const char* key, const std::string& value)
{
    return set(key, Json::string(value));
}

Json& Json::set(const char* key, const char* value)
{
    return set(key, Json::string(value ? value : ""));
}

Json& Json::set(const char* key, int value)
{
    return set(key, Json::intValue(value));
}

Json& Json::set(const char* key, long long value)
{
    return set(key, Json::intValue(value));
}

Json& Json::set(const char* key, double value)
{
    return set(key, Json::num(value));
}

Json& Json::set(const char* key, bool value)
{
    return set(key, Json::boolValue(value));
}

Json& Json::push(Json value)
{
    if (!v_ || !cJSON_IsArray(v_) || !value.v_)
        return *this;
    cJSON* node = value.release();
    if (!cJSON_AddItemToArray(v_, node))
        cJSON_Delete(node);
    return *this;
}

Json& Json::push(const std::string& value) { return push(Json::string(value)); }
Json& Json::push(const char* value)
{
    return push(Json::string(value ? value : ""));
}
Json& Json::push(int value) { return push(Json::intValue(value)); }
Json& Json::push(double value) { return push(Json::num(value)); }
Json& Json::push(bool value) { return push(Json::boolValue(value)); }

std::string Json::str(const std::string& fallback) const
{
    if (!v_ || !cJSON_IsString(v_) || !v_->valuestring)
        return fallback;
    return v_->valuestring;
}

double Json::number(double fallback) const
{
    if (!v_ || !cJSON_IsNumber(v_))
        return fallback;
    return v_->valuedouble;
}

long long Json::integer(long long fallback) const
{
    if (!v_ || !cJSON_IsNumber(v_))
        return fallback;
    return (long long)v_->valuedouble;
}

bool Json::boolean(bool fallback) const
{
    if (!v_ || !cJSON_IsBool(v_))
        return fallback;
    return cJSON_IsTrue(v_) != 0;
}

std::string Json::dump(bool pretty) const
{
    if (!v_)
        return {};
    char* text = pretty ? cJSON_Print(v_) : cJSON_PrintUnformatted(v_);
    if (!text)
        return {};
    std::string out(text);
    cJSON_free(text);
    return out;
}

cJSON* Json::release()
{
    cJSON* v = v_;
    v_ = nullptr;
    return v;
}

Json jsonStringArray(std::initializer_list<const char*> items)
{
    Json arr = Json::array();
    for (const char* s : items)
        arr.push(s ? s : "");
    return arr;
}

} //namespace sdlagent
