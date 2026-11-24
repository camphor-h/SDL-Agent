// SPDX-License-Identifier: GPL-3.0-only

#include "app.hpp"

#include "icon_data.inc"

#include "agents.hpp"
#include "attachments.hpp"
#include "camera.hpp"
#include "chat_panel.hpp"
#include "clipboard.hpp"
#include "dialogs.hpp"
#include "filepick.hpp"
#include "font.hpp"
#include "i18n.hpp"
#include "ime.hpp"
#include "log.hpp"
#include "model_catalog.hpp"
#include "native_search.hpp"
#include "paths.hpp"
#include "prompt.hpp"
#include "provider_presets.hpp"
#include "session_list.hpp"
#include "title_gen.hpp"
#include "ui_widgets.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>

namespace sdlagent
{
namespace sg = sdlgui;

namespace
{

sg::Theme makeTheme(bool light)
{
    if (light)
    {
        sg::Theme t = sg::Theme::light();
        t.windowBg = sg::Color(246, 247, 249);
        t.panelBg = sg::Color(238, 239, 243);
        t.panelBorder = sg::Color(210, 212, 218);
        t.border = sg::Color(200, 202, 208);
        t.divider = sg::Color(210, 212, 218);
        t.menuAccent = sg::Color(50, 110, 190);
        t.titleBarBg = sg::Color(228, 230, 234);
        t.titleText = sg::Color(40, 42, 48);
        t.titleButton = sg::Color(228, 230, 234);
        t.titleButtonHover = sg::Color(205, 208, 214);
        t.dialogBg = sg::Color(250, 250, 252);
        t.dialogBorder = sg::Color(200, 203, 210);
        t.dialogTitleText = sg::Color(40, 42, 48);
        t.rowHover = sg::Color(228, 230, 235);
        t.rowSelected = sg::Color(224, 236, 250);
        t.rowSelectedText = sg::Color(20, 20, 20);
        t.overlay = sg::Color(20, 22, 26, 120);
        return t;
    }

    sg::Theme t = sg::Theme::dark();
    t.windowBg = sg::Color(22, 23, 26);
    t.text = sg::Color(226, 228, 233);
    t.mutedText = sg::Color(150, 153, 160);
    t.panelBg = sg::Color(30, 31, 35);
    t.panelBorder = sg::Color(54, 56, 62);
    t.border = sg::Color(58, 60, 66);

    t.buttonBg = sg::Color(44, 46, 52);
    t.buttonHover = sg::Color(56, 58, 66);
    t.buttonPressed = sg::Color(36, 38, 43);
    t.buttonText = sg::Color(230, 232, 237);
    t.buttonBorder = sg::Color(60, 62, 70);

    t.inputBg = sg::Color(24, 25, 29);
    t.inputText = sg::Color(226, 228, 233);
    t.inputPlaceholder = sg::Color(118, 121, 128);
    t.inputBorder = sg::Color(58, 60, 66);
    t.inputCursor = sg::Color(210, 214, 220);
    t.inputSelection = sg::Color(52, 92, 160);

    t.textAreaBg = sg::Color(24, 25, 29);
    t.textAreaText = sg::Color(226, 228, 233);
    t.textAreaCursor = sg::Color(210, 214, 220);
    t.textAreaSelection = sg::Color(52, 92, 160, 175);

    t.rowHover = sg::Color(38, 40, 46);
    t.rowSelected = sg::Color(52, 80, 122);
    t.rowSelectedText = sg::Color(255, 255, 255);

    t.scrollTrack = sg::Color(30, 31, 35);
    t.scrollThumb = sg::Color(80, 84, 92);

    t.titleBarBg = sg::Color(20, 21, 24);
    t.titleText = sg::Color(210, 213, 219);
    t.titleButton = sg::Color(20, 21, 24);
    t.titleButtonHover = sg::Color(46, 48, 54);
    t.titleCloseHover = sg::Color(190, 45, 45);

    t.menuAccent = sg::Color(255, 242, 0); //same yellow as the app icon
    t.checkboxBox = sg::Color(230, 232, 236);
    t.checkboxBorder = sg::Color(90, 93, 100);
    t.checkboxCheck = sg::Color(70, 150, 90);

    //Dark amber for list/dropdown selections; the session list and profile
    //list use a softer pale yellow with black text below.
    t.rowSelected = sg::Color(74, 66, 10);
    t.rowSelectedText = sg::Color(255, 242, 0);
    t.overlay = sg::Color(0, 0, 0, 180);
    t.dialogBg = sg::Color(38, 40, 46);
    t.dialogBorder = sg::Color(70, 73, 80);
    t.dialogTitleText = sg::Color(226, 228, 233);
    t.dialogText = sg::Color(226, 228, 233);

    t.divider = sg::Color(50, 52, 58);
    return t;
}

std::filesystem::path normalized(const std::filesystem::path& p)
{
    std::error_code ec;
    auto abs = std::filesystem::absolute(p, ec);
    if (ec)
        return p.lexically_normal();
    return abs.lexically_normal();
}

//An untitled conversation with no messages is the placeholder "new chat".
//It cannot be renamed, pinned or deleted.
bool isNewConversation(const Session& s)
{
    return !s.hasMessages && s.messages.empty() &&
           (s.title.empty() || s.title.rfind(tr().untitled, 0) == 0);
}

//True while the title is still the locally generated placeholder.
bool isProvisionalTitle(const Session& s)
{
    if (isNewConversation(s))
        return true;

    for (const Message& m : s.messages)
    {
        if (m.role == Role::User)
        {
            std::string provisional = firstLine(m.content, 40);
            return s.title.empty() || s.title == provisional ||
                   s.title.rfind(tr().untitled, 0) == 0;
        }
    }
    return false;
}

//Newest activity first; ties fall back to creation time, then id.
bool sessionNewer(const Session& a, const Session& b)
{
    if (a.updatedAt != b.updatedAt)
        return a.updatedAt > b.updatedAt;
    if (a.createdAt != b.createdAt)
        return a.createdAt > b.createdAt;
    return a.id > b.id;
}

std::string exportTimestamp(uint64_t ms)
{
    std::time_t t = (std::time_t)(ms / 1000);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif

    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tmv) == 0)
        return "session";
    return buf;
}

std::string sanitizeExportName(std::string name)
{
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name)
    {
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
            continue;
        out += (char)c;
    }
    out = trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    if (utf8Length(out) > 80)
        out = utf8Prefix(out, 80);
    return out;
}

struct SaveDialogCtx
{
    std::function<void(const std::string&)> done;
    SDL_DialogFileFilter filter{};
};

void SDLCALL saveFilePicked(void* userdata, const char* const* files, int)
{
    std::unique_ptr<SaveDialogCtx> ctx(static_cast<SaveDialogCtx*>(userdata));
    if (files && files[0] && files[0][0] && ctx->done)
        ctx->done(files[0]);
}

bool isUtf8Continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

//Clipboard text from Windows apps usually has CRLF line endings and may carry
//other control characters. Normalize it and cap the size so a huge paste
//cannot stall the text layout.
std::string sanitizePastedText(const std::string& in)
{
    constexpr std::size_t kMaxPasteBytes = 256 * 1024;

    std::string out;
    out.reserve(std::min<std::size_t>(in.size(), kMaxPasteBytes));

    std::size_t i = 0;
    //Drop a UTF-8 BOM if the source app included one.
    if (in.size() >= 3 && (unsigned char)in[0] == 0xEF &&
        (unsigned char)in[1] == 0xBB && (unsigned char)in[2] == 0xBF)
        i = 3;

    bool truncated = false;
    for (; i < in.size(); ++i)
    {
        unsigned char c = (unsigned char)in[i];

        if (c == '\r')
        {
            //CRLF becomes LF; a lone CR also becomes LF.
            if (i + 1 < in.size() && in[i + 1] == '\n')
                continue;
            c = '\n';
        }
        else if (c < 0x20 && c != '\n' && c != '\t')
        {
            continue;
        }
        else if (c == 0x7F)
        {
            continue;
        }

        if (out.size() >= kMaxPasteBytes)
        {
            truncated = true;
            break;
        }
        out.push_back((char)c);
    }

    if (truncated)
    {
        //Do not end on a half-written UTF-8 sequence.
        std::size_t cont = 0;
        while (!out.empty() && isUtf8Continuation((unsigned char)out.back()))
        {
            out.pop_back();
            ++cont;
        }
        if (!out.empty())
        {
            unsigned char lead = (unsigned char)out.back();
            std::size_t need = 1;
            if ((lead & 0xE0) == 0xC0)
                need = 2;
            else if ((lead & 0xF0) == 0xE0)
                need = 3;
            else if ((lead & 0xF8) == 0xF0)
                need = 4;
            if (cont + 1 < need)
                out.pop_back();
        }

        out += "\n\n";
        out += tr().pasteTruncated;
    }

    return out;
}

Lang detectSystemLanguage()
{
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    if (!locales)
        return Lang::En;

    Lang result = Lang::En;
    for (int i = 0; i < count && locales[i]; ++i)
    {
        const char* language = locales[i]->language;
        if (language && std::string(language).rfind("zh", 0) == 0)
        {
            result = Lang::Zh;
            break;
        }
    }
    SDL_free(locales);
    return result;
}

} //namespace

App::App(bool openSettingsOnStart, std::string initialSessionId)
    : config_(loadConfig(paths::configFile()))
    , store_(paths::fromUtf8(config_.sessionsDir.empty()
                                 ? paths::toUtf8(paths::defaultSessionsDir())
                                 : config_.sessionsDir))
{
    paths::setDataDir(
        paths::resolveDir(config_.dataDir, paths::prefDir()));

    //First launch: write a default config.json right away instead of waiting
    //for the user to open and save the settings dialog.
    {
        std::error_code ec;
        bool firstRun = !std::filesystem::exists(paths::configFile(), ec);
        if (firstRun)
        {
            config_.lang = detectSystemLanguage();

            Config defaults = config_;
            defaults.dataDir = paths::toUtf8(paths::dataDir());
            if (defaults.sessionsDir.empty())
                defaults.sessionsDir =
                    paths::toUtf8(paths::defaultSessionsDir());

            std::string cfgErr;
            if (!saveConfig(paths::configFile(), defaults, cfgErr) &&
                !cfgErr.empty())
                log::writef("%s", cfgErr.c_str());

            config_ = std::move(defaults);
        }
    }

    setLang(config_.lang);

    gui_.setFPS(60);
    //Desktop UI zoom is fixed at 100% so every machine gets the same layout.
    //Mobile applies the device density after the window exists.
    gui_.setUiScale(1.0f);

    {
        std::string fontErr;
        if (!font::apply(config_, fontErr) && !fontErr.empty())
            log::writef("%s", fontErr.c_str());
    }

    agentEventType_ = SDL_RegisterEvents(6);
    titleEventType_ = agentEventType_ ? agentEventType_ + 1 : 0;
    logEventType_ = agentEventType_ ? agentEventType_ + 2 : 0;
    cameraEventType_ = agentEventType_ ? agentEventType_ + 3 : 0;
    imeEventType_ = agentEventType_ ? agentEventType_ + 4 : 0;
    filePickEventType_ = agentEventType_ ? agentEventType_ + 5 : 0;
    camera::setEventType(cameraEventType_);
    ime::setEventType(imeEventType_);
    filepick::setEventType(filePickEventType_);
    engine_.setUserEventType(agentEventType_);
    gui_.onUserEvent = [this](const SDL_Event& ev)
    {
        if (ev.type == agentEventType_)
            onAgentEvents();
        else if (titleEventType_ && ev.type == titleEventType_)
            processTitleResults();
        else if (logEventType_ && ev.type == logEventType_ &&
                 settingsDialog_)
            settingsDialog_->poll();
        else if (cameraEventType_ && ev.type == cameraEventType_)
            handleCameraResult();
        else if (imeEventType_ && ev.type == imeEventType_)
            handleImeInset();
        else if (filePickEventType_ && ev.type == filePickEventType_)
            handleFilePickResult();
    };
    if (logEventType_)
        logTimer_ = SDL_AddTimer(200, &App::logTimerProc, this);

    if (paths::isMobile())
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");

    gui_.onBackPressed = [this]
    {
        if (agentsDialog_ && agentsDialog_->isOpen())
        {
            agentsDialog_->close();
            return;
        }
        if (settingsDialog_ && settingsDialog_->isOpen())
        {
            settingsDialog_->back();
            return;
        }
        if (drawer_ && drawer_->isOpen())
        {
            drawer_->close();
            if (win_)
                win_->requestRedraw();
            return;
        }
        gui_.quit();
    };

    win_ = &gui_.addWindow(kAppName, 1180, 760);
    if (paths::isMobile())
    {
        //Android: no custom titlebar; scale the UI by the display density.
        win_->setTitleBarMode(sg::Window::TitleBarMode::Hidden);
        float density = SDL_GetWindowDisplayScale(win_->handle());
        if (density > 0.5f)
        {
            gui_.setUiScale(density);
            //Rebuild fonts at the new raster scale.
            std::string fontErr;
            if (!font::apply(config_, fontErr) && !fontErr.empty())
                log::writef("%s", fontErr.c_str());
        }
    }
    else
    {
        win_->setTitleBarMode(sg::Window::TitleBarMode::Custom);
        win_->setTitleBarHeight(36);
    }
    win_->setResizable(!paths::isMobile());

    //Embedded icon: one copy for the custom titlebar, one for taskbar/dock.
    win_->setTitleIconMemory(sdlagent::icon::kPng,
                             sdlagent::icon::kPngSize);
    win_->setTitleIconSize(22);
    win_->setTitleIconPadding(10, 8);
    win_->setWindowIconMemory(sdlagent::icon::kPng,
                              sdlagent::icon::kPngSize);

    buildUi();
    buildDialogs();
    applyTheme();

    if (chat_)
    {
        chat_->setSessionsRoot(store_.root());
        bool foundModel = false;
        bool imageSupport = catalog::supportsImage(config_.model, &foundModel);
        chat_->setImageInputSupported(!foundModel || imageSupport);
    }

    prompt::ensureFiles();
    attachment::ensureConfig();
    catalog::ensureBundled();

    std::string err;
    if (!store_.loadAll(err) && !err.empty())
        log::writef("%s", err.c_str());
    if (!err.empty())
        log::writef("%s", err.c_str());

    //Do not persist an empty "new chat" placeholder. Remove placeholder
    //files left by older versions and keep at most one in memory.
    bool hasReal = false;
    std::vector<std::string> drafts;
    for (const auto& s : store_.sessions())
    {
        if (isNewConversation(s))
            drafts.push_back(s.id);
        else
            hasReal = true;
    }

    if (hasReal || !drafts.empty())
    {
        for (const auto& id : drafts)
            store_.remove(id, err);
    }

    if (store_.sessions().empty())
    {
        Session& s = store_.create();
        s.title = tr().untitled;
    }

    for (auto& s : store_.sessions())
    {
        if (s.title.empty())
            s.title = tr().untitled;
    }

    //No session argument: always open a fresh conversation. The draft is
    //in-memory only and stays hidden from the sidebar until its first message.
    if (!initialSessionId.empty())
    {
        if (store_.find(initialSessionId))
            currentId_ = initialSessionId;
        else
            log::writef("session not found: %s",
                        initialSessionId.c_str());
    }
    else
    {
        currentId_.clear();
    }

    refreshSidebar();
    if (!currentId_.empty())
        selectSession(currentId_);
    else
        createSession();

    gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr);

    if (openSettingsOnStart)
        openSettings();

    lastRefresh_ = std::chrono::steady_clock::now();
}

App::~App()
{
    if (logTimer_)
    {
        SDL_RemoveTimer(logTimer_);
        logTimer_ = 0;
    }
    stopTitleWorker();
}

Uint32 SDLCALL App::logTimerProc(void* userdata, SDL_TimerID, Uint32)
{
    auto* app = static_cast<App*>(userdata);
    if (app && app->logEventType_)
    {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = app->logEventType_;
        SDL_PushEvent(&ev);
    }
    return 200;
}

int App::run()
{
    SDL_SetEventFilter(&App::eventFilter, this);
    gui_.run();
    SDL_SetEventFilter(nullptr, nullptr);
    return 0;
}

bool SDLCALL App::eventFilter(void* userdata, SDL_Event* event)
{
    App* app = static_cast<App*>(userdata);
    return app && event ? app->handleAppEvent(*event) : true;
}

bool App::handleAppEvent(SDL_Event& ev)
{
    if (ev.type == SDL_EVENT_KEY_DOWN)
    {
        const SDL_Keymod mod = (SDL_Keymod)ev.key.mod;
        const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
        const bool other = (mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT |
                                   SDL_KMOD_GUI)) != 0;

        //Intercept Ctrl+V only for file clipboards; text stays with SDLGui so
        //its PasteFilter hook performs the insertion.
        if (ev.key.key == SDLK_V && ctrl && !other)
            return pasteFromClipboard();
        return true;
    }

    if (ev.type == SDL_EVENT_DROP_FILE)
    {
        handleDroppedFile(ev.drop.data);
        return true;
    }

    return true;
}

bool App::pasteFromClipboard()
{
    sg::Widget* focus = gui_.focused();
    if (!focus)
        return true;

    //Files copied in a file manager attach only when the composer has focus.
    if (chat_ && focus == chat_->inputWidget() && canAttachFiles())
    {
        std::vector<std::string> files = clipboard::files();
        if (!files.empty())
        {
            attachFiles(files);
            return false; // consume Ctrl+V, SDLGui must not paste text
        }
    }

    //Text paste is left to SDLGui, which now calls the widget's paste filter.
    //Install the sanitizer lazily so every dialog text box gets it too.
    auto install = [](sg::Widget* w)
    {
        if (auto* tb = dynamic_cast<sg::TextBox*>(w))
        {
            if (!tb->pasteFilter())
                tb->setPasteFilter(sanitizePastedText);
        }
        else if (auto* ta = dynamic_cast<sg::TextArea*>(w))
        {
            if (!ta->pasteFilter())
                ta->setPasteFilter(sanitizePastedText);
        }
        else if (auto* cb = dynamic_cast<sg::ComboBox*>(w))
        {
            if (!cb->pasteFilter())
                cb->setPasteFilter(sanitizePastedText);
        }
    };
    install(focus);
    return true;
}

void App::handleDroppedFile(const char* path)
{
    if (!path || !*path || !canAttachFiles())
        return;
    attachFiles({ path });
}

bool App::canAttachFiles() const
{
    if (!chat_ || !chat_->visible())
        return false;

    //Only the chat page accepts files. Dialogs and the mobile drawer are
    //overlays, so actions there must do nothing.
    if (settingsDialog_ && settingsDialog_->isOpen())
        return false;
    if (inputDialog_ && inputDialog_->isOpen())
        return false;
    if (confirmDialog_ && confirmDialog_->isOpen())
        return false;
    if (drawer_ && drawer_->isOpen())
        return false;
    return true;
}

void App::attachFiles(const std::vector<std::string>& paths)
{
    if (!chat_)
        return;

    std::string firstError;
    for (const std::string& path : paths)
    {
        std::string err;
        chat_->addPickedFile(path, err);
        if (firstError.empty() && !err.empty())
            firstError = err;
    }

    if (!firstError.empty() && win_)
        win_->showMessageBox(tr().attach, firstError.c_str(),
                             sg::MessageBoxType::Warning,
                             sg::MessageBoxButtons::OK, nullptr);
}

void App::buildUi()
{
    sg::Widget& root = win_->root();

    phoneUI_ = paths::isMobile() && root.width() < 600;

    mainSplit_ = root.addChild<sg::SplitView>();
    mainSplit_->pos(0, 0)->sz(root.width(), root.height());
    mainSplit_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                               sg::AnchorRight | sg::AnchorBottom,
                           0, 0, 0, 0);
    mainSplit_->setOrientation(sg::SplitView::Orientation::SideBySide);
    mainSplit_->setSplit(0.23f);
    mainSplit_->setMinPaneSize(180);
    mainSplit_->setDividerWidth(1);
    mainSplit_->setDividerHitWidth(8);

    sidebar_ = mainSplit_->addChild<ui::Panel>();
    sidebar_->setColors(sg::Color(30, 31, 35), sg::Color(0, 0, 0, 0), 0);

    sg::Widget* content = mainSplit_->addChild<sg::Widget>();

    newChatButton_ = sidebar_->addChild<sg::Button>();
    newChatButton_->pos(10, 10)->sz(180, 38);
    newChatButton_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight,
                               10, 10, phoneUI_ ? 40 : 10, 0);
    newChatButton_->setText(tr().newChat);
    newChatButton_->onClick = [this](sg::MouseEvent&) { createSession(); };

    ui::VLine* sep = sidebar_->addChild<ui::VLine>();
    sep->pos(0, 58)->sz(200, 1);
    sep->setAnchors(sg::AnchorLeft | sg::AnchorRight | sg::AnchorTop,
                    0, 58, 0, 0);
    sep->setColor(sg::Color(0, 0, 0, 255));

    sessionList_ = sidebar_->addChild<ui::SessionList>();
    sessionList_->pos(0, 64)->sz(200, 300);
    sessionList_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                 sg::AnchorRight | sg::AnchorBottom,
                             0, 64, 0, 54);
    sessionList_->onSelect = [this](const std::string& id)
    { selectSession(id); };
    sessionList_->onSessionContextMenu =
        [this](const std::string& id, sg::ContextMenu& menu)
    {
        Session* s = store_.find(id);
        if (!s)
            return;
        if (isNewConversation(*s))
            return;
        menu.addItem(s->pinned ? tr().unpin : tr().pin,
                     [this, id] { togglePin(id); });
        menu.addItem(tr().rename, [this, id] { renameSession(id); });
        menu.addSeparator();
        menu.addItem(tr().exportSession, [this, id] { exportSession(id); });
        menu.addSeparator();
        menu.addItem(tr().remove, [this, id] { requestDeleteSession(id); });
    };

    settingsButton_ = sidebar_->addChild<sg::Button>();
    settingsButton_->pos(10, 0)->sz(180, 36);
    settingsButton_->setAnchors(sg::AnchorLeft | sg::AnchorRight |
                                    sg::AnchorBottom,
                                10, 0, 10, 10);
    settingsButton_->setText(tr().settings);
    settingsButton_->onClick = [this](sg::MouseEvent&) { openSettings(); };

    contentTopbar_ = content->addChild<ui::Panel>();
    contentTopbar_->pos(0, 0)->sz(400, 42);
    contentTopbar_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                   sg::AnchorRight,
                               0, 0, 0, 0);
    contentTopbar_->setColors(sg::Color(30, 31, 35),
                              sg::Color(0, 0, 0, 0), 0);

    //Separator under the topbar. A Panel border would also draw a vertical
    //line on the left, doubling the split divider.
    contentTopbarLine_ = contentTopbar_->addChild<ui::VLine>();
    contentTopbarLine_->pos(0, 41)->sz(400, 1);
    contentTopbarLine_->setAnchors(sg::AnchorLeft | sg::AnchorRight |
                                       sg::AnchorBottom,
                                   0, 0, 0, 0);
    contentTopbarLine_->setColor(sg::Color(0, 0, 0, 255));

    toggleSidebar_ = contentTopbar_->addChild<ui::ArrowButton>();
    toggleSidebar_->pos(8, 7)->sz(32, 28);
    toggleSidebar_->setDirection(true);
    toggleSidebar_->onClick = [this](sg::MouseEvent&) { toggleSidebar(); };

    titleLabel_ = contentTopbar_->addChild<sg::Label>();
    titleLabel_->pos(50, 12)->sz(300, 20);
    titleLabel_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                sg::AnchorRight,
                            50, 12, 16, 0);
    titleLabel_->setText(tr().untitled);

    chat_ = content->addChild<ui::ChatPanel>();
    chat_->pos(0, 42)->sz(400, 300);
    chat_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                          sg::AnchorBottom,
                      0, 42, 0, 0);
    chat_->onSend = [this](const std::string& text) { sendMessage(text); };
    chat_->onCancel = [this] { cancelRun(); };
    chat_->onNewChat = [this] { createSession(); };
    chat_->onOpenSettings = [this] { openSettings(); };
    chat_->onCamera = [this] { camera::capture(); };
    chat_->onWebSearchToggle = [this](bool enabled)
    {
        config_.webSearch = enabled;
        std::string err;
        if (!saveConfig(paths::configFile(), config_, err) && !err.empty())
            log::writef("%s", err.c_str());
    };
    chat_->setWebSearchEnabled(config_.webSearch);
    updateWebSearchAvailability();
    chat_->onConfirmation = [this](ui::ChatPanel::ConfirmChoice choice)
    {
        if (!engine_.busy())
            return;

        engine_.resolveConfirmation(
            choice == ui::ChatPanel::ConfirmChoice::Allow);

        if (chat_)
            chat_->clearConfirmation();
        gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr);
    };

    if (phoneUI_)
    {
        //Phone: move the same sidebar into an overlay drawer.
        drawer_ = root.addChild<sg::Drawer>();
        drawer_->pos(0, 0)->sz(root.width(), root.height());
        drawer_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                sg::AnchorRight | sg::AnchorBottom,
                            0, 0, 0, 0);
        drawer_->setSide(sg::Drawer::Side::Left);
        drawer_->setPanelWidth(
            std::max(240, std::min(320, root.width() - 56)));
        drawer_->setCloseButtonVisible(false);

        sidebar_->reparent(drawer_->panel());
        sidebar_->setPosition(0, 0);
        sidebar_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                 sg::AnchorRight | sg::AnchorBottom,
                             0, 0, 0, 0);

        //A square close button inside the panel's top-right corner. The
        //built-in X is hidden; the new-chat button leaves room on the right.
        const int tabSize = 30;
        drawerTab_ = drawer_->panel()->addChild<ui::ArrowButton>();
        drawerTab_->setDirection(ui::ArrowButton::Direction::Right);
        drawerTab_->pos(drawer_->panelWidth() - tabSize - 8, 14)
            ->sz(tabSize, tabSize);
        drawerTab_->onClick = [this](sg::MouseEvent&)
        {
            if (drawer_)
                drawer_->close();
            if (win_)
                win_->requestRedraw();
        };
        drawer_->onOpenChanged = [this](bool open)
        {
            if (drawerTab_)
            {
                const int tabSize = 30;
                drawerTab_->setPosition(drawer_->panelWidth() - tabSize - 8,
                                        14);
            }
            if (win_)
                win_->requestRedraw();
        };
    }
}

void App::buildDialogs()
{
    sg::Widget& root = win_->root();

    createSettingsDialog();

    inputDialog_ = std::make_unique<ui::InputDialog>(
        &root,
        [this] { gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr); });

    confirmDialog_ = std::make_unique<ui::ConfirmDialog>(
        &root,
        [this] { gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr); });
}

void App::applyTheme()
{
    applyTheme(config_.theme);
}

void App::applyTheme(const std::string& themeName)
{
    sg::Theme theme = makeTheme(themeName == "light");
    win_->setTheme(theme);

    if (mainSplit_)
        mainSplit_->setDividerColor(sg::Color(0, 0, 0, 255));
    if (drawer_)
    {
        drawer_->setPanelColor(theme.panelBg);
        drawer_->setScrimColor(sg::Color(0, 0, 0, 60));
    }
    if (drawerTab_)
        drawerTab_->setColors(theme.panelBg, theme.buttonHover,
                              theme.buttonPressed, theme.text,
                              sg::Color(0, 0, 0, 255));
    if (sidebar_)
        sidebar_->setColors(theme.panelBg, theme.panelBorder, 0);
    if (contentTopbar_)
        contentTopbar_->setColors(theme.panelBg, sg::Color(0, 0, 0, 0), 0);
    if (contentTopbarLine_)
        contentTopbarLine_->setColor(sg::Color(0, 0, 0, 255));
    if (sessionList_)
    {
        bool dark = themeName != "light";
        sg::Color selectedRow = dark ? sg::Color(232, 222, 170)
                                     : theme.rowSelected;
        sg::Color selectedTxt = dark ? sg::Color(26, 24, 0)
                                     : theme.rowSelectedText;
        sg::Color selectedBar = dark
                                    ? sg::Color(255, 242, 0)
                                    : sg::Color(45, 95, 170);
        sessionList_->setThemeColors(theme.text, theme.mutedText,
                                     theme.rowHover, selectedRow,
                                     selectedBar,
                                     selectedTxt, theme.border,
                                     theme.scrollTrack, theme.scrollThumb);
    }
    if (chat_)
        chat_->applyTheme(theme);
    if (newChatButton_)
    {
        auto shade = [](sg::Color c, int delta)
        {
            auto clamp = [](int v)
            {
                return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            };
            return sg::Color(clamp(c.r + delta), clamp(c.g + delta),
                             clamp(c.b + delta), c.a);
        };
        newChatButton_->setBgColor(theme.menuAccent,
                                   shade(theme.menuAccent, 12),
                                   shade(theme.menuAccent, -30));
        int accentLum = (theme.menuAccent.r * 299 + theme.menuAccent.g * 587 +
                         theme.menuAccent.b * 114) / 1000;
        newChatButton_->setTextColor(accentLum > 150
                                         ? sg::Color(20, 19, 0)
                                         : sg::Color(255, 255, 255));
        newChatButton_->setBorderColor(theme.menuAccent);
    }
    if (toggleSidebar_)
    {
        toggleSidebar_->setColors(theme.panelBg, theme.buttonHover,
                                  theme.buttonPressed, theme.text);
    }
    if (settingsDialog_)
        settingsDialog_->applyTheme(theme);
    if (agentsDialog_)
        agentsDialog_->applyTheme(theme);
    if (inputDialog_)
        inputDialog_->applyTheme(theme);
    if (confirmDialog_)
        confirmDialog_->applyTheme(theme);

    layoutTopbar();

    win_->requestRedraw();
}

void App::layoutTopbar()
{
    if (!contentTopbar_ || !chat_)
        return;

    int lh = sg::currentFont() ? sg::currentFont()->lineHeight() : 18;
    if (lh < 18)
        lh = 18;

    int barH = std::max(42, lh + 18);
    int btnH = std::max(28, lh + 6);

    int barW = contentTopbar_->width();
    contentTopbar_->setSize(barW, barH);

    if (contentTopbarLine_)
    {
        contentTopbarLine_->setPosition(0, barH - 1);
        contentTopbarLine_->setSize(barW, 1);
    }

    if (toggleSidebar_)
    {
        toggleSidebar_->setPosition(8, (barH - btnH) / 2);
        toggleSidebar_->setSize(32, btnH);
    }

    if (titleLabel_)
    {
        //Keep the right anchor so window resizes still work.
        titleLabel_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                    sg::AnchorRight,
                                50, (barH - lh) / 2, 16, 0);
        titleLabel_->setSize(std::max(80, barW - 66), lh);
    }

    chat_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                          sg::AnchorBottom,
                      0, barH, 0, 0);
    chat_->setPosition(0, barH);
}

void App::refreshSidebar()
{
    if (!sessionList_)
        return;

    std::vector<Session*> list;
    list.reserve(store_.sessions().size());
    for (auto& s : store_.sessions())
    {
        //The in-memory "new chat" draft is not a sidebar entry until the
        //first message is sent.
        if (isNewConversation(s))
            continue;
        list.push_back(&s);
    }

    std::sort(list.begin(), list.end(), [](const Session* a, const Session* b)
    {
        if (a->pinned != b->pinned)
            return a->pinned;
        return sessionNewer(*a, *b);
    });

    std::vector<ui::SessionListItem> items;
    items.reserve(list.size());
    for (const Session* s : list)
        items.push_back({ s->id, s->title, s->pinned });

    sessionList_->setItems(std::move(items), currentId_);
}

Session* App::currentSession()
{
    return currentId_.empty() ? nullptr : store_.find(currentId_);
}

Session* App::ensureSession()
{
    std::string loadErr;

    if (Session* s = currentSession())
    {
        std::string id = s->id;
        if (store_.loadSession(id, loadErr))
            return s;
        store_.forget(id);
        if (id == currentId_)
            currentId_.clear();
    }

    while (true)
    {
        Session* best = nullptr;
        for (auto& cand : store_.sessions())
            if (!best || sessionNewer(cand, *best))
                best = &cand;

        if (!best)
        {
            Session& created = store_.create();
            created.title = tr().untitled;
            currentId_ = created.id;
            refreshSidebar();
            return &store_.sessions().back();
        }

        std::string err;
        if (store_.loadSession(best->id, err))
        {
            currentId_ = best->id;
            refreshSidebar();
            return store_.find(best->id);
        }

        store_.forget(best->id);
    }

    return nullptr;
}

void App::selectSession(const std::string& id)
{
    Session* s = store_.find(id);
    if (!s)
        return;

    std::string loadErr;
    if (!store_.loadSession(id, loadErr))
    {
        //The file was removed or corrupted while the app was running.
        store_.forget(id);
        if (id == currentId_)
            currentId_.clear();
        refreshSidebar();
        if (win_)
            win_->showMessageBox(tr().appTitle, loadErr.c_str(),
                                 sg::MessageBoxType::Warning,
                                 sg::MessageBoxButtons::OK, nullptr);

        //Missing/corrupt session: start a fresh conversation instead of
        //jumping to an unrelated session.
        createSession();
        return;
    }

    std::set<std::string> keep{ id };
    if (!runningId_.empty())
        keep.insert(runningId_);
    store_.unloadAllExcept(keep);

    currentId_ = id;
    if (chat_)
    {
        chat_->setSession(s);
        chat_->setShowReasoning(config_.showReasoning);
        chat_->setModelName(config_.model);
        updateUsageUi();
    }
    if (sessionList_)
        sessionList_->setActive(id);
    if (drawer_ && drawer_->isOpen())
        drawer_->close();
    updateTopbar();
    if (win_)
        win_->requestRedraw();
}

void App::updateTopbar()
{
    Session* s = currentSession();
    std::string title = s && !s->title.empty() ? s->title : tr().untitled;

    int avail = contentTopbar_ ? contentTopbar_->width() - 50 - 20 : 600;
    avail = std::max(120, avail);

    if (titleLabel_)
        titleLabel_->setText(ui::ellipsize(title, avail));
}

void App::createSession()
{
    Session* current = currentSession();
    if (current && current->messages.empty() && !current->hasMessages &&
        (current->title.empty() ||
         current->title.rfind(tr().untitled, 0) == 0))
    {
        //already on an empty new conversation: keep it
        if (drawer_ && drawer_->isOpen())
            drawer_->close();
        if (chat_)
            gui_.setFocus(chat_->inputWidget());
        return;
    }

    for (auto& existing : store_.sessions())
    {
        if (isNewConversation(existing))
        {
            existing.updatedAt = nowMs();
            currentId_ = existing.id;
            refreshSidebar();
            selectSession(existing.id);
            if (drawer_ && drawer_->isOpen())
                drawer_->close();
            if (chat_)
                gui_.setFocus(chat_->inputWidget());
            return;
        }
    }

    Session& s = store_.create();
    s.title = nextUntitledTitle();

    currentId_ = s.id;
    refreshSidebar();
    selectSession(s.id);
    if (chat_)
        gui_.setFocus(chat_->inputWidget());
}

std::string App::nextUntitledTitle() const
{
    const std::string base = tr().untitled;
    std::set<std::string> used;
    for (const auto& s : store_.sessions())
        used.insert(s.title);

    if (!used.count(base))
        return base;

    for (int i = 2;; ++i)
    {
        std::string candidate = base + " " + std::to_string(i);
        if (!used.count(candidate))
            return candidate;
    }
}

void App::toggleSidebar()
{
    if (drawer_)
    {
        drawer_->toggle();
        if (win_)
            win_->requestRedraw();
        return;
    }
    if (!sidebar_ || !mainSplit_)
        return;
    sidebar_->setVisible(!sidebar_->visible());
    if (toggleSidebar_)
        toggleSidebar_->setDirection(sidebar_->visible());
    mainSplit_->relayout();
    win_->requestRedraw();
}

bool App::loadSessionForAction(const std::string& id)
{
    std::string err;
    if (store_.loadSession(id, err))
        return true;

    bool wasCurrent = (id == currentId_);
    store_.forget(id);
    refreshSidebar();
    if (win_)
        win_->showMessageBox(tr().appTitle, err.c_str(),
                             sg::MessageBoxType::Warning,
                             sg::MessageBoxButtons::OK, nullptr);

    if (wasCurrent)
    {
        currentId_.clear();
        createSession();
    }
    return false;
}

void App::togglePin(const std::string& id)
{
    Session* s = store_.find(id);
    if (!s || isNewConversation(*s))
        return;
    if (!loadSessionForAction(id))
        return;
    s = store_.find(id);
    if (!s)
        return;

    s->pinned = !s->pinned;
    std::string err;
    store_.save(*s, err);
    refreshSidebar();
    if (sessionList_)
        sessionList_->setActive(currentId_);
    if (id != currentId_ && id != runningId_)
        store_.unload(id);
}

void App::renameSession(const std::string& id)
{
    Session* s = store_.find(id);
    if (!s || !inputDialog_ || isNewConversation(*s))
        return;
    if (!loadSessionForAction(id))
        return;
    s = store_.find(id);
    if (!s)
        return;

    inputDialog_->open(tr().renameTitle, s->title,
                       [this, id](const std::string& value)
    {
        if (!loadSessionForAction(id))
            return;
        Session* target = store_.find(id);
        if (!target)
            return;
        target->title = value;
        std::string err;
        store_.save(*target, err);
        refreshSidebar();
        if (id == currentId_)
            updateTopbar();
        if (id != currentId_ && id != runningId_)
            store_.unload(id);
    });
    gui_.setFocus(inputDialog_->firstField());
}

void App::exportSession(const std::string& id)
{
    Session* s = store_.find(id);
    if (!s || isNewConversation(*s))
        return;
    if (!loadSessionForAction(id))
        return;
    s = store_.find(id);
    if (!s)
        return;

    std::string base = sanitizeExportName(s->title);
    if (base.empty())
        base = exportTimestamp(nowMs());

    std::filesystem::path suggested =
        store_.root() / paths::fromUtf8(base + ".json");

    std::string snapshot = toJson(*s).dump(true);
    if (id != currentId_ && id != runningId_)
        store_.unload(id);

    auto* ctx = new SaveDialogCtx;
    ctx->filter.name = tr().exportFilter;
    ctx->filter.pattern = "json";
    ctx->done = [this, snapshot](const std::string& chosen)
    {
        std::filesystem::path path = paths::fromUtf8(chosen);
        if (path.extension().empty())
            path += ".json";

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (win_)
                win_->showMessageBox(
                    tr().exportSession,
                    (std::string(tr().exportFailed) + paths::toUtf8(path))
                        .c_str(),
                    sg::MessageBoxType::Warning, sg::MessageBoxButtons::OK,
                    nullptr);
            return;
        }

        out.write(snapshot.data(), (std::streamsize)snapshot.size());
        if (!out)
        {
            if (win_)
                win_->showMessageBox(
                    tr().exportSession,
                    (std::string(tr().exportFailed) + paths::toUtf8(path))
                        .c_str(),
                    sg::MessageBoxType::Warning, sg::MessageBoxButtons::OK,
                    nullptr);
            return;
        }

        showStatus(std::string(tr().exportDone) + paths::toUtf8(path));
    };

    SDL_ShowSaveFileDialog(saveFilePicked, ctx,
                           win_ ? win_->handle() : nullptr, &ctx->filter, 1,
                           paths::toUtf8(suggested).c_str());
}

void App::requestDeleteSession(const std::string& id)
{
    Session* target = store_.find(id);
    if (target && isNewConversation(*target))
        return;

    if (id == runningId_)
    {
        win_->showMessageBox(tr().appTitle,
                             "这个会话正在回复，暂时不能删除。",
                             sg::MessageBoxType::Warning,
                             sg::MessageBoxButtons::OK, nullptr);
        return;
    }

    win_->showMessageBox(tr().deleteTitle, tr().deleteConfirm,
                         sg::MessageBoxType::Question,
                         sg::MessageBoxButtons::YesNo,
                         [this, id](sg::MessageBoxResult result)
    {
        if (result == sg::MessageBoxResult::Yes)
            deleteSession(id);
    });
}

void App::deleteSession(const std::string& id)
{
    Session* target = store_.find(id);
    if (target && isNewConversation(*target))
        return;

    std::string err;
    store_.remove(id, err);
    if (!err.empty())
        log::writef("%s", err.c_str());

    if (id == currentId_)
    {
        currentId_.clear();
        if (store_.sessions().empty())
            createSession();
        else
        {
            Session* best = nullptr;
            for (auto& s : store_.sessions())
                if (!best || sessionNewer(s, *best))
                    best = &s;
            if (best)
                selectSession(best->id);
        }
    }
    refreshSidebar();
}

AgentRunConfig App::makeRunConfig(const std::string& sessionId)
{
    const Session* session = store_.find(sessionId);
    const std::string workspace =
        (session && !session->workspace.empty()) ? session->workspace
                                                 : effectiveWorkspace();
    const std::string agents =
        session ? session->agentsText : std::string();
    const uint64_t startedAt = session ? session->createdAt : 0;

    AgentRunConfig rc;
    rc.baseUrl = config_.baseUrl;
    rc.apiKey = config_.apiKey;
    rc.model = config_.model;
    rc.reasoningEffort = config_.reasoningEffort;
    rc.apiProtocol = config_.apiProtocol;
    rc.headers = provider::headersFor(config_.baseUrl, config_.model,
                                      sessionId);
    rc.temperature = config_.temperature;
    rc.maxTokens = config_.maxTokens;
    rc.workspace = paths::fromUtf8(workspace);
    rc.sessionsRoot = store_.root();
    bool foundModel = false;
    rc.imageInput = catalog::supportsImage(config_.model, &foundModel);
    if (!foundModel)
        rc.imageInput = true;
    rc.toolMode = config_.toolMode;

    //Re-resolve on every run so a profile or model switch can never leave a
    //stale "supports search" flag behind.
    native_search::Plan plan = native_search::resolve(
        config_.baseUrl, config_.model, config_.apiProtocol);

    //Keep the server-side search tool declared for the whole conversation so
    //the tools array is stable; a notice tells the model whether it may be
    //used. OpenAI Chat search models always search, so that parameter stays
    //tied to the toggle.
    if (plan.mode == native_search::Plan::Mode::RawTool)
        rc.nativeWebTool = plan.toolJson;
    else if (plan.mode == native_search::Plan::Mode::ChatOptions &&
             config_.webSearch)
        rc.nativeWebChatOptions = true;

    rc.sessionStartedAt = startedAt;
    rc.agentsText = agents;

    std::string sysText = prompt::readFile(paths::sysPromptFile(),
                                           prompt::defaultSysPrompt());
    std::string toolText = prompt::readFile(paths::toolPromptFile(),
                                            prompt::defaultToolPrompt());
    rc.systemPrompt = prompt::compose(
        sysText, toolText, config_.toolMode != ToolMode::Off,
        SDL_GetPlatform(), workspace, config_.model, agents, startedAt);
    return rc;
}

void App::updateWebSearchAvailability()
{
    if (!chat_)
        return;

    native_search::Plan plan = native_search::resolve(
        config_.baseUrl, config_.model, config_.apiProtocol);
    chat_->setWebSearchAvailable(native_search::supported(plan));
}

std::string App::effectiveWorkspace() const
{
    return paths::toUtf8(paths::defaultWorkspace());
}

void App::prepareSessionPromptState(Session& s)
{
    if (s.workspace.empty())
        s.workspace = effectiveWorkspace();

    agents::Snapshot snapshot = agents::load(
        paths::globalAgentsFile(),
        paths::fromUtf8(s.workspace) / "AGENTS.md");

    if (s.agentsHash.empty())
    {
        //First send: seed the frozen text; nothing to announce yet.
        s.agentsText = snapshot.text;
        s.agentsHash = snapshot.hash;
    }
    else if (snapshot.hash != s.agentsHash)
    {
        Message notice;
        notice.role = Role::System;
        notice.hidden = true;
        notice.content = snapshot.text.empty()
                             ? std::string(tr().agentsRemoved)
                             : std::string(tr().agentsUpdated) + "\n" +
                                   snapshot.text;
        notice.timestamp = nowMs();
        s.messages.push_back(std::move(notice));
        s.agentsText = snapshot.text;
        s.agentsHash = snapshot.hash;
    }

    int web = config_.webSearch ? 1 : 0;
    if (s.webSearchState != web)
    {
        Message notice;
        notice.role = Role::System;
        notice.hidden = true;
        notice.content = std::string("[状态] ") +
                         (web ? tr().webSearchOn : tr().webSearchOff);
        notice.timestamp = nowMs();
        s.messages.push_back(std::move(notice));
        s.webSearchState = web;
    }

    int mode = (int)config_.toolMode;
    if (s.toolModeState != mode)
    {
        const char* label = config_.toolMode == ToolMode::Off
                                ? tr().toolsOff
                                : (config_.toolMode == ToolMode::Auto
                                       ? tr().toolsAuto
                                       : tr().toolsAsk);
        Message notice;
        notice.role = Role::System;
        notice.hidden = true;
        notice.content = std::string("[状态] ") + tr().toolModeLabel +
                         "：" + label;
        notice.timestamp = nowMs();
        s.messages.push_back(std::move(notice));
        s.toolModeState = mode;
    }
}

void App::sendMessage(const std::string& text)
{
    if (engine_.busy())
        return;

    std::string content = trim(text);
    std::vector<Attachment> pending =
        chat_ ? chat_->pendingAttachments() : std::vector<Attachment>();
    if (content.empty() && pending.empty())
        return;

    auto warn = [this](const std::string& message)
    {
        if (!win_)
            return;
        win_->showMessageBox(
            tr().attach, message.c_str(), sg::MessageBoxType::Warning,
            sg::MessageBoxButtons::OK,
            [this](sg::MessageBoxResult)
            {
                gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr);
            });
    };

    if (!config_.apiReady())
    {
        win_->showMessageBox(
            tr().settingsTitle, tr().apiMissing, sg::MessageBoxType::Warning,
            sg::MessageBoxButtons::OK,
            [this](sg::MessageBoxResult)
            {
                gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr);
                openSettings();
            });
        return;
    }

    bool foundModel = false;
    bool imageSupported = catalog::supportsImage(config_.model, &foundModel);
    if (!foundModel)
        imageSupported = true;

    for (const Attachment& a : pending)
    {
        if (attachment::isImage(a) && !imageSupported)
        {
            warn(tr().attachImageUnsupported);
            return;
        }
    }

    Session* s = ensureSession();
    if (!s)
        return;
    if (chat_)
    {
        chat_->setSession(s);
        chat_->setModelName(config_.model);
        updateUsageUi();
    }

    if (s->title.empty() || s->title.rfind(tr().untitled, 0) == 0)
    {
        if (!content.empty())
            s->title = firstLine(content, 40);
        else if (!pending.empty())
            s->title = firstLine(pending.front().name, 40);
    }

    std::vector<Attachment> imported;
    imported.reserve(pending.size());
    for (const Attachment& p : pending)
    {
        Attachment stored;
        std::string importErr;
        if (!attachment::importFile(paths::fromUtf8(p.path), p, store_.root(),
                                    s->id, stored, importErr))
        {
            warn(importErr);
            return;
        }
        imported.push_back(std::move(stored));
    }

    //Append state changes and AGENTS.md updates before the new user message so
    //the existing prefix is preserved and only the tail grows.
    prepareSessionPromptState(*s);

    Message msg;
    msg.role = Role::User;
    msg.content = content;
    msg.attachments = std::move(imported);
    msg.timestamp = nowMs();
    s->messages.push_back(std::move(msg));
    s->hasMessages = true;
    s->updatedAt = nowMs();
    s->model = config_.model;

    std::string err;
    store_.save(*s, err);
    if (!err.empty())
        log::writef("%s", err.c_str());

    if (chat_)
    {
        chat_->clearInput();
        chat_->clearAttachments();
        chat_->refresh(true);
    }
    refreshSidebar();
    updateTopbar();

    runningId_ = s->id;
    log::writef("request: model=%s", config_.model.c_str());
    engine_.start(s->id, s->messages, makeRunConfig(s->id));
    setBusyUi(true);
    lastRefresh_ = std::chrono::steady_clock::now();
    if (chat_)
        gui_.setFocus(chat_->inputWidget());
}

void App::cancelRun()
{
    if (!engine_.busy())
        return;
    engine_.cancel();
    showStatus(tr().statusStopping);
}

void App::setBusyUi(bool busy)
{
    if (chat_)
        chat_->setBusy(busy);
    if (win_)
        win_->requestRedraw();
}

void App::showStatus(const std::string& text)
{
    if (chat_)
        chat_->setStatus(text);
}

void App::handleCameraResult()
{
    std::string path;
    if (!camera::takeResult(path))
        return;
    if (!chat_)
        return;

    std::string err;
    chat_->addPickedFile(path, err);
    if (!err.empty())
    {
        if (win_)
            win_->showMessageBox(tr().attach, err.c_str(),
                                 sg::MessageBoxType::Warning,
                                 sg::MessageBoxButtons::OK, nullptr);
        return;
    }
    gui_.setFocus(chat_->inputWidget());
}

void App::handleFilePickResult()
{
    std::vector<std::string> paths;
    if (!filepick::take(paths) || !chat_)
        return;

    std::string firstError;
    for (const std::string& path : paths)
    {
        std::string err;
        chat_->addPickedFile(path, err);
        if (firstError.empty() && !err.empty())
            firstError = err;
    }

    if (!firstError.empty() && win_)
    {
        win_->showMessageBox(tr().attach, firstError.c_str(),
                             sg::MessageBoxType::Warning,
                             sg::MessageBoxButtons::OK, nullptr);
    }
}

void App::handleImeInset()
{
    int px = 0;
    if (!ime::takeInset(px))
        return;

    applyImeInset(px);
}

void App::applyImeInset(int px)
{
    imeInsetPx_ = px;
    if (!win_ || !win_->handle())
        return;

    int ww = 0, wh = 0;
    SDL_GetWindowSize(win_->handle(), &ww, &wh);

    float scale = gui_.uiScale();
    if (scale < 0.01f)
        scale = 1.0f;

    //SDLGui compares the current client height with its un-keyboarded
    //baseline, so pass the full IME height. It will not subtract twice when
    //the OS has already resized the window.
    int logical = px > 0
                      ? (int)std::lround((double)px / (double)scale)
                      : 0;
    gui_.setKeyboardInset(logical);

    log::writef("ime px=%d win=%dx%d scale=%.2f logical=%d",
                px, ww, wh, (double)scale, logical);

    //Window::layoutRoot refreshes the SDL IME anchor after resizing the root.
    if (win_)
        win_->requestRedraw();
}

void App::updateUsageUi()
{
    if (!chat_)
        return;

    int tokens = 0;
    if (Session* s = currentSession())
    {
        for (auto it = s->messages.rbegin(); it != s->messages.rend(); ++it)
        {
            if (it->role != Role::Assistant)
                continue;
            if (it->promptTokens <= 0 && it->completionTokens <= 0)
                continue;
            //Use the prompt size (the context actually sent) so the number
            //grows with the conversation instead of jumping with each
            //answer's length.
            tokens = it->promptTokens;
            break;
        }
    }
    chat_->setContextUsage(tokens, config_.contextTokens);
}

void App::onAgentEvents()
{
    std::vector<AgentEvent> events = engine_.takeEvents();
    if (events.empty())
        return;

    bool touchedCurrent = false;
    bool force = false;

    for (const AgentEvent& ev : events)
    {
        if (ev.type == AgentEventType::ConfirmRequest)
        {
            if (chat_ && !chat_->hasConfirmation())
            {
                auto data = engine_.takeConfirmation();
                if (data)
                    chat_->setConfirmation(data->tool, data->summary,
                                           data->detail);
            }
            continue;
        }

        bool isCurrent = (ev.sessionId == currentId_);
        bool refresh = false;
        bool forceThis = false;
        handleAgentEvent(ev, refresh, forceThis);
        if (isCurrent && refresh)
        {
            touchedCurrent = true;
            force = force || forceThis;
        }
    }

    if (!touchedCurrent || !chat_)
        return;

    auto now = std::chrono::steady_clock::now();
    bool due = force ||
               (now - lastRefresh_ > std::chrono::milliseconds(70));

    if (due)
    {
        //Do not force a jump to the bottom here: the user may have scrolled
        //up to read something while the answer was streaming. ChatPanel only
        //follows new content when the view was already near the bottom.
        chat_->refresh(false);
        lastRefresh_ = now;
    }
}

void App::handleAgentEvent(const AgentEvent& ev, bool& refresh, bool& force)
{
    Session* s = store_.find(ev.sessionId);
    if (!s)
        return;

    auto findLive = [&]() -> Message*
    {
        if (!s->messages.empty() && s->messages.back().streaming)
            return &s->messages.back();
        return nullptr;
    };

    auto ensureLive = [&]() -> Message*
    {
        if (Message* m = findLive())
            return m;
        Message m;
        m.role = Role::Assistant;
        m.streaming = true;
        m.timestamp = nowMs();
        s->messages.push_back(std::move(m));
        return &s->messages.back();
    };

    auto saveSession = [&]()
    {
        std::string err;
        store_.save(*s, err);
        if (!err.empty())
            log::writef("%s", err.c_str());
    };

    switch (ev.type)
    {
        case AgentEventType::TextDelta:
            ensureLive()->content += ev.text;
            s->updatedAt = nowMs();
            refresh = true;
            break;

        case AgentEventType::ThinkingDelta:
            ensureLive()->reasoning += ev.text;
            s->updatedAt = nowMs();
            refresh = true;
            break;

        case AgentEventType::AssistantDone:
        {
            Message* live = findLive();
            if (live)
            {
                *live = ev.message;
                live->streaming = false;
            }
            else
            {
                Message copy = ev.message;
                copy.streaming = false;
                s->messages.push_back(std::move(copy));
            }
            log::writef("assistant done: content=%zu reasoning=%zu tools=%s",
                        ev.message.content.size(),
                        ev.message.reasoning.size(),
                        ev.message.toolCalls.empty() ? "no" : "yes");
            if (ev.sessionId == currentId_)
                updateUsageUi();
            s->updatedAt = nowMs();
            saveSession();
            refresh = true;
            force = true;
            break;
        }

        case AgentEventType::ToolFinished:
        {
            Message* live = findLive();
            if (live)
                live->streaming = false;
            s->messages.push_back(ev.message);
            s->messages.back().streaming = false;
            s->updatedAt = nowMs();
            log::writef("tool %s %s", ev.message.toolName.c_str(),
                        ev.message.error ? "failed" : "ok");
            saveSession();
            refresh = true;
            force = true;
            break;
        }

        case AgentEventType::Error:
        {
            if (Message* live = findLive())
                live->streaming = false;

            log::writef("error: %s", ev.text.c_str());

            Message m;
            m.role = Role::Assistant;
            m.content = ev.text;
            m.error = true;
            m.timestamp = nowMs();
            s->messages.push_back(std::move(m));
            s->updatedAt = nowMs();
            saveSession();
            refresh = true;
            force = true;
            break;
        }

        case AgentEventType::Finished:
        {
            if (Message* live = findLive())
                live->streaming = false;
            s->updatedAt = nowMs();
            saveSession();
            runningId_.clear();
            setBusyUi(false);
            if (chat_)
                chat_->clearConfirmation();

            if (confirmDialog_ && confirmDialog_->isOpen())
                confirmDialog_->close();

            std::string status = ev.cancelled ? tr().statusCancelled
                                              : tr().statusReady;
            showStatus(status);
            updateUsageUi();

            if (!ev.cancelled)
                maybeStartTitleJob(*s);

            refresh = true;
            force = true;
            break;
        }

        case AgentEventType::ToolStarted:
            log::writef("tool %s start", ev.message.toolName.c_str());
            break;
        case AgentEventType::ConfirmRequest:
            break;
    }
}

void App::maybeStartTitleJob(Session& s)
{
    if (!config_.autoTitle)
        return;
    if (!config_.apiReady())
        return;
    if (s.messages.size() < 2)
        return;
    if (titleAttempted_.count(s.id))
        return;
    if (!isProvisionalTitle(s))
        return;

    std::string userText;
    std::string assistantText;

    for (const Message& m : s.messages)
    {
        if (m.role == Role::User && trim(m.content).size())
        {
            userText = m.content;
            break;
        }
    }

    for (auto it = s.messages.rbegin(); it != s.messages.rend(); ++it)
    {
        if (it->role != Role::Assistant)
            continue;
        if (!it->error)
            assistantText = it->content;
        break;
    }

    if (trim(userText).empty() || trim(assistantText).empty())
        return;

    titleAttempted_.insert(s.id);

    TitleJob job;
    job.sessionId = s.id;
    job.request.baseUrl = config_.baseUrl;
    job.request.apiKey = config_.apiKey;
    job.request.model = config_.model;
    job.request.sessionId = s.id;
    job.request.apiProtocol = config_.apiProtocol;
    job.request.headers = provider::headersFor(config_.baseUrl,
                                               config_.model, s.id);
    job.request.prompt = prompt::readFile(paths::titlePromptFile(),
                                          prompt::defaultTitlePrompt());
    job.request.userText = std::move(userText);
    job.request.assistantText = std::move(assistantText);

    {
        std::lock_guard<std::mutex> lock(titleMutex_);
        titleJobs_.push_back(std::move(job));
    }

    startTitleWorker();
    titleCv_.notify_one();
}

void App::startTitleWorker()
{
    if (titleThread_.joinable())
        return;

    titleStop_ = false;
    titleCancel_ = false;

    titleThread_ = std::thread([this]
    {
        for (;;)
        {
            TitleJob job;
            {
                std::unique_lock<std::mutex> lock(titleMutex_);
                titleCv_.wait(lock, [this]
                              { return titleStop_ || !titleJobs_.empty(); });
                if (titleStop_)
                    break;
                job = std::move(titleJobs_.front());
                titleJobs_.pop_front();
            }

            titleCancel_ = false;
            if (titleStop_)
                break;

            std::string err;
            std::string title =
                titlegen::generate(job.request, &titleCancel_, err);
            if (title.empty())
            {
                if (!err.empty())
                    log::writef("title generation: %s", err.c_str());
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(titleMutex_);
                titleResults_.push_back({ job.sessionId, std::move(title) });
            }

            if (titleEventType_)
            {
                SDL_Event ev;
                SDL_zero(ev);
                ev.type = titleEventType_;
                SDL_PushEvent(&ev);
            }
        }
    });
}

void App::stopTitleWorker()
{
    {
        std::lock_guard<std::mutex> lock(titleMutex_);
        titleStop_ = true;
        titleCancel_ = true;
    }
    titleCv_.notify_all();

    if (titleThread_.joinable())
        titleThread_.join();
}

void App::processTitleResults()
{
    std::vector<TitleResult> results;
    {
        std::lock_guard<std::mutex> lock(titleMutex_);
        results.swap(titleResults_);
    }

    bool changed = false;
    for (TitleResult& result : results)
    {
        Session* s = store_.find(result.sessionId);
        if (!s)
            continue;
        if (!loadSessionForAction(result.sessionId))
            continue;
        s = store_.find(result.sessionId);
        if (!s)
            continue;
        //Manual rename wins; only replace the provisional title.
        if (!isProvisionalTitle(*s))
            continue;

        s->title = result.title;

        std::string err;
        store_.save(*s, err);
        if (!err.empty())
            log::writef("%s", err.c_str());
        else
            log::writef("title applied: %s", s->title.c_str());

        if (s->id == currentId_)
            updateTopbar();
        changed = true;

        if (s->id != currentId_ && s->id != runningId_)
            store_.unload(s->id);
    }

    if (changed)
        refreshSidebar();
}

void App::openSettings()
{
    if (!settingsDialog_)
        return;

    int lineH = sg::currentFont() ? sg::currentFont()->lineHeight() : 0;
    //Rows are laid out from the font line height; rebuild when it changes.
    if (settingsLineHeight_ != lineH)
    {
        createSettingsDialog();
        //The constructor starts from dark defaults; apply the active theme
        //before the rebuilt dialog is shown.
        applyTheme();
    }

    settingsDialog_->open(config_);
    if (paths::isMobile())
        gui_.setFocus(nullptr);
    else
        gui_.setFocus(settingsDialog_->firstField());
}

void App::createSettingsDialog()
{
    settingsDialog_ = std::make_unique<ui::SettingsDialog>(
        &win_->root(), win_,
        [this](const Config& cfg, std::string& err)
        { return saveConfigFromDialog(cfg, err); },
        [this](const std::string& themeName) { applyTheme(themeName); },
        [this]
        {
            //Revert any unsaved theme preview.
            applyTheme();
            //Do not focus the composer here on mobile: a programmatic focus
            //starts text input and makes the IME flash on the way out.
            if (paths::isMobile())
                gui_.setFocus(nullptr);
            else
                gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr);
        });
    settingsDialog_->onEditAgents = [this]
    {
        if (agentsDialog_)
            agentsDialog_->open(paths::globalAgentsFile());
    };

    //Created after the settings modal so it renders on top of it.
    agentsDialog_ = std::make_unique<ui::AgentsDialog>(
        &win_->root(),
        [this]
        {
            if (settingsDialog_ && settingsDialog_->isOpen())
                gui_.setFocus(settingsDialog_->firstField());
            else
                gui_.setFocus(chat_ ? chat_->inputWidget() : nullptr);
        });

    settingsLineHeight_ =
        sg::currentFont() ? sg::currentFont()->lineHeight() : 0;
}

bool App::saveConfigFromDialog(const Config& cfg, std::string& err)
{
    std::filesystem::path target =
        paths::fromUtf8(cfg.sessionsDir.empty()
                            ? paths::toUtf8(paths::defaultSessionsDir())
                            : cfg.sessionsDir);
    target = normalized(target);

    if (normalized(store_.root()) != target)
    {
        std::string copyErr;
        store_.copyAllTo(target, copyErr);
        if (!copyErr.empty())
        {
            err = std::string(tr().copyFailed) + copyErr;
            return false;
        }
        store_.setRoot(target);
        if (chat_)
            chat_->setSessionsRoot(target);
    }

    Config next = cfg;
    next.sessionsDir = paths::toUtf8(target);

    std::filesystem::path dataTarget =
        normalized(paths::resolveDir(cfg.dataDir, paths::prefDir()));
    std::filesystem::path oldData = normalized(paths::dataDir());
    if (dataTarget != oldData)
    {
        std::error_code ec;
        std::filesystem::create_directories(dataTarget, ec);
        if (ec)
        {
            err = "无法创建配置目录：" + paths::toUtf8(dataTarget);
            return false;
        }

        for (const char* name :
             { "sys_prompt.md", "tool_prompt.md", "title_prompt.md",
               "api.json", "attachments.json", "gateways.json" })
        {
            std::filesystem::path src = oldData / name;
            std::filesystem::path dst = dataTarget / name;
            std::error_code fec;
            if (!std::filesystem::exists(src, fec) ||
                std::filesystem::exists(dst, fec))
                continue;

            std::filesystem::copy_file(src, dst, fec);
            if (fec)
            {
                err = std::string("复制 ") + name + " 失败：" +
                      paths::toUtf8(dst);
                return false;
            }
        }

        paths::setDataDir(dataTarget);
        prompt::ensureFiles();
    attachment::ensureConfig();
        catalog::ensureBundled();
    }
    next.dataDir = paths::toUtf8(dataTarget);

    if (!saveConfig(paths::configFile(), next, err))
        return false;

    //The font raster scale follows the current UI scale, which is derived
    //from the display; only the font settings itself change here.
    {
        std::string fontErr;
        if (!font::apply(next, fontErr))
        {
            err = fontErr;
            return false;
        }
    }

    config_ = next;

    applyTheme();
    if (chat_)
    {
        chat_->setShowReasoning(config_.showReasoning);
        chat_->setModelName(config_.model);
        if (!engine_.busy())
            chat_->setStatus("");
        bool foundModel = false;
        bool imageSupport = catalog::supportsImage(config_.model, &foundModel);
        chat_->setImageInputSupported(!foundModel || imageSupport);
    }
    updateWebSearchAvailability();
    updateUsageUi();
    updateTopbar();
    refreshSidebar();

    win_->requestRedraw();
    return true;
}

} //namespace sdlagent
