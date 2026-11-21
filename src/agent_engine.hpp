// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "config.hpp"
#include "session.hpp"
#include "tools.hpp"

#include "llmapi.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sdlagent
{

struct AgentRunConfig
{
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    std::string systemPrompt; //empty = built-in
    double      temperature = 0.0;
    int         maxTokens = 4096;
    std::filesystem::path workspace;
    //Sessions root used to resolve attachment paths.
    std::filesystem::path sessionsRoot;
    bool        imageInput = false;
    ToolMode    toolMode = ToolMode::Ask;
};

struct ConfirmationData
{
    std::string tool;
    std::string summary;
    std::string detail;
};

enum class AgentEventType
{
    TextDelta,
    ThinkingDelta,
    AssistantDone,
    ToolStarted,
    ToolFinished,
    Error,
    Usage,
    Finished,
    ConfirmRequest
};

struct AgentEvent
{
    AgentEventType type = AgentEventType::Finished;
    std::string    sessionId;
    std::string    text;
    Message        message;
    int            promptTokens = 0;
    int            completionTokens = 0;
    bool           cancelled = false;
};

//Runs one conversation turn (including the tool loop) on a worker thread and
//posts events to the UI through an SDL user event.
class AgentEngine
{
public:
    AgentEngine();
    ~AgentEngine();

    AgentEngine(const AgentEngine&) = delete;
    AgentEngine& operator=(const AgentEngine&) = delete;

    void setUserEventType(uint32_t type) { userEventType_ = type; }

    void start(const std::string& sessionId, std::vector<Message> history,
               const AgentRunConfig& cfg);
    void cancel();

    bool busy() const { return busy_.load(std::memory_order_relaxed); }

    //called from the UI thread after the user event arrives
    std::vector<AgentEvent> takeEvents();

    //confirmation handshake
    std::optional<ConfirmationData> takeConfirmation() const;
    void resolveConfirmation(bool allow);

private:
    struct ToolAccum
    {
        std::string id;
        std::string name;
        std::string args;
        std::string type = "function";
    };

    struct StreamAccum
    {
        std::string text;
        std::string thinking;
        std::map<int, ToolAccum> calls;
    };

    void run(std::vector<Message> history, AgentRunConfig cfg);
    void finish(bool cancelled, const std::string& sessionId);

    void pushEvent(AgentEvent ev);
    void flushDeltas(bool force, const std::string& sessionId);

    void onStream(StreamAccum& acc, LlmmapiStreamType type,
                  const char* content, int done);

    static void streamTrampoline(LlmmapiStreamType type, const char* content,
                                 int done, void* userdata);

    bool requestConfirmation(const ConfirmationData& data);

    static std::string defaultSystemPrompt(const AgentRunConfig& cfg,
                                           bool toolsEnabled);
    static std::string buildToolCallsJson(const std::map<int, ToolAccum>& calls);

    uint32_t userEventType_ = 0;

    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};

    std::thread thread_;

    std::string sessionId_;

    //worker -> UI event queue
    mutable std::mutex    queueMutex_;
    std::deque<AgentEvent> queue_;
    bool                  eventPending_ = false;

    //streaming coalescing (worker thread only)
    std::string pendingText_;
    std::string pendingThinking_;
    std::chrono::steady_clock::time_point lastEmit_;

    //confirmation handshake
    mutable std::mutex      confirmMutex_;
    std::condition_variable confirmCv_;
    std::optional<ConfirmationData> confirmData_;
    bool confirmPending_ = false;
    bool confirmAnswered_ = false;
    bool confirmAllow_ = false;

    ToolRegistry tools_;
};

} //namespace sdlagent
