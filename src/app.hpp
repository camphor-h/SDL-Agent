// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent_engine.hpp"
#include "config.hpp"
#include "session_store.hpp"
#include "sdlgui.hpp"
#include "title_gen.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace sdlagent::ui
{
class Panel;
class VLine;
class SessionList;
class ChatPanel;
class ArrowButton;
class SettingsDialog;
class InputDialog;
class ConfirmDialog;
}

namespace sdlagent
{

class App
{
public:
    explicit App(bool openSettingsOnStart = false,
                 std::string initialSessionId = {});
    ~App();

    int run();

private:
    void buildUi();
    void buildDialogs();
    void applyTheme();
    void applyTheme(const std::string& themeName);
    void layoutTopbar();
    void updateTopbar();

    void refreshSidebar();
    void selectSession(const std::string& id);
    Session* ensureSession();
    Session* currentSession();
    std::string nextUntitledTitle() const;

    void createSession();
    void sendMessage(const std::string& text);
    void cancelRun();

    void onAgentEvents();
    void handleCameraResult();
    void handleFilePickResult();
    void handleImeInset();
    void applyImeInset(int px);
    static bool SDLCALL imeResizeWatch(void* userdata, SDL_Event* event);

    //Clipboard and drag-and-drop handling.
    static bool SDLCALL eventFilter(void* userdata, SDL_Event* event);
    bool handleAppEvent(SDL_Event& ev);
    bool pasteFromClipboard();
    void handleDroppedFile(const char* path);
    void updateWebSearchAvailability();
    bool canAttachFiles() const;
    void attachFiles(const std::vector<std::string>& paths);
    void handleAgentEvent(const AgentEvent& ev, bool& refresh, bool& force);
    void setBusyUi(bool busy);
    void showStatus(const std::string& text);
    void updateUsageUi();
    static Uint32 SDLCALL logTimerProc(void* userdata, SDL_TimerID timerID,
                                       Uint32 interval);

    void maybeStartTitleJob(Session& session);
    void startTitleWorker();
    void stopTitleWorker();
    void processTitleResults();

    void openSettings();
    void createSettingsDialog();
    bool saveConfigFromDialog(const Config& cfg, std::string& err);

    void toggleSidebar();
    void togglePin(const std::string& id);    void renameSession(const std::string& id);
    void exportSession(const std::string& id);
    //Loads messages for an operation; drops the entry when the file is gone.
    bool loadSessionForAction(const std::string& id);
    void requestDeleteSession(const std::string& id);
    void deleteSession(const std::string& id);

    AgentRunConfig makeRunConfig(const std::string& sessionId) const;
    std::string    effectiveWorkspace() const;

    sdlgui::Application gui_;
    sdlgui::Window*     win_ = nullptr;

    Config       config_;
    SessionStore store_;
    AgentEngine  engine_;

    uint32_t agentEventType_ = 0;
    uint32_t titleEventType_ = 0;
    uint32_t logEventType_ = 0;
    uint32_t cameraEventType_ = 0;
    uint32_t imeEventType_ = 0;
    uint32_t filePickEventType_ = 0;
    int      imeInsetPx_ = 0;
    int      imeBaseWindowH_ = 0;
    int settingsLineHeight_ = 0;
    SDL_TimerID logTimer_ = 0;

    ui::Panel*       sidebar_ = nullptr;
    ui::SessionList* sessionList_ = nullptr;
    sdlgui::SplitView* mainSplit_ = nullptr;
    sdlgui::Drawer*    drawer_ = nullptr;
    ui::ArrowButton*   drawerTab_ = nullptr;
    bool               phoneUI_ = false;
    ui::Panel*       sidebarTopbar_ = nullptr;
    sdlgui::Button*  newChatButton_ = nullptr;
    sdlgui::Button*  settingsButton_ = nullptr;
    ui::ArrowButton* toggleSidebar_ = nullptr;
    sdlgui::Label*   titleLabel_ = nullptr;
    ui::Panel*       contentTopbar_ = nullptr;
    ui::VLine*       contentTopbarLine_ = nullptr;
    ui::ChatPanel*   chat_ = nullptr;

    std::unique_ptr<ui::SettingsDialog> settingsDialog_;
    std::unique_ptr<ui::InputDialog>    inputDialog_;
    std::unique_ptr<ui::ConfirmDialog>  confirmDialog_;

    std::string currentId_;
    std::string runningId_;

    struct TitleJob
    {
        std::string sessionId;
        titlegen::Request request;
    };

    struct TitleResult
    {
        std::string sessionId;
        std::string title;
    };

    std::mutex titleMutex_;
    std::condition_variable titleCv_;
    std::deque<TitleJob> titleJobs_;
    std::vector<TitleResult> titleResults_;
    std::thread titleThread_;
    std::atomic<bool> titleCancel_{false};
    std::atomic<bool> titleStop_{false};
    std::set<std::string> titleAttempted_;

    std::chrono::steady_clock::time_point lastRefresh_;
    bool forceRefreshNext_ = false;
};

} //namespace sdlagent
