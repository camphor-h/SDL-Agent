// SPDX-License-Identifier: GPL-3.0-only

#include "agent_engine.hpp"

#include "attachments.hpp"
#include "http_transport.hpp"
#include "i18n.hpp"
#include "paths.hpp"
#include "util.hpp"

#include <SDL3/SDL.h>



#include <algorithm>
#include <cstring>



namespace sdlagent
{
namespace
{

struct StreamCtx
{
    AgentEngine* engine = nullptr;
    void*        acc = nullptr;
};

bool isMaxTokensError(const std::string& message)
{
    std::string s = toLower(message);
    return s.find("max_tokens") != std::string::npos ||
           s.find("max_completion_tokens") != std::string::npos ||
           s.find("maximum output tokens") != std::string::npos;
}

struct PreparedUser
{
    std::string              text;
    std::vector<std::string> images;
};

//Builds one user message. Text attachments are inlined with explicit
//boundaries; images become data: URLs for the neutral image parts.
PreparedUser prepareUserMessage(const Message& m,
                                const std::filesystem::path& root)
{
    PreparedUser out;
    std::string text = m.content;

    for (const Attachment& a : m.attachments)
    {
        if (attachment::isImage(a))
            continue;

        std::string body;
        std::string err;
        if (!attachment::readText(root, a, body, err))
        {
            text += "\n\n[附件: " + a.name + "]\n";
            text += std::string("（无法读取附件：") +
                    (err.empty() ? "未知错误" : err) + "）\n";
            text += "[附件结束: " + a.name + "]\n";
            continue;
        }

        text += "\n\n[附件开始: " + a.name + "]\n";
        text += body;
        if (!body.empty() && body.back() != '\n')
            text += '\n';
        text += "[附件结束: " + a.name + "]\n";
    }

    for (const Attachment& a : m.attachments)
    {
        if (!attachment::isImage(a))
            continue;

        std::string err;
        std::string url = attachment::imageDataUrl(root, a, err);
        if (url.empty())
        {
            text += "\n（无法读取图片：" + a.name + "）";
            continue;
        }
        out.images.push_back(std::move(url));
    }

    out.text = std::move(text);
    return out;
}

} //namespace

//A private static trampoline so the C callback can reach the engine.
void AgentEngine::streamTrampoline(const LlmmapiStreamEvent* ev,
                                   void* userdata)
{
    StreamCtx* ctx = static_cast<StreamCtx*>(userdata);
    if (!ctx || !ctx->engine || !ctx->acc || !ev)
        return;
    ctx->engine->onStream(*static_cast<StreamAccum*>(ctx->acc), ev);
}

void AgentEngine::onStream(StreamAccum& acc, const LlmmapiStreamEvent* ev)
{
    if (!ev)
        return;

    switch (ev->type)
    {
        case LLMAPI_STREAM_TEXT:
            if (!ev->text || !*ev->text)
                return;
            acc.text += ev->text;
            pendingText_ += ev->text;
            flushDeltas(false, sessionId_);
            break;

        case LLMAPI_STREAM_THINKING:
            if (!ev->text || !*ev->text)
                return;
            acc.thinking += ev->text;
            pendingThinking_ += ev->text;
            flushDeltas(false, sessionId_);
            break;

        case LLMAPI_STREAM_TOOL_CALL:
        {
            int index = (int)ev->toolIndex;
            if (index < 0)
                index = 0;

            ToolAccum& call = acc.calls[index];
            if (ev->toolCallId && *ev->toolCallId)
                call.id = ev->toolCallId;
            if (ev->toolName && *ev->toolName && call.name.empty())
                call.name = ev->toolName;
            if (ev->text)
                call.args += ev->text;
            break;
        }

        case LLMAPI_STREAM_DONE:
            break;
    }
}

AgentEngine::AgentEngine()
    : tools_(makeBuiltinTools())
{
    lastEmit_ = std::chrono::steady_clock::now();
}

AgentEngine::~AgentEngine()
{
    cancel();
    if (thread_.joinable())
        thread_.join();
}

void AgentEngine::start(const std::string& sessionId,
                        std::vector<Message> history,
                        const AgentRunConfig& cfg)
{
    if (thread_.joinable())
        thread_.join();

    if (busy_.load(std::memory_order_relaxed))
        return;

    cancel_ = false;
    sessionId_ = sessionId;
    pendingText_.clear();
    pendingThinking_.clear();
    lastEmit_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.clear();
        eventPending_ = false;
    }
    {
        std::lock_guard<std::mutex> lock(confirmMutex_);
        confirmData_.reset();
        confirmPending_ = false;
        confirmAnswered_ = false;
        confirmAllow_ = false;
    }

    busy_ = true;
    thread_ = std::thread([this, history = std::move(history), cfg]() mutable
                          { run(std::move(history), cfg); });
}

void AgentEngine::cancel()
{
    cancel_ = true;
    {
        std::lock_guard<std::mutex> lock(confirmMutex_);
        confirmCv_.notify_all();
    }
}

std::vector<AgentEvent> AgentEngine::takeEvents()
{
    std::vector<AgentEvent> out;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        out.reserve(queue_.size());
        while (!queue_.empty())
        {
            out.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        eventPending_ = false;
    }
    return out;
}

std::optional<ConfirmationData> AgentEngine::takeConfirmation() const
{
    std::lock_guard<std::mutex> lock(confirmMutex_);
    if (!confirmPending_ || !confirmData_)
        return std::nullopt;
    return confirmData_;
}

void AgentEngine::resolveConfirmation(bool allow)
{
    std::lock_guard<std::mutex> lock(confirmMutex_);
    if (!confirmPending_)
        return;
    confirmAnswered_ = true;
    confirmAllow_ = allow;
    confirmCv_.notify_all();
}

void AgentEngine::pushEvent(AgentEvent ev)
{
    bool needWake = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.push_back(std::move(ev));
        if (!eventPending_)
        {
            eventPending_ = true;
            needWake = true;
        }
    }

    if (needWake && userEventType_ != 0)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = userEventType_;
        e.user.type = userEventType_;
        SDL_PushEvent(&e);
    }
}

void AgentEngine::flushDeltas(bool force, const std::string& sessionId)
{
    auto now = std::chrono::steady_clock::now();
    if (!force && now - lastEmit_ < std::chrono::milliseconds(40))
        return;
    lastEmit_ = now;

    if (!pendingText_.empty())
    {
        AgentEvent ev;
        ev.type = AgentEventType::TextDelta;
        ev.sessionId = sessionId;
        ev.text = std::move(pendingText_);
        pendingText_.clear();
        pushEvent(std::move(ev));
    }
    if (!pendingThinking_.empty())
    {
        AgentEvent ev;
        ev.type = AgentEventType::ThinkingDelta;
        ev.sessionId = sessionId;
        ev.text = std::move(pendingThinking_);
        pendingThinking_.clear();
        pushEvent(std::move(ev));
    }
}

bool AgentEngine::requestConfirmation(const ConfirmationData& data)
{
    std::unique_lock<std::mutex> lock(confirmMutex_);
    if (cancel_.load(std::memory_order_relaxed))
        return false;

    confirmData_ = data;
    confirmPending_ = true;
    confirmAnswered_ = false;
    confirmAllow_ = false;
    lock.unlock();

    AgentEvent ev;
    ev.type = AgentEventType::ConfirmRequest;
    ev.sessionId = sessionId_;
    pushEvent(std::move(ev));

    lock.lock();
    confirmCv_.wait(lock, [&]
                    { return confirmAnswered_ || cancel_.load(std::memory_order_relaxed); });

    bool allow = confirmAnswered_ && confirmAllow_ &&
                 !cancel_.load(std::memory_order_relaxed);
    confirmPending_ = false;
    confirmData_.reset();
    return allow;
}

std::string AgentEngine::defaultSystemPrompt(const AgentRunConfig& cfg,
                                             bool toolsEnabled)
{
    std::string os = SDL_GetPlatform();
    std::string ws = paths::toUtf8(cfg.workspace);

    std::string s;
    s += "你是 SDL Agent，一个运行在用户电脑上的 AI 助手。\n";
    s += "操作系统: " + os + "\n";
    s += "工作区目录: " + ws + "\n\n";

    if (toolsEnabled)
    {
        s += "你可以调用工具来读取文件、修改文件和执行命令。工作方式：\n";
        s += "- 需要查看文件时使用 read_file；修改已有文件前先读取确认内容。\n";
        s += "- 修改已有文件优先用 edit_file 做精确替换；创建新文件或整体重写时才用 write_file。\n";
        s += "- 需要运行程序、构建或查看目录时使用 shell。\n";
        s += "- 工具执行结果会以 tool 消息返回；不要伪造工具输出，也不要声称执行了没有执行的操作。\n";
        s += "- 一次只做必要的工作，完成后用简洁的中文总结结果。\n\n";
    }

    s += "回答使用中文，除非用户使用其他语言。代码、命令和路径保持原样。";
    return s;
}

std::string AgentEngine::buildToolCallsJson(
    const std::map<int, ToolAccum>& calls)
{
    if (calls.empty())
        return {};

    Json arr = Json::array();
    for (const auto& [index, call] : calls)
    {
        std::string id = call.id;
        if (id.empty())
            id = "call_" + std::to_string(index);

        Json fn = Json::object();
        fn.set("name", call.name)
          .set("arguments", call.args.empty() ? "{}" : call.args);

        Json item = Json::object();
        item.set("id", id)
            .set("type", call.type.empty() ? "function" : call.type)
            .set("function", std::move(fn));
        arr.push(std::move(item));
    }
    return arr.dump();
}

void AgentEngine::finish(bool cancelled, const std::string& sessionId)
{
    busy_ = false;
    AgentEvent ev;
    ev.type = AgentEventType::Finished;
    ev.sessionId = sessionId;
    ev.cancelled = cancelled;
    pushEvent(std::move(ev));
}

void AgentEngine::run(std::vector<Message> history, AgentRunConfig cfg)
{
    const std::string sid = sessionId_;
    const bool toolsEnabled = (cfg.toolMode != ToolMode::Off);
    const std::string systemPrompt =
        cfg.systemPrompt.empty()
            ? defaultSystemPrompt(cfg, toolsEnabled)
            : cfg.systemPrompt;

    LlmmapiCtx* ctx = llmapiInit();
    if (!ctx)
    {
        AgentEvent ev;
        ev.type = AgentEventType::Error;
        ev.sessionId = sid;
        ev.text = "无法初始化 LLM 上下文。";
        pushEvent(std::move(ev));
        finish(false, sid);
        return;
    }

    TransportState transportState;
    transportState.cancel = &cancel_;
    transportState.sessionId = sid;
    transportState.headers = cfg.headers;

    LlmmapiTransport transport;
    std::memset(&transport, 0, sizeof(transport));
    transport.perform = &CurlTransport::perform;
    transport.userdata = &transportState;
    llmapiSetTransport(ctx, &transport);

    if (cfg.apiProtocol == "anthropic")
        llmapiSetApi(ctx, LLMMAPI_API_ANTHROPIC);
    else if (cfg.apiProtocol == "responses")
        llmapiSetApi(ctx, LLMMAPI_API_OPENAI_RESPONSES);
    else
        llmapiSetApi(ctx, LLMMAPI_API_OPENAI);

    const int kMaxRounds = 32;
    bool cancelled = false;
    bool failed = false;
    bool lastHadTools = false;

    //Some providers reject max_tokens values larger than their output limit.
    //Keep the configured value, but retry once without it.
    bool omitMaxTokens = false;

    //Per user turn image budget for images read by tools.
    int imagesThisTurn = 0;

    //Attachment payloads are built once per conversation turn; they are
    //re-sent to the provider on every tool round.
    std::map<std::size_t, PreparedUser> preparedUsers;

    for (int round = 0; round < kMaxRounds; ++round)
    {
        if (cancel_.load(std::memory_order_relaxed))
        {
            cancelled = true;
            break;
        }

        LlmmapiReq* req = llmapiReqNew(cfg.model.c_str());
        if (!req)
        {
            AgentEvent ev;
            ev.type = AgentEventType::Error;
            ev.sessionId = sid;
            ev.text = "无法创建请求。";
            pushEvent(std::move(ev));
            failed = true;
            break;
        }

        llmapiReqAddMsg(req, LLMAPI_ROLE_SYSTEM, systemPrompt.c_str());
        llmapiReqSetTemperature(req, cfg.temperature);
        if (!omitMaxTokens && cfg.maxTokens > 0)
            llmapiReqSetMaxTokens(req, (size_t)cfg.maxTokens);
        if (!cfg.reasoningEffort.empty())
            llmapiReqSetExtra(req, "reasoning_effort",
                              cfg.reasoningEffort.c_str());

        if (toolsEnabled)
        {
            for (const ToolSpec& tool : tools_.all())
            {
                if (!tool.available)
                    continue;
                llmapiReqAddTool(req, tool.name.c_str(),
                                 tool.description.c_str(),
                                 tool.parameters.dump().c_str());
            }
        }

        std::vector<std::string> pendingToolImages;
        auto flushToolImages = [&]()
        {
            if (pendingToolImages.empty())
                return;

            LlmmapiMessage* user =
                llmapiReqAddMessage(req, LLMAPI_ROLE_USER);
            llmapiMsgAddText(user, "Attached media from tool result:");

            for (const std::string& url : pendingToolImages)
                llmapiMsgAddImage(user, url.c_str());
            pendingToolImages.clear();
        };

        for (std::size_t hi = 0; hi < history.size(); ++hi)
        {
            const Message& m = history[hi];
            if (m.role == Role::System)
                continue;

            if (m.role == Role::Tool)
            {
                std::string id = m.toolCallId.empty() ? "call_0" : m.toolCallId;
                std::string name = m.toolName.empty() ? "tool" : m.toolName;
                std::string content = m.content.empty() ? "(无输出)" : m.content;
                (void)name;
                LlmmapiMessage* tool =
                    llmapiReqAddMessage(req, LLMAPI_ROLE_TOOL);
                llmapiMsgAddToolResult(tool, id.c_str(), content.c_str());

                for (const Attachment& a : m.attachments)
                {
                    if (!attachment::isImage(a))
                        continue;
                    std::string err;
                    std::string url =
                        attachment::imageDataUrl(cfg.sessionsRoot, a, err);
                    if (!url.empty())
                        pendingToolImages.push_back(std::move(url));
                }
                continue;
            }

            //Any non-tool message closes the current tool group.
            flushToolImages();

            if (m.role == Role::Assistant && !m.toolCalls.empty() &&
                m.toolCalls != "[]")
            {
                LlmmapiMessage* assistant =
                    llmapiReqAddMessage(req, LLMAPI_ROLE_ASSISTANT);
                if (!m.content.empty())
                    llmapiMsgAddText(assistant, m.content.c_str());

                Json calls = Json::parse(m.toolCalls);
                if (calls.isArray())
                {
                    for (std::size_t ci = 0; ci < calls.size(); ++ci)
                    {
                        Json call = calls[ci];
                        if (!call.isObject())
                            continue;

                        std::string id = call["id"].str("call_0");
                        std::string name;
                        std::string args = "{}";

                        Json fn = call["function"];
                        if (fn.isObject())
                        {
                            name = fn["name"].str();
                            Json value = fn["arguments"];
                            if (value.isString())
                                args = value.str();
                            else if (value.valid() && !value.isNull())
                                args = value.dump();
                        }

                        if (id.empty())
                            id = "call_0";
                        if (name.empty())
                            name = "tool";

                        llmapiMsgAddToolCall(assistant, id.c_str(),
                                             name.c_str(), args.c_str());
                    }
                }
                continue;
            }

            if (m.role == Role::User && !m.attachments.empty())
            {
                auto it = preparedUsers.find(hi);
                if (it == preparedUsers.end())
                {
                    PreparedUser prepared =
                        prepareUserMessage(m, cfg.sessionsRoot);
                    it = preparedUsers.emplace(hi, std::move(prepared)).first;
                }

                LlmmapiMessage* user =
                    llmapiReqAddMessage(req, LLMAPI_ROLE_USER);
                if (!it->second.text.empty())
                    llmapiMsgAddText(user, it->second.text.c_str());
                for (const std::string& url : it->second.images)
                    llmapiMsgAddImage(user, url.c_str());
                continue;
            }

            if (m.content.empty())
                continue;

            llmapiReqAddMsg(req, roleToString(m.role), m.content.c_str());
        }
        flushToolImages();

        StreamAccum acc;
        StreamCtx streamCtx;
        streamCtx.engine = this;
        streamCtx.acc = &acc;

        pendingText_.clear();
        pendingThinking_.clear();
        lastEmit_ = std::chrono::steady_clock::now();

        int rc = llmapiChatCompleteStream(
            ctx, cfg.baseUrl.c_str(), cfg.apiKey.c_str(), req,
            &streamTrampoline, &streamCtx);

        llmapiReqFree(req);

        flushDeltas(true, sid);

        long totalTokens = 0, promptTokens = 0, completionTokens = 0;
        llmapiStreamGetUsage(ctx, &totalTokens, &promptTokens,
                             &completionTokens);
        if (promptTokens < 0) promptTokens = 0;
        if (completionTokens < 0) completionTokens = 0;
        (void)totalTokens;

        if (cancel_.load(std::memory_order_relaxed))
        {
            cancelled = true;
            break;
        }

        if (rc != 0)
        {
            std::string msg = llmapiStrError(ctx);

            //Provider rejected the output cap: retry this round without it,
            //but only if the failed attempt produced no partial content.
            if (!omitMaxTokens && acc.text.empty() && acc.thinking.empty() &&
                acc.calls.empty() && isMaxTokensError(msg))
            {
                omitMaxTokens = true;
                --round;
                continue;
            }

            AgentEvent ev;
            ev.type = AgentEventType::Error;
            ev.sessionId = sid;
            ev.text = std::string(tr().requestFailed) +
                      (msg.empty() ? "unknown error" : msg);
            pushEvent(std::move(ev));
            failed = true;
            break;
        }

        Message assistant;
        assistant.role = Role::Assistant;
        assistant.content = acc.text;
        assistant.reasoning = acc.thinking;
        assistant.toolCalls = buildToolCallsJson(acc.calls);
        assistant.timestamp = nowMs();
        assistant.promptTokens = (int)promptTokens;
        assistant.completionTokens = (int)completionTokens;

        if (assistant.content.empty() && assistant.reasoning.empty() &&
            (assistant.toolCalls.empty() || assistant.toolCalls == "[]"))
        {
            AgentEvent ev;
            ev.type = AgentEventType::Error;
            ev.sessionId = sid;
            ev.text = tr().emptyResponse;
            pushEvent(std::move(ev));
            failed = true;
            break;
        }

        history.push_back(assistant);

        AgentEvent done;
        done.type = AgentEventType::AssistantDone;
        done.sessionId = sid;
        done.message = assistant;
        pushEvent(std::move(done));

        if (assistant.toolCalls.empty() || assistant.toolCalls == "[]")
            break;

        lastHadTools = false;
        Json calls = Json::parse(assistant.toolCalls);
        if (!calls.isArray() || calls.size() == 0)
            break;

        lastHadTools = true;

        //When a run is interrupted mid-batch, every tool_call still needs a
        //matching tool message or the next request would be malformed.
        auto appendInterruptedResults = [&](std::size_t from)
        {
            for (std::size_t rest = from; rest < calls.size(); ++rest)
            {
                Json call = calls[rest];
                if (!call.isObject())
                    continue;

                std::string id = call["id"].str("call_0");
                std::string name;
                Json fn = call["function"];
                if (fn.isObject())
                    name = fn["name"].str();
                if (id.empty())
                    id = "call_0";

                Message toolMsg;
                toolMsg.role = Role::Tool;
                toolMsg.content = tr().toolInterrupted;
                toolMsg.toolCallId = id;
                toolMsg.toolName = name;
                toolMsg.timestamp = nowMs();
                toolMsg.error = true;

                history.push_back(toolMsg);

                AgentEvent finished;
                finished.type = AgentEventType::ToolFinished;
                finished.sessionId = sid;
                finished.message = toolMsg;
                pushEvent(std::move(finished));
            }
        };

        for (std::size_t ci = 0; ci < calls.size(); ++ci)
        {
            if (cancel_.load(std::memory_order_relaxed))
            {
                cancelled = true;
                appendInterruptedResults(ci);
                break;
            }

            Json call = calls[ci];
            if (!call.isObject())
                continue;

            std::string id = call["id"].str("call_0");
            std::string name;
            std::string argsText = "{}";

            Json fn = call["function"];
            if (fn.isObject())
            {
                name = fn["name"].str();

                Json value = fn["arguments"];
                if (value.isString())
                    argsText = value.str();
                else if (value.valid() && !value.isNull())
                    argsText = value.dump();
            }

            if (id.empty())
                id = "call_0";

            AgentEvent started;
            started.type = AgentEventType::ToolStarted;
            started.sessionId = sid;
            started.text = name;
            started.message.toolName = name;
            started.message.toolCallId = id;
            started.message.content = argsText;
            pushEvent(std::move(started));

            Json args = Json::parse(argsText);
            if (!args.isObject())
                args = Json::object();

            ToolResult result;
            const ToolSpec* spec =
                toolsEnabled ? tools_.find(name) : nullptr;

            if (!spec || !spec->available)
            {
                result.ok = false;
                result.output = std::string(tr().unknownTool) + name;
            }
            else
            {
                bool allowed = true;
                if (cfg.toolMode == ToolMode::Ask)
                {
                    ConfirmationData data;
                    data.tool = name;
                    if (spec->describe)
                    {
                        ToolApproval ap = spec->describe(args);
                        data.summary = ap.summary;
                        data.detail = ap.detail;
                    }
                    else
                    {
                        data.summary = name;
                        data.detail = args.dump(true);
                    }
                    allowed = requestConfirmation(data);
                }

                if (cancel_.load(std::memory_order_relaxed))
                {
                    cancelled = true;
                }
                else if (!allowed)
                {
                    //A denied call ends the run instead of letting the model
                    //continue around it.
                    cancelled = true;
                    result.ok = false;
                    result.output = tr().toolDenied;
                }
                else
                {
                    ToolContext tctx;
                    tctx.workspace = cfg.workspace;
                    tctx.sessionsRoot = cfg.sessionsRoot;
                    tctx.sessionId = sid;
                    tctx.imageInput = cfg.imageInput;
                    tctx.imageCount = &imagesThisTurn;
                    tctx.maxImages = 9;
                    tctx.cancel = &cancel_;
                    try
                    {
                        result = spec->run(args, tctx);
                    }
                    catch (const std::exception& ex)
                    {
                        result.ok = false;
                        result.output = std::string("工具执行异常：") + ex.what();
                    }
                    catch (...)
                    {
                        result.ok = false;
                        result.output = "工具执行异常。";
                    }
                }
            }

            Message toolMsg;
            toolMsg.role = Role::Tool;
            toolMsg.content = result.output;
            toolMsg.toolCallId = id;
            toolMsg.toolName = name;
            toolMsg.timestamp = nowMs();
            toolMsg.error = !result.ok;
            toolMsg.attachments = std::move(result.attachments);

            if (toolMsg.content.size() > 120000)
                toolMsg.content = toolMsg.content.substr(0, 120000) + "\n[已截断]";

            history.push_back(toolMsg);

            AgentEvent finished;
            finished.type = AgentEventType::ToolFinished;
            finished.sessionId = sid;
            finished.message = toolMsg;
            pushEvent(std::move(finished));

            if (failed || cancelled)
            {
                if (cancelled)
                    appendInterruptedResults(ci + 1);
                break;
            }
        }

        if (cancelled)
            break;
    }

    if (lastHadTools && !cancelled && !failed)
    {
        AgentEvent ev;
        ev.type = AgentEventType::Error;
        ev.sessionId = sid;
        ev.text = tr().tooManyRounds;
        pushEvent(std::move(ev));
    }

    llmapiCleanup(ctx);
    finish(cancelled, sid);
}

} //namespace sdlagent
