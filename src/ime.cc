// SPDX-License-Identifier: GPL-3.0-only

#include "ime.hpp"

#include <atomic>

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace sdlagent::ime
{
namespace
{

std::atomic<std::uint32_t> gEventType{ 0 };
std::atomic<int>           gInset{ 0 };
std::atomic<bool>          gPending{ false };

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
    if (type && gPending.load(std::memory_order_relaxed))
        pushEvent();
}

void deliverInset(int px)
{
    gInset.store(px, std::memory_order_relaxed);
    gPending.store(true, std::memory_order_relaxed);
    pushEvent();
}

bool takeInset(int& px)
{
    if (!gPending.exchange(false, std::memory_order_relaxed))
        return false;
    px = gInset.load(std::memory_order_relaxed);
    return true;
}

} //namespace sdlagent::ime

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_MainActivity_nativeImeInset(JNIEnv*, jclass, jint px)
{
    sdlagent::ime::deliverInset((int)px);
}
#endif
