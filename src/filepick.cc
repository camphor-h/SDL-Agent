// SPDX-License-Identifier: GPL-3.0-only

#include "filepick.hpp"

#include <atomic>
#include <mutex>

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

namespace sdlagent::filepick
{
namespace
{

std::mutex              gMutex;
std::vector<std::string> gPaths;
bool                    gPending = false;
std::atomic<std::uint32_t> gEventType{ 0 };

void pushEvent()
{
    std::uint32_t type = gEventType.load(std::memory_order_relaxed);
    if (!type)
        return;

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = type;
    SDL_PushEvent(&ev);
}

} //namespace

void setEventType(std::uint32_t type)
{
    gEventType.store(type, std::memory_order_relaxed);
}

void deliver(std::vector<std::string> paths)
{
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gPaths = std::move(paths);
        gPending = true;
    }
    pushEvent();
}

bool take(std::vector<std::string>& paths)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gPending)
        return false;
    gPending = false;
    paths = std::move(gPaths);
    gPaths.clear();
    return true;
}

#if defined(__ANDROID__)

bool available()
{
    return true;
}

void open()
{
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity)
        return;

    jclass cls = env->GetObjectClass(activity);
    if (!cls)
        return;

    jmethodID method = env->GetMethodID(cls, "launchFilePicker", "()V");
    if (method)
        env->CallVoidMethod(activity, method);

    env->DeleteLocalRef(cls);
}

#else

bool available()
{
    return false;
}

void open()
{
}

#endif

} //namespace sdlagent::filepick

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_MainActivity_nativeFilesPicked(JNIEnv* env, jclass,
                                                   jobjectArray paths)
{
    std::vector<std::string> result;
    if (paths)
    {
        jsize count = env->GetArrayLength(paths);
        result.reserve((std::size_t)count);
        for (jsize i = 0; i < count; ++i)
        {
            jstring item = (jstring)env->GetObjectArrayElement(paths, i);
            if (!item)
                continue;
            const char* chars = env->GetStringUTFChars(item, nullptr);
            if (chars)
            {
                result.emplace_back(chars);
                env->ReleaseStringUTFChars(item, chars);
            }
            env->DeleteLocalRef(item);
        }
    }

    sdlagent::filepick::deliver(std::move(result));
}
#endif
