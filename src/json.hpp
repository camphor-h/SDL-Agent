// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

struct cJSON;

namespace sdlagent
{

//Thin RAII wrapper over cJSON.
//
//- owns its node, deep-copies on copy, moves on move
//- an invalid Json represents "missing value" and is safe to query
//- element access returns a copy, so cJSON ownership never leaks to callers
class Json
{
public:
    Json() noexcept : v_(nullptr) {}

    static Json object();
    static Json array();
    static Json string(const std::string& s);
    static Json num(double d);
    static Json intValue(long long i);
    static Json boolValue(bool b);
    static Json null();

    static Json parse(const std::string& text);
    static Json parse(const char* text, std::size_t len);

    Json(const Json& other);
    Json(Json&& other) noexcept;
    Json& operator=(const Json& other);
    Json& operator=(Json&& other) noexcept;
    ~Json();

    bool valid() const { return v_ != nullptr; }
    bool isObject() const;
    bool isArray() const;
    bool isString() const;
    bool isNumber() const;
    bool isBool() const;
    bool isNull() const;

    std::size_t size() const;

    //object / array access; returns an invalid Json when missing
    Json operator[](const char* key) const;
    Json operator[](std::size_t index) const;

    bool has(const char* key) const;

    //object helpers (only effective on objects)
    Json& set(const char* key, Json value);
    Json& set(const char* key, const std::string& value);
    Json& set(const char* key, const char* value);
    Json& set(const char* key, int value);
    Json& set(const char* key, long long value);
    Json& set(const char* key, double value);
    Json& set(const char* key, bool value);

    //array helpers (only effective on arrays)
    Json& push(Json value);
    Json& push(const std::string& value);
    Json& push(const char* value);
    Json& push(int value);
    Json& push(double value);
    Json& push(bool value);

    //scalar getters with fallbacks
    std::string str(const std::string& fallback = {}) const;
    double      number(double fallback = 0.0) const;
    long long   integer(long long fallback = 0) const;
    bool        boolean(bool fallback = false) const;

    std::string dump(bool pretty = false) const;

    cJSON* raw() const { return v_; }
    cJSON* release();

private:
    explicit Json(cJSON* v) : v_(v) {}

    cJSON* v_;
};

Json jsonStringArray(std::initializer_list<const char*> items);

} //namespace sdlagent
