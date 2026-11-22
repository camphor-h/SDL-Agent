// SPDX-License-Identifier: GPL-3.0-only

#include "camera.hpp"

#include <atomic>
#include <mutex>

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

namespace sdlagent::camera
{
namespace
{

std::mutex          gMutex;
std::string         gPath;
bool                gOk = false;
std::atomic<uint32_t> gEventType{ 0 };

} //namespace

void setEventType(std::uint32_t type)
{
    gEventType.store(type);
}

void deliverResult(bool ok, const std::string& path)
{
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gOk = ok;
        gPath = path;
    }

    uint32_t type = gEventType.load();
    if (type != 0)
    {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = type;
        SDL_PushEvent(&ev);
    }
}

bool takeResult(std::string& path)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gOk)
    {
        gPath.clear();
        return false;
    }
    gOk = false;
    path = std::move(gPath);
    gPath.clear();
    return !path.empty();
}

#if defined(__ANDROID__)

bool available()
{
    return true;
}

void capture()
{
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity)
        return;

    jclass cls = env->GetObjectClass(activity);
    if (!cls)
        return;

    jmethodID method = env->GetMethodID(cls, "launchCamera", "()V");
    if (method)
        env->CallVoidMethod(activity, method);

    env->DeleteLocalRef(cls);
}

#else

bool available()
{
    return false;
}

void capture()
{
}

#endif

} //namespace sdlagent::camera

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_MainActivity_nativeCameraResult(JNIEnv* env, jclass,
                                                    jboolean ok,
                                                    jstring path)
{
    std::string result;
    if (path)
    {
        const char* chars = env->GetStringUTFChars(path, nullptr);
        if (chars)
        {
            result = chars;
            env->ReleaseStringUTFChars(path, chars);
        }
    }
    sdlagent::camera::deliverResult(ok == JNI_TRUE, result);
}
#endif
