// SPDX-License-Identifier: GPL-3.0-only

#include "api_update.hpp"

#include "json.hpp"
#include "paths.hpp"

#include <curl/curl.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace sdlagent::apiupdate
{
namespace
{

struct ProgressCtx
{
    std::atomic<int>*  percent = nullptr;
    std::atomic<bool>* cancel = nullptr;
};

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::ofstream*>(userdata);
    std::size_t bytes = size * nmemb;
    out->write(ptr, (std::streamsize)bytes);
    if (!out->good())
        return 0;
    return bytes;
}

int progressCallback(void* userdata, curl_off_t dlTotal,
                     curl_off_t dlNow, curl_off_t, curl_off_t)
{
    auto* ctx = static_cast<ProgressCtx*>(userdata);
    if (ctx->cancel && ctx->cancel->load())
        return 1;

    if (ctx->percent && dlTotal > 0)
    {
        int value = (int)((dlNow * 100) / dlTotal);
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        ctx->percent->store(value);
    }
    return 0;
}

bool validateJsonFile(const std::filesystem::path& path, std::string& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        err = "无法读取下载文件：" + paths::toUtf8(path);
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (text.empty())
    {
        err = "下载内容为空。";
        return false;
    }

    Json j = Json::parse(text);
    if (!j.isObject() || j.size() == 0)
    {
        err = "下载内容不是有效的 api.json。";
        return false;
    }
    return true;
}

} //namespace

Result download(const Request& request, std::atomic<int>& percent,
                std::atomic<bool>& cancel)
{
    Result result;
    percent.store(0);

    std::filesystem::path temp = request.target;
    temp += ".download";

    std::error_code ec;
    std::filesystem::create_directories(request.target.parent_path(), ec);
    std::filesystem::remove(temp, ec);

    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        result.error = "无法创建下载文件：" + paths::toUtf8(temp);
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result.error = "无法初始化 libcurl。";
        return result;
    }

    ProgressCtx ctx{ &percent, &cancel };
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SDLAgent/api-updater");

    std::string proxy;
    if (request.proxyPort > 0)
    {
        proxy = "http://127.0.0.1:" + std::to_string(request.proxyPort);
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
    }

    CURLcode code = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    out.close();

    if (cancel.load())
    {
        result.cancelled = true;
        result.error = "下载已取消。";
        std::error_code existsEc;
        if (std::filesystem::exists(temp, existsEc) && !existsEc)
            result.savedPath = temp;
        return result;
    }

    if (code != CURLE_OK)
    {
        result.error = curl_easy_strerror(code);
        std::error_code existsEc;
        if (std::filesystem::exists(temp, existsEc) && !existsEc)
            result.savedPath = temp;
        return result;
    }

    if (httpCode < 200 || httpCode >= 300)
    {
        result.error = "服务器返回 HTTP " + std::to_string(httpCode);
        std::error_code existsEc;
        if (std::filesystem::exists(temp, existsEc) && !existsEc)
            result.savedPath = temp;
        return result;
    }

    std::string jsonErr;
    if (!validateJsonFile(temp, jsonErr))
    {
        result.error = jsonErr;
        std::error_code existsEc;
        if (std::filesystem::exists(temp, existsEc) && !existsEc)
            result.savedPath = temp;
        return result;
    }

    std::filesystem::path backup = request.target;
    backup += ".bak";

    std::filesystem::remove(backup, ec);

    bool hadTarget = std::filesystem::exists(request.target, ec) && !ec;
    if (hadTarget)
    {
        std::filesystem::rename(request.target, backup, ec);
        if (ec)
        {
            result.error = "无法备份旧 api.json：" + ec.message();
            result.savedPath = temp;
            return result;
        }
    }

    std::filesystem::rename(temp, request.target, ec);
    if (ec)
    {
        if (hadTarget)
        {
            std::error_code restoreEc;
            std::filesystem::rename(backup, request.target, restoreEc);
        }
        result.error = "无法替换 api.json：" + ec.message();
        result.savedPath = temp;
        return result;
    }

    std::filesystem::remove(backup, ec);

    percent.store(100);
    result.ok = true;
    result.savedPath = request.target;
    return result;
}

} //namespace sdlagent::apiupdate
