// SPDX-License-Identifier: GPL-3.0-only

#include "dialogs.hpp"

#include "i18n.hpp"
#include "font.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "ui_widgets.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace sdlagent::ui
{
namespace sg = sdlgui;

namespace
{

sg::Label* addLabel(sg::Widget* parent, int x, int y, int w,
                    const std::string& text, sg::Color color)
{
    sg::Label* l = parent->addChild<sg::Label>();
    l->pos(x, y)->sz(w, 18);
    l->setText(text);
    l->setTextColor(color);
    return l;
}

sg::TextBox* addTextBox(sg::Widget* parent, int x, int y, int w, int h = 30)
{
    sg::TextBox* tb = parent->addChild<sg::TextBox>();
    tb->pos(x, y)->sz(w, h);
    return tb;
}

void addShortcutClose(sg::Widget* w, std::function<void()> close)
{
    w->onKeyDown = [close = std::move(close)](sg::KeyEvent& e)
    {
        if (e.key == SDLK_ESCAPE)
            close();
    };
}

struct PickCtx
{
    std::shared_ptr<std::atomic<bool>> alive;
    std::function<void(const std::string&)> set;
};

void SDLCALL folderPicked(void* userdata, const char* const* files, int)
{
    auto* ctx = static_cast<PickCtx*>(userdata);
    if (ctx->alive && ctx->alive->load())
    {
        if (files && files[0] && files[0][0] && ctx->set)
            ctx->set(files[0]);
    }
    delete ctx;
}

struct SaveLogCtx
{
    std::function<void(const std::string&)> done;
    SDL_DialogFileFilter filter{};
};

void SDLCALL logSavePicked(void* userdata, const char* const* files, int)
{
    std::unique_ptr<SaveLogCtx> ctx(static_cast<SaveLogCtx*>(userdata));
    if (files && files[0] && files[0][0] && ctx->done)
        ctx->done(files[0]);
}

std::string logTimestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif

    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tmv) == 0)
        return "log";
    return buf;
}

std::string fileUrl(const std::filesystem::path& p)
{
    std::string path = paths::toUtf8(p);
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string url = "file://";
    if (path.empty() || path[0] != '/')
        url += "/";
    url += path;
    return url;
}

//Opaque modal backdrop: white for light themes, black for dark themes.
sg::Color modalBackdrop(const sg::Theme& theme)
{
    int lum = (theme.windowBg.r * 299 + theme.windowBg.g * 587 +
               theme.windowBg.b * 114) / 1000;
    return lum > 128 ? sg::Color(255, 255, 255) : sg::Color(0, 0, 0);
}

//Accept plain numbers and suffixes: 2k, 512k, 0.5m, 2万.
bool parseCount(const std::string& text, long long& value)
{
    std::string s = trim(text);
    if (s.empty())
        return false;

    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || v < 0.0 || !std::isfinite(v))
        return false;

    std::string suffix = trim(std::string(end));
    double mult = 1.0;
    if (suffix.empty())
        mult = 1.0;
    else if (suffix == "k" || suffix == "K")
        mult = 1024.0;
    else if (suffix == "m" || suffix == "M")
        mult = 1024.0 * 1024.0;
    else if (suffix == "w" || suffix == "W" || suffix == "万")
        mult = 10000.0;
    else
        return false;

    value = (long long)(v * mult);
    return true;
}

std::string tokensText(int tokens)
{
    if (tokens >= 1048576 && tokens % 1048576 == 0)
        return std::to_string(tokens / 1048576) + "m";
    if (tokens >= 1024 && tokens % 1024 == 0)
        return std::to_string(tokens / 1024) + "k";
    return std::to_string(tokens);
}

} //namespace

//==============================================================================
// SettingsDialog
//==============================================================================

SettingsDialog::SettingsDialog(sg::Widget* root, sg::Window* window,
                               SaveFn onSave, ThemePreviewFn onThemePreview,
                               std::function<void()> onClosed)
    : window_(window)
    , onSave_(std::move(onSave))
    , onThemePreview_(std::move(onThemePreview))
    , onClosed_(std::move(onClosed))
    , alive_(std::make_shared<std::atomic<bool>>(true))
{
    build(root);
}

SettingsDialog::~SettingsDialog()
{
    alive_->store(false);
}

void SettingsDialog::build(sg::Widget* root)
{
    modal_ = root->addChild<Modal>();
    modal_->pos(0, 0)->sz(root->width(), root->height());
    modal_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                       sg::AnchorBottom, 0, 0, 0, 0);

    panel_ = modal_->addChild<Panel>();
    panel_->pos(0, 0)->sz(1100, 700);
    panel_->setColors(sg::Color(42, 44, 50), sg::Color(70, 73, 80), 1);
    modal_->setPanel(panel_);
    modal_->setResponsivePanel(true, 32, 20, 820, 520, 1200, 900);

    fontDialog_ = std::make_unique<FontDialog>(
        root,
        [this]
        {
            if (auto* app = sg::Application::instance())
                app->setFocus(fontButton_);
        });

    catalogDialog_ = std::make_unique<ModelCatalogDialog>(
        root,
        [this]
        {
            if (apiUpdateDialog_)
                apiUpdateDialog_->open();
        },
        [this]
        {
            if (auto* app = sg::Application::instance())
                app->setFocus(catalogButton_);
        });

    apiUpdateDialog_ = std::make_unique<ApiUpdateDialog>(
        root, window_,
        [this]
        {
            if (catalogDialog_ && catalogDialog_->isOpen())
                catalogDialog_->refresh();
            if (auto* app = sg::Application::instance())
                app->setFocus(profileList_);
        });

    title_ = addLabel(panel_, 24, 16, 520,
                      std::string(tr().settingsTitle) + " · " + kAppVersion,
                      sg::Color(225, 228, 234));

    auto makeTab = [&](const char* text, int y, int page)
    {
        sg::Button* b = panel_->addChild<sg::Button>();
        b->pos(40, y)->sz(560, 56);
        b->setText(text);
        b->onClick = [this, page](sg::MouseEvent&) { selectPage(page); };
        return b;
    };

    tabApi_ = makeTab(tr().settingsApi, 80, 0);
    tabAppearance_ = makeTab(tr().settingsAppearance, 148, 1);
    if (!paths::isMobile())
        tabCustom_ = makeTab(tr().settingsCustom, 216, 2);
    tabAbout_ = makeTab(tr().settingsAbout, 340, 3);

    auto makePage = [&](sg::ScrollView*& sv, sg::Widget*& content, int height)
    {
        sv = panel_->addChild<sg::ScrollView>();
        sv->pos(24, 64)->sz(1000, 400);
        sv->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                           sg::AnchorBottom,
                       24, 64, 24, 130);
        sv->scrollbarV(true);
        sv->setScrollbarColors(sg::Color(42, 44, 50), sg::Color(90, 94, 102));
        sv->setContentSize(800, height);
        content = sv->addChild<sg::Widget>();
        content->pos(0, 0)->sz(800, height);
    };

    configListPage_ = panel_->addChild<sg::Widget>();
    configListPage_->pos(24, 64)->sz(1000, 400);
    configListPage_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                    sg::AnchorRight | sg::AnchorBottom,
                                24, 64, 24, 130);

    profileList_ = configListPage_->addChild<sg::ListBox>();
    profileList_->pos(0, 0)->sz(1000, 400);
    profileList_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                                 sg::AnchorRight | sg::AnchorBottom,
                             0, 0, 0, 0);
    profileList_->onItemSelected = [this](int index)
    {
        if (index < 0 || index >= (int)profiles_.size())
            return;

        if (index == lastClickedProfile_)
        {
            editProfile(index);
            return;
        }

        lastClickedProfile_ = index;
        activeProfileId_ = profiles_[index].id;
        rebuildProfileList();
    };

    makePage(formScroll_, formContent_, 360);
    makePage(appearanceScroll_, appearanceContent_, 440);
    makePage(customScroll_, customContent_, 600);
    makePage(aboutScroll_, aboutContent_, 460);

    //Configuration form
    int y = 0;
    addLabel(formContent_, 0, y, 200, tr().profileName,
             sg::Color(200, 203, 210));
    profileName_ = addTextBox(formContent_, 0, y + 22, 800);
    y += 62;

    addLabel(formContent_, 0, y, 200, tr().apiUrl, sg::Color(200, 203, 210));
    baseUrl_ = addTextBox(formContent_, 0, y + 22, 800);
    y += 62;

    addLabel(formContent_, 0, y, 200, tr().apiKey, sg::Color(200, 203, 210));
    apiKey_ = addTextBox(formContent_, 0, y + 22, 800);
    apiKey_->setPassword(true);
    y += 62;

    addLabel(formContent_, 0, y, 200, tr().model, sg::Color(200, 203, 210));
    model_ = addTextBox(formContent_, 0, y + 22, 400);

    catalogButton_ = formContent_->addChild<sg::Button>();
    catalogButton_->pos(0, y)->sz(220, 28);
    catalogButton_->setText(tr().selectFromCatalog);
    catalogButton_->onClick = [this](sg::MouseEvent&)
    {
        if (!catalogDialog_)
            return;
        catalogDialog_->open(
            [this](const catalog::Provider& p, const catalog::Model& m)
            {
                std::string currentName = trim(profileName_->text());
                if (currentName.empty() || currentName == autoProfileName_)
                {
                    profileName_->setText(p.name);
                    autoProfileName_ = p.name;
                }
                if (!p.api.empty())
                    baseUrl_->setText(p.api);
                else
                    error_->setText("该供应商没有提供 API 地址，请手动填写。");

                model_->setText(m.id);
                if (m.context > 0)
                {
                    int ctx = std::max(256, std::min(m.context, 1048576));
                    contextTokens_->setText(tokensText(ctx));
                }
                if (m.output > 0)
                {
                    int out = std::max(256, std::min(m.output, 1048576));
                    maxTokens_->setText(tokensText(out));
                }
            });
    };
    y += 60;

    contextTokensLabel_ = addLabel(formContent_, 0, y, 220, tr().contextTokens,
                                   sg::Color(200, 203, 210));
    maxTokensLabel_ = addLabel(formContent_, 340, y, 220, tr().maxTokens,
                               sg::Color(200, 203, 210));
    contextTokens_ = addTextBox(formContent_, 0, y + 22, 260);
    maxTokens_ = addTextBox(formContent_, 340, y + 22, 260);

    //Appearance page
    y = 0;
    addLabel(appearanceContent_, 0, y, 200, tr().theme,
             sg::Color(200, 203, 210));
    theme_ = appearanceContent_->addChild<sg::DropDown>();
    theme_->pos(0, y + 22)->sz(180, 30);
    theme_->addItem(tr().themeDark);
    theme_->addItem(tr().themeLight);
    theme_->onItemSelected = [this](int index)
    {
        if (onThemePreview_)
            onThemePreview_(index == 1 ? "light" : "dark");
    };

    addLabel(appearanceContent_, 220, y, 200, tr().language,
             sg::Color(200, 203, 210));
    language_ = appearanceContent_->addChild<sg::DropDown>();
    language_->pos(220, y + 22)->sz(180, 30);
    language_->addItem(tr().languageZh);
    language_->addItem(tr().languageEn);
    addLabel(appearanceContent_, 420, y + 28, 360, tr().languageHint,
             sg::Color(135, 138, 145));
    y += 60;

    showReasoning_ = appearanceContent_->addChild<sg::CheckBox>();
    showReasoning_->pos(0, y)->sz(320, 24);
    showReasoning_->setText(tr().showReasoning);
    y += 44;

    addLabel(appearanceContent_, 0, y, 240, tr().fontSection,
             sg::Color(200, 203, 210));
    y += 28;

    fontSummary_ = addLabel(appearanceContent_, 0, y, 620, "",
                            sg::Color(150, 153, 160));
    y += 30;

    fontButton_ = appearanceContent_->addChild<sg::Button>();
    fontButton_->pos(0, y)->sz(180, 32);
    fontButton_->setText(tr().fontSettings);
    fontButton_->onClick = [this](sg::MouseEvent&)
    {
        if (!fontDialog_)
            return;
        fontDialog_->open(
            fontBackendValue_, fontPathValue_, fontPointSizeValue_,
            [this](std::string backend, std::string path, int size)
            {
                fontBackendValue_ = std::move(backend);
                fontPathValue_ = std::move(path);
                fontPointSizeValue_ = size;
                updateFontSummary();
            });
    };
    y += 46;

    addLabel(appearanceContent_, 0, y, 620, tr().fontHint,
             sg::Color(135, 138, 145));

    //Custom page
    y = 0;
    addLabel(customContent_, 0, y, 300, tr().sessionsDir,
             sg::Color(200, 203, 210));
    sessions_ = addTextBox(customContent_, 0, y + 22, 650);
    pickSessions_ = customContent_->addChild<sg::Button>();
    pickSessions_->pos(660, y + 22)->sz(100, 30);
    pickSessions_->setText(tr().browse);
    pickSessions_->onClick = [this](sg::MouseEvent&)
    {
        auto* ctx = new PickCtx{ alive_, [this](const std::string& p)
                                 { sessions_->setText(p); } };
        std::string start = sessions_->text();
        SDL_ShowOpenFolderDialog(folderPicked, ctx,
                                 window_ ? window_->handle() : nullptr,
                                 start.c_str(), false);
    };
    openSessions_ = customContent_->addChild<sg::Button>();
    openSessions_->pos(770, y + 22)->sz(100, 30);
    openSessions_->setText(tr().openDir);
    openSessions_->onClick = [this](sg::MouseEvent&)
    {
        std::filesystem::path dir = paths::fromUtf8(trim(sessions_->text()));
        if (dir.empty())
            dir = paths::defaultSessionsDir();
        SDL_OpenURL(fileUrl(dir).c_str());
    };
    y += 62;

    addLabel(customContent_, 0, y, 300, tr().configDir,
             sg::Color(200, 203, 210));
    dataDir_ = addTextBox(customContent_, 0, y + 22, 650);
    pickDataDir_ = customContent_->addChild<sg::Button>();
    pickDataDir_->pos(660, y + 22)->sz(100, 30);
    pickDataDir_->setText(tr().browse);
    pickDataDir_->onClick = [this](sg::MouseEvent&)
    {
        auto* ctx = new PickCtx{ alive_, [this](const std::string& p)
                                 { dataDir_->setText(p); } };
        std::string start = dataDir_->text();
        if (start.empty())
            start = paths::toUtf8(paths::prefDir());
        SDL_ShowOpenFolderDialog(folderPicked, ctx,
                                 window_ ? window_->handle() : nullptr,
                                 start.c_str(), false);
    };
    openDataDir_ = customContent_->addChild<sg::Button>();
    openDataDir_->pos(770, y + 22)->sz(100, 30);
    openDataDir_->setText(tr().openDir);
    openDataDir_->onClick = [this](sg::MouseEvent&)
    {
        std::filesystem::path dir = paths::fromUtf8(trim(dataDir_->text()));
        if (dir.empty())
            dir = paths::prefDir();
        SDL_OpenURL(fileUrl(dir).c_str());
    };
    configDirLabel_ = addLabel(customContent_, 0, y + 58, 650,
                               tr().configDirHint,
                               sg::Color(135, 138, 145));
    y += 86;

    addLabel(customContent_, 0, y, 110, "config.json",
             sg::Color(200, 203, 210));
    configFileLabel_ = addLabel(customContent_, 116, y, 550,
                                paths::toUtf8(paths::configFile()),
                                sg::Color(135, 138, 145));
    openConfig_ = customContent_->addChild<sg::Button>();
    openConfig_->pos(770, y - 4)->sz(100, 26);
    openConfig_->setText(tr().edit);
    openConfig_->onClick = [](sg::MouseEvent&)
    {
        SDL_OpenURL(fileUrl(paths::configFile()).c_str());
    };
    y += 36;

    addLabel(customContent_, 0, y, 200, tr().toolMode,
             sg::Color(200, 203, 210));
    toolMode_ = customContent_->addChild<sg::DropDown>();
    toolMode_->pos(0, y + 22)->sz(230, 30);
    toolMode_->addItem(tr().toolsAuto);
    toolMode_->addItem(tr().toolsAsk);
    toolMode_->addItem(tr().toolsOff);
    y += 62;

    autoTitle_ = customContent_->addChild<sg::CheckBox>();
    autoTitle_->pos(0, y)->sz(360, 24);
    autoTitle_->setText(tr().autoTitle);
    y += 44;

    addLabel(customContent_, 0, y, 380, tr().promptFiles,
             sg::Color(200, 203, 210));
    y += 24;

    sysPath_ = addLabel(customContent_, 0, y, 650,
                        paths::toUtf8(paths::sysPromptFile()),
                        sg::Color(135, 138, 145));
    editSys_ = customContent_->addChild<sg::Button>();
    editSys_->pos(660, y - 4)->sz(134, 26);
    editSys_->setText(tr().edit);
    editSys_->onClick = [](sg::MouseEvent&)
    {
        SDL_OpenURL(fileUrl(paths::sysPromptFile()).c_str());
    };
    y += 32;

    toolPath_ = addLabel(customContent_, 0, y, 650,
                         paths::toUtf8(paths::toolPromptFile()),
                         sg::Color(135, 138, 145));
    editTool_ = customContent_->addChild<sg::Button>();
    editTool_->pos(660, y - 4)->sz(134, 26);
    editTool_->setText(tr().edit);
    editTool_->onClick = [](sg::MouseEvent&)
    {
        SDL_OpenURL(fileUrl(paths::toolPromptFile()).c_str());
    };
    y += 32;

    titlePath_ = addLabel(customContent_, 0, y, 650,
                          paths::toUtf8(paths::titlePromptFile()),
                          sg::Color(135, 138, 145));
    editTitle_ = customContent_->addChild<sg::Button>();
    editTitle_->pos(660, y - 4)->sz(134, 26);
    editTitle_->setText(tr().edit);
    editTitle_->onClick = [](sg::MouseEvent&)
    {
        SDL_OpenURL(fileUrl(paths::titlePromptFile()).c_str());
    };

    //About page
    aboutText_ = aboutContent_->addChild<sg::Label>();
    aboutText_->pos(0, 0)->sz(800, 420);
    aboutText_->setWordWrap(true);

    //Log page
    logView_ = panel_->addChild<sg::RichTextView>();
    logView_->pos(24, 80)->sz(600, 360);
    logView_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                             sg::AnchorRight | sg::AnchorBottom,
                         24, 80, 24, 88);
    logView_->setSelectable(true);
    logView_->setPadding(8);
    logView_->setLineSpacing(2);
    logView_->setVisible(false);

    //Footer
    error_ = addLabel(panel_, 24, 0, 430, "", sg::Color(235, 110, 110));
    error_->setAnchors(sg::AnchorLeft | sg::AnchorRight | sg::AnchorBottom,
                       24, 0, 24, 76);
    error_->setVisible(true);

    newProfile_ = panel_->addChild<sg::Button>();
    newProfile_->pos(24, 0)->sz(100, 44);
    newProfile_->setAnchors(sg::AnchorLeft | sg::AnchorBottom, 24, 0, 0, 24);
    newProfile_->setText(tr().newProfile);
    newProfile_->onClick = [this](sg::MouseEvent&) { editProfile(-1); };

    deleteProfile_ = panel_->addChild<sg::Button>();
    deleteProfile_->pos(134, 0)->sz(100, 44);
    deleteProfile_->setAnchors(sg::AnchorLeft | sg::AnchorBottom, 134, 0, 0, 24);
    deleteProfile_->setText(tr().deleteProfile);
    deleteProfile_->setTextColor(sg::Color(220, 70, 70));
    deleteProfile_->onClick = [this](sg::MouseEvent&)
    { deleteSelectedProfile(); };

    saveProfile_ = panel_->addChild<sg::Button>();
    saveProfile_->pos(0, 0)->sz(200, 44);
    saveProfile_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 234, 24);
    saveProfile_->setText(tr().saveProfile);
    saveProfile_->onClick = [this](sg::MouseEvent&) { saveProfileForm(); };

    sg::Button* back = panel_->addChild<sg::Button>();
    back->pos(0, 0)->sz(200, 44);
    back->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 24, 24);
    back->setText(tr().back);
    back->onClick = [this](sg::MouseEvent&)
    {
        if (page_ == 4)
            selectPage(0);
        else if (page_ >= 0)
            selectPage(-1);
        else
            accept();
    };

    logButton_ = panel_->addChild<sg::Button>();
    logButton_->pos(24, 0)->sz(200, 44);
    logButton_->setAnchors(sg::AnchorLeft | sg::AnchorBottom, 24, 0, 0, 24);
    logButton_->setText(tr().viewLogs);
    logButton_->onClick = [this](sg::MouseEvent&) { selectPage(5); };

    logExport_ = panel_->addChild<sg::Button>();
    logExport_->pos(0, 0)->sz(200, 44);
    logExport_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 234, 24);
    logExport_->setText(tr().exportLogs);
    logExport_->setVisible(false);
    logExport_->onClick = [this](sg::MouseEvent&) { exportLogs(); };

    for (sg::Widget* w : { (sg::Widget*)profileName_, (sg::Widget*)baseUrl_,
                           (sg::Widget*)apiKey_, (sg::Widget*)model_,
                           (sg::Widget*)contextTokens_, (sg::Widget*)maxTokens_,
                           (sg::Widget*)sessions_ })
    {
        addShortcutClose(w, [this] { close(); });
    }

    modal_->onPanelSize = [this](int w, int h) { layoutFields(w, h); };
    selectPage(-1);
    layoutFields(panel_->width(), panel_->height());
    modal_->hideModal();
}

void SettingsDialog::layoutFields(int panelW, int)
{
    int contentW = std::max(480, panelW - 24 - 24 - 8);

    auto setPage = [&](sg::ScrollView* sv, sg::Widget* content, int height)
    {
        content->setSize(contentW, height);
        sv->setContentSize(contentW, height);
    };

    setPage(formScroll_, formContent_, 360);
    setPage(appearanceScroll_, appearanceContent_, 440);
    setPage(customScroll_, customContent_, 600);
    setPage(aboutScroll_, aboutContent_, 460);

    if (profileName_)
        profileName_->setSize(contentW, 30);
    baseUrl_->setSize(contentW, 30);
    apiKey_->setSize(contentW, 30);
    model_->setSize(std::min(400, contentW), 30);

    if (maxTokensLabel_)
        maxTokensLabel_->setPosition(0, maxTokensLabel_->y());
    if (contextTokens_)
    {
        int half = std::max(160, (contentW - 20) / 2);
        int col2 = half + 20;
        contextTokens_->setSize(half, 30);
        if (contextTokensLabel_)
            contextTokensLabel_->setPosition(0, contextTokensLabel_->y());
        if (maxTokensLabel_)
            maxTokensLabel_->setPosition(col2, maxTokensLabel_->y());
        maxTokens_->setPosition(col2, maxTokens_->y());
        maxTokens_->setSize(half, 30);
    }
    if (catalogButton_)
        catalogButton_->setPosition(std::max(0, contentW - 220),
                                    catalogButton_->y());

    if (fontSummary_)
        fontSummary_->setSize(contentW, 18);

    int pathW = std::max(200, contentW - 240);
    sessions_->setSize(pathW, 30);
    if (pickSessions_)
        pickSessions_->setPosition(contentW - 230, pickSessions_->y());
    if (openSessions_)
        openSessions_->setPosition(contentW - 120, openSessions_->y());

    if (configDirLabel_)
        configDirLabel_->setSize(std::max(200, contentW - 120), 18);
    if (dataDir_)
        dataDir_->setSize(pathW, 30);
    if (pickDataDir_)
        pickDataDir_->setPosition(contentW - 230, pickDataDir_->y());
    if (openDataDir_)
        openDataDir_->setPosition(contentW - 120, openDataDir_->y());

    if (configFileLabel_)
        configFileLabel_->setSize(std::max(160, contentW - 236), 18);
    if (openConfig_)
        openConfig_->setPosition(contentW - 120, openConfig_->y());

    if (sysPath_)
        sysPath_->setSize(std::max(200, contentW - 160), 18);
    if (editSys_)
        editSys_->setPosition(contentW - 140, editSys_->y());
    if (toolPath_)
        toolPath_->setSize(std::max(200, contentW - 160), 18);
    if (editTool_)
        editTool_->setPosition(contentW - 140, editTool_->y());
    if (titlePath_)
        titlePath_->setSize(std::max(200, contentW - 160), 18);
    if (editTitle_)
        editTitle_->setPosition(contentW - 140, editTitle_->y());

    if (aboutText_)
        aboutText_->setSize(contentW, 420);

    int mx = 40;
    int mw = std::max(200, panelW - 80);
    auto placeMenu = [&](sg::Button* b, int y)
    {
        if (!b)
            return;
        b->setPosition(mx, y);
        b->setSize(mw, 56);
    };
    placeMenu(tabApi_, 80);
    placeMenu(tabAppearance_, 148);
    placeMenu(tabCustom_, 216);
    placeMenu(tabAbout_, paths::isMobile() ? 216 : 340);
}

void SettingsDialog::selectPage(int index)
{
    page_ = index < 0 ? -1 : std::min(index, 5);

    bool menu = (page_ < 0);
    if (tabApi_) tabApi_->setVisible(menu);
    if (tabAppearance_) tabAppearance_->setVisible(menu);
    if (tabCustom_) tabCustom_->setVisible(menu);
    if (tabAbout_) tabAbout_->setVisible(menu);
    if (logButton_) logButton_->setVisible(menu);

    if (configListPage_) configListPage_->setVisible(page_ == 0);
    if (formScroll_) formScroll_->setVisible(page_ == 4);
    if (appearanceScroll_) appearanceScroll_->setVisible(page_ == 1);
    if (customScroll_) customScroll_->setVisible(page_ == 2);
    if (aboutScroll_) aboutScroll_->setVisible(page_ == 3);
    if (logView_) logView_->setVisible(page_ == 5);
    if (logExport_) logExport_->setVisible(page_ == 5);

    if (newProfile_) newProfile_->setVisible(page_ == 0);
    if (deleteProfile_) deleteProfile_->setVisible(page_ == 0);
    if (saveProfile_) saveProfile_->setVisible(page_ == 4);

    if (page_ == 5)
        refreshLogs();

    if (title_)
    {
        std::string title = std::string(tr().settingsTitle) + " · " + kAppVersion;
        if (page_ == 0)
            title = std::string(tr().settingsTitle) + " · " + tr().settingsApi;
        else if (page_ == 1)
            title = std::string(tr().settingsTitle) + " · " + tr().settingsAppearance;
        else if (page_ == 2)
            title = std::string(tr().settingsTitle) + " · " + tr().settingsCustom;
        else if (page_ == 3)
            title = std::string(tr().settingsTitle) + " · " + tr().settingsAbout;
        else if (page_ == 4)
            title = std::string(tr().settingsTitle) + " · " + tr().settingsApi +
                    " · " + (editingProfile_ < 0 ? tr().newProfile : tr().edit);
        else if (page_ == 5)
            title = std::string(tr().settingsTitle) + " · " + tr().logs;
        title_->setText(title);
    }

    if (panel_ && panel_->getWindow())
        panel_->getWindow()->requestRedraw();

    if (auto* app = sg::Application::instance())
    {
        if (page_ == 0)
            app->setFocus(profileList_);
        else if (page_ == 4)
            app->setFocus(profileName_);
        else if (page_ == 1)
            app->setFocus(fontButton_);
        else if (page_ == 2)
            app->setFocus(sessions_);
        else if (page_ == 3)
            app->setFocus(nullptr);
        else if (page_ == 5)
            app->setFocus(logView_);
        else
            app->setFocus(tabApi_);
    }
}

void SettingsDialog::refreshLogsIfVisible()
{
    if (page_ == 5)
        refreshLogs();
}

void SettingsDialog::poll()
{
    refreshLogsIfVisible();
    if (apiUpdateDialog_)
        apiUpdateDialog_->poll();
}

void SettingsDialog::refreshLogs()
{
    if (!logView_)
        return;

    uint64_t revision = log::revision();
    if (revision == logRevision_ && !logView_->blocks().empty())
        return;
    logRevision_ = revision;

    std::string text = log::dump();
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos)
        {
            if (start < text.size())
                lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }

    std::vector<sg::RichTextBlock> blocks;
    const std::size_t maxLines = 500;
    for (auto it = lines.rbegin();
         it != lines.rend() && blocks.size() < maxLines; ++it)
    {
        sg::RichTextBlock b;
        b.type = sg::RichBlockType::Paragraph;
        b.marginTop = 0;
        b.marginBottom = 1;
        b.wrap = true;

        sg::RichTextRun run;
        run.text = it->empty() ? " " : *it;
        b.runs.push_back(std::move(run));
        blocks.push_back(std::move(b));
    }

    if (blocks.empty())
    {
        sg::RichTextBlock b;
        b.type = sg::RichBlockType::Paragraph;
        b.marginTop = 0;
        b.marginBottom = 0;

        sg::RichTextRun run;
        run.text = tr().logEmpty;
        b.runs.push_back(std::move(run));
        blocks.push_back(std::move(b));
    }

    //Newest first, so opening the page always shows the latest entries.
    logView_->setBlocks(std::move(blocks));
}

void SettingsDialog::exportLogs()
{
    std::filesystem::path suggested =
        paths::prefDir() / paths::fromUtf8("sdlagent-log-" + logTimestamp() + ".txt");

    auto* ctx = new SaveLogCtx;
    ctx->filter.name = tr().logs;
    ctx->filter.pattern = "txt";
    ctx->done = [this](const std::string& chosen)
    {
        std::filesystem::path path = paths::fromUtf8(chosen);
        if (path.extension().empty())
            path += ".txt";

        std::string err;
        if (!log::exportTo(path, err) && window_)
        {
            window_->showMessageBox(
                tr().logs,
                (std::string(tr().logExportFailed) + err).c_str(),
                sg::MessageBoxType::Warning, sg::MessageBoxButtons::OK,
                nullptr);
        }
    };

    SDL_ShowSaveFileDialog(logSavePicked, ctx,
                           window_ ? window_->handle() : nullptr,
                           &ctx->filter, 1,
                           paths::toUtf8(suggested).c_str());
}

void SettingsDialog::rebuildProfileList()
{
    if (!profileList_)
        return;

    profileList_->clear();

    int activeIndex = -1;
    for (std::size_t i = 0; i < profiles_.size(); ++i)
    {
        const ApiProfile& p = profiles_[i];
        std::string name = !p.name.empty()
                               ? p.name
                               : (!p.model.empty() ? p.model : p.baseUrl);
        if (name.empty())
            name = "未命名";

        std::string item = (p.id == activeProfileId_) ? "● " : "   ";
        item += name;
        if (!p.model.empty() && p.model != name)
            item += "   " + p.model;
        profileList_->addItem(item);

        if (p.id == activeProfileId_)
            activeIndex = (int)i;
    }

    if (profiles_.empty())
        profileList_->setSelectedIndex(-1);
    else
        profileList_->setSelectedIndex(activeIndex >= 0 ? activeIndex : 0);
}

void SettingsDialog::editProfile(int index)
{
    editingProfile_ = index;

    if (index >= 0 && index < (int)profiles_.size())
    {
        const ApiProfile& p = profiles_[index];
        profileName_->setText(p.name);
        autoProfileName_ = p.name;
        baseUrl_->setText(p.baseUrl);
        apiKey_->setText(p.apiKey);
        model_->setText(p.model);
        contextTokens_->setText(tokensText(p.contextTokens));
        maxTokens_->setText(tokensText(p.maxTokens));
    }
    else
    {
        profileName_->setText("");
        autoProfileName_.clear();
        baseUrl_->setText("");
        apiKey_->setText("");
        model_->setText("");
        contextTokens_->setText("1m");
        maxTokens_->setText("512k");
    }

    error_->setText("");
    selectPage(4);
}

void SettingsDialog::saveProfileForm()
{
    ApiProfile p;
    p.id = (editingProfile_ >= 0 && editingProfile_ < (int)profiles_.size())
               ? profiles_[editingProfile_].id
               : makeId();
    p.name = trim(profileName_->text());
    p.baseUrl = trim(baseUrl_->text());
    while (!p.baseUrl.empty() && p.baseUrl.back() == '/')
        p.baseUrl.pop_back();
    if (!p.baseUrl.empty() &&
        p.baseUrl.find("://") == std::string::npos)
        p.baseUrl = "https://" + p.baseUrl;
    p.apiKey = trim(apiKey_->text());
    p.model = trim(model_->text());
    p.temperature = 0.0;

    long long context = 0;
    if (!parseCount(contextTokens_->text(), context))
    {
        error_->setText(tr().invalidNumber);
        return;
    }
    if (context < 256) context = 256;
    if (context > 1048576) context = 1048576;
    p.contextTokens = (int)context;

    long long tokens = 0;
    if (!parseCount(maxTokens_->text(), tokens))
    {
        error_->setText(tr().invalidNumber);
        return;
    }
    if (tokens < 256) tokens = 256;
    if (tokens > 1048576) tokens = 1048576;
    p.maxTokens = (int)tokens;

    if (p.name.empty())
        p.name = !p.model.empty() ? p.model
                                  : (!p.baseUrl.empty() ? p.baseUrl : "未命名");

    contextTokens_->setText(tokensText(p.contextTokens));
    maxTokens_->setText(tokensText(p.maxTokens));
    profileName_->setText(p.name);
    autoProfileName_ = p.name;

    if (editingProfile_ >= 0 && editingProfile_ < (int)profiles_.size())
        profiles_[editingProfile_] = p;
    else
        profiles_.push_back(p);

    activeProfileId_ = p.id;
    editingProfile_ = -1;
    rebuildProfileList();
    selectPage(0);
}

void SettingsDialog::deleteSelectedProfile()
{
    if (!profileList_)
        return;

    int index = profileList_->selectedIndex();
    if (index < 0 || index >= (int)profiles_.size())
        return;

    std::string id = profiles_[index].id;
    profiles_.erase(profiles_.begin() + index);

    if (activeProfileId_ == id)
    {
        activeProfileId_ = profiles_.empty() ? std::string()
                                             : profiles_.front().id;
    }

    rebuildProfileList();
}

void SettingsDialog::updateFontSummary()
{
    if (!fontSummary_)
        return;

    std::string text;
    std::string path = fontPathValue_;
    if (path.empty())
        path = font::detectSystemFontPath();
    std::string file = path.empty()
                           ? std::string("系统字体")
                           : paths::toUtf8(paths::fromUtf8(path).filename());
    text = file + " · " + std::to_string(fontPointSizeValue_) + "px";

    std::string emoji = font::detectEmojiFontPath();
    if (!emoji.empty())
        text += " · emoji " +
                paths::toUtf8(paths::fromUtf8(emoji).filename());

    fontSummary_->setText(ellipsize(text, 620));
}

void SettingsDialog::open(const Config& cfg)
{
    original_ = cfg;

    profiles_ = cfg.profiles;
    activeProfileId_ = cfg.activeProfileId;
    if (activeProfileId_.empty() && !profiles_.empty())
        activeProfileId_ = profiles_.front().id;
    editingProfile_ = -1;
    lastClickedProfile_ = -1;
    rebuildProfileList();

    sessions_->setText(cfg.sessionsDir);
    dataDir_->setText(cfg.dataDir.empty() ? paths::toUtf8(paths::prefDir())
                                          : cfg.dataDir);
    if (sysPath_)
        sysPath_->setText(paths::toUtf8(paths::sysPromptFile()));
    if (toolPath_)
        toolPath_->setText(paths::toUtf8(paths::toolPromptFile()));
    if (titlePath_)
        titlePath_->setText(paths::toUtf8(paths::titlePromptFile()));

    toolMode_->setSelectedIndex(cfg.toolMode == ToolMode::Auto
                                    ? 0
                                    : (cfg.toolMode == ToolMode::Ask ? 1 : 2));
    if (autoTitle_)
        autoTitle_->setChecked(cfg.autoTitle);
    theme_->setSelectedIndex(cfg.theme == "light" ? 1 : 0);
    if (language_)
        language_->setSelectedIndex(cfg.lang == Lang::En ? 1 : 0);
    showReasoning_->setChecked(cfg.showReasoning);

    fontBackendValue_ = cfg.fontBackend;
    fontPathValue_ = cfg.fontPath;
    fontPointSizeValue_ = cfg.fontPointSize;
    updateFontSummary();

    std::string about;
    about += std::string("SDL Agent ") + kAppVersion + "\n\n";
    about += "本地 AI Agent，基于 SDL3、SDLGui 和 libllmapi。\n\n";
    about += "配置文件: " + paths::toUtf8(paths::configFile()) + "\n";
    about += "会话目录: " + cfg.sessionsDir + "\n";
    about += "系统提示词: " + paths::toUtf8(paths::sysPromptFile()) + "\n";
    about += "工具提示词: " + paths::toUtf8(paths::toolPromptFile()) + "\n";
    about += "标题提示词: " + paths::toUtf8(paths::titlePromptFile()) + "\n";
    about += "附件类型: " + paths::toUtf8(paths::attachmentsConfigFile()) +
             "\n";
    about += "网关请求头: " + paths::toUtf8(paths::gatewaysFile()) + "\n\n";
    about += "依赖: SDL3, SDLGui, libllmapi, md4c, cJSON, libcurl, SDL3_ttf\n";
    about += "许可证: GPL-3.0-only（本程序）；第三方库各自遵循其许可证";
    aboutText_->setText(about);

    selectPage(-1);
    error_->setText("");
    modal_->showModal();
    layoutFields(panel_->width(), panel_->height());
    if (window_)
        window_->requestRedraw();
}

void SettingsDialog::close()
{
    if (!modal_->isOpen())
        return;
    if (apiUpdateDialog_ && apiUpdateDialog_->isOpen())
        apiUpdateDialog_->close();
    modal_->hideModal();
    if (onClosed_)
        onClosed_();
}

bool SettingsDialog::isOpen() const
{
    return modal_ && modal_->isOpen();
}

sg::Widget* SettingsDialog::firstField() const
{
    return page_ >= 0 ? (sg::Widget*)baseUrl_ : (sg::Widget*)tabApi_;
}

void SettingsDialog::accept()
{
    Config cfg = original_;

    cfg.profiles = profiles_;
    cfg.activeProfileId = activeProfileId_;

    if (!cfg.profiles.empty())
    {
        ApiProfile* active = nullptr;
        for (auto& p : cfg.profiles)
        {
            if (p.id == cfg.activeProfileId)
            {
                active = &p;
                break;
            }
        }
        if (!active)
        {
            active = &cfg.profiles.front();
            cfg.activeProfileId = active->id;
        }

        cfg.baseUrl = active->baseUrl;
        cfg.apiKey = active->apiKey;
        cfg.model = active->model;
        cfg.temperature = active->temperature;
        cfg.contextTokens = active->contextTokens;
        cfg.maxTokens = active->maxTokens;
    }
    else
    {
        cfg.baseUrl.clear();
        cfg.apiKey.clear();
        cfg.model.clear();
        cfg.temperature = 0.0;
        cfg.contextTokens = 1048576;
        cfg.maxTokens = 524288;
    }

    cfg.sessionsDir = paths::toUtf8(
        paths::resolveDir(sessions_->text(), paths::defaultSessionsDir()));

    cfg.dataDir = paths::toUtf8(
        paths::resolveDir(dataDir_->text(), paths::prefDir()));

    if (paths::isMobile())
    {
        //Paths are not user-editable on Android/iOS.
        cfg.sessionsDir = paths::toUtf8(paths::defaultSessionsDir());
        cfg.dataDir = paths::toUtf8(paths::prefDir());
    }

    int mode = toolMode_->selectedIndex();
    cfg.toolMode = mode == 0 ? ToolMode::Auto
                             : (mode == 1 ? ToolMode::Ask : ToolMode::Off);
    cfg.autoTitle = autoTitle_ ? autoTitle_->isChecked() : true;
    cfg.theme = (theme_->selectedIndex() == 1) ? "light" : "dark";
    if (language_)
        cfg.lang = language_->selectedIndex() == 1 ? Lang::En : Lang::Zh;
    cfg.showReasoning = showReasoning_->isChecked();

    cfg.fontBackend = "ttf";
    cfg.fontPath = fontPathValue_;
    cfg.fontPointSize = std::max(10, std::min(fontPointSizeValue_, 24));

    if (cfg.fontPath.empty())
        cfg.fontPath = font::detectSystemFontPath();
    if (cfg.fontPath.empty())
    {
        error_->setText("没有找到可用的系统字体，请手动选择字体文件。");
        return;
    }

    std::string ferr;
    if (!font::probe(cfg.fontPath, cfg.fontPointSize, ferr))
    {
        error_->setText(ferr);
        return;
    }
    fontPathValue_ = cfg.fontPath;
    updateFontSummary();

    cfg.onboarded = true;

    std::string err;
    if (onSave_ && !onSave_(cfg, err))
    {
        error_->setText(err.empty() ? std::string("保存失败") : err);
        return;
    }

    modal_->hideModal();
    if (onClosed_)
        onClosed_();
}

void SettingsDialog::applyTheme(const sg::Theme& theme)
{
    if (modal_)
        modal_->setOverlayColor(modalBackdrop(theme));
    if (panel_)
        panel_->setColors(theme.dialogBg, theme.border, 1);
    if (title_)
        title_->setTextColor(theme.dialogTitleText);
    if (error_)
        error_->setTextColor(sg::Color(235, 110, 110));
    if (sysPath_)
        sysPath_->setTextColor(theme.mutedText);
    if (toolPath_)
        toolPath_->setTextColor(theme.mutedText);
    if (titlePath_)
        titlePath_->setTextColor(theme.mutedText);
    if (configDirLabel_)
        configDirLabel_->setTextColor(theme.mutedText);
    if (deleteProfile_)
        deleteProfile_->setTextColor(sg::Color(220, 70, 70));
    if (fontSummary_)
        fontSummary_->setTextColor(theme.mutedText);
    if (logView_)
    {
        logView_->setDefaultTextColor(theme.text);
        logView_->setCodeBackground(theme.panelBg);
        logView_->setCodeBorder(theme.panelBorder);
        logView_->setSeparatorColor(theme.border);
        logView_->setSelectionColor(theme.rowSelected);
    }
    if (fontDialog_)
        fontDialog_->applyTheme(theme);
    if (catalogDialog_)
        catalogDialog_->applyTheme(theme);
    if (apiUpdateDialog_)
        apiUpdateDialog_->applyTheme(theme);
}

//==============================================================================
// InputDialog
//==============================================================================

InputDialog::InputDialog(sg::Widget* root, std::function<void()> onClosed)
    : onClosed_(std::move(onClosed))
{
    modal_ = root->addChild<Modal>();
    modal_->pos(0, 0)->sz(root->width(), root->height());
    modal_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                       sg::AnchorBottom, 0, 0, 0, 0);

    panel_ = modal_->addChild<Panel>();
    panel_->pos(0, 0)->sz(500, 210);
    panel_->setColors(sg::Color(42, 44, 50), sg::Color(70, 73, 80), 1);
    modal_->setPanel(panel_);

    title_ = addLabel(panel_, 22, 18, 450, "", sg::Color(225, 228, 234));

    input_ = addTextBox(panel_, 22, 60, 456, 34);
    input_->onKeyDown = [this](sg::KeyEvent& e)
    {
        if (e.key == SDLK_RETURN || e.key == SDLK_KP_ENTER)
            accept();
        else if (e.key == SDLK_ESCAPE)
            close();
    };

    sg::Button* cancel = panel_->addChild<sg::Button>();
    cancel->pos(280, 146)->sz(96, 32);
    cancel->setText(tr().cancel);
    cancel->onClick = [this](sg::MouseEvent&) { close(); };

    sg::Button* ok = panel_->addChild<sg::Button>();
    ok->pos(386, 146)->sz(96, 32);
    ok->setText(tr().ok);
    ok->onClick = [this](sg::MouseEvent&) { accept(); };

    modal_->hideModal();
}

void InputDialog::open(const std::string& title, const std::string& initial,
                       std::function<void(const std::string&)> onAccept)
{
    onAccept_ = std::move(onAccept);
    title_->setText(title);
    input_->setText(initial);
    input_->selectAll();
    modal_->showModal();
}

void InputDialog::close()
{
    if (!modal_->isOpen())
        return;
    modal_->hideModal();
    if (onClosed_)
        onClosed_();
}

bool InputDialog::isOpen() const
{
    return modal_ && modal_->isOpen();
}

sg::Widget* InputDialog::firstField() const
{
    return input_;
}

void InputDialog::accept()
{
    std::string value = trim(input_->text());
    if (value.empty())
        return;

    auto cb = std::move(onAccept_);
    onAccept_ = nullptr;
    modal_->hideModal();
    if (cb)
        cb(value);
    if (onClosed_)
        onClosed_();
}

void InputDialog::applyTheme(const sg::Theme& theme)
{
    if (modal_)
        modal_->setOverlayColor(modalBackdrop(theme));
    if (panel_)
        panel_->setColors(theme.dialogBg, theme.border, 1);
    if (title_)
        title_->setTextColor(theme.dialogTitleText);
}

//==============================================================================
// ConfirmDialog
//==============================================================================

ConfirmDialog::ConfirmDialog(sg::Widget* root, std::function<void()> onClosed)
    : onClosed_(std::move(onClosed))
{
    modal_ = root->addChild<Modal>();
    modal_->pos(0, 0)->sz(root->width(), root->height());
    modal_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                       sg::AnchorBottom, 0, 0, 0, 0);

    panel_ = modal_->addChild<Panel>();
    panel_->pos(0, 0)->sz(460, 340);
    panel_->setColors(sg::Color(42, 44, 50), sg::Color(70, 73, 80), 1);
    modal_->setPanel(panel_);
    modal_->setResponsivePanel(true, 100, 80, 380, 280, 460, 340);
    modal_->onPanelSize = [this](int w, int h) { layoutPanel(w, h); };

    title_ = addLabel(panel_, 24, 18, 640, tr().confirmTitle,
                      sg::Color(225, 228, 234));

    summary_ = addLabel(panel_, 24, 50, 660, "", sg::Color(118, 170, 255));

    detail_ = panel_->addChild<sg::TextArea>();
    detail_->pos(24, 82)->sz(672, 330);
    detail_->setReadOnly(true);
    detail_->setWordWrap(true);
    detail_->onKeyDown = [this](sg::KeyEvent& e)
    {
        if (e.key == SDLK_ESCAPE)
            close();
    };

    auto addButton = [this](int x, int y, const char* text)
    {
        sg::Button* b = panel_->addChild<sg::Button>();
        b->pos(x, y)->sz(412, 32);
        b->setText(text);
        return b;
    };

    stop_ = addButton(24, 260, tr().stopRun);
    stop_->setTextColor(sg::Color(230, 100, 100));
    stop_->onClick = [this](sg::MouseEvent&)
    {
        modal_->hideModal();
        auto cb = std::move(onDecision_);
        onDecision_ = nullptr;
        if (cb) cb(ConfirmDecision::Stop);
        if (onClosed_) onClosed_();
    };

    deny_ = addButton(24, 218, tr().deny);
    deny_->onClick = [this](sg::MouseEvent&)
    {
        modal_->hideModal();
        auto cb = std::move(onDecision_);
        onDecision_ = nullptr;
        if (cb) cb(ConfirmDecision::Deny);
        if (onClosed_) onClosed_();
    };

    allow_ = addButton(24, 176, tr().allow);
    allow_->setTextColor(sg::Color(120, 200, 130));
    allow_->onClick = [this](sg::MouseEvent&)
    {
        modal_->hideModal();
        auto cb = std::move(onDecision_);
        onDecision_ = nullptr;
        if (cb) cb(ConfirmDecision::Allow);
        if (onClosed_) onClosed_();
    };

    layoutPanel(460, 340);
    modal_->hideModal();
}

void ConfirmDialog::layoutPanel(int w, int h)
{
    const int pad = 24;
    const int btnH = 32;
    const int gap = 8;
    const int bottom = 16;
    const int stack = btnH * 3 + gap * 2;
    const int stackY = h - bottom - stack;

    if (title_)
        title_->setSize(std::max(120, w - pad * 2), 18);
    if (summary_)
        summary_->setSize(std::max(120, w - pad * 2), 18);
    if (detail_)
    {
        detail_->setPosition(pad, 78);
        detail_->setSize(std::max(120, w - pad * 2),
                         std::max(40, stackY - 78 - 10));
    }

    int btnW = std::max(120, w - pad * 2);
    auto place = [&](sg::Button* b, int y)
    {
        if (!b)
            return;
        b->setPosition(pad, y);
        b->setSize(btnW, btnH);
    };

    place(allow_, stackY);
    place(deny_, stackY + btnH + gap);
    place(stop_, stackY + (btnH + gap) * 2);
}

void ConfirmDialog::open(const ConfirmationRequestView& request,
                         std::function<void(ConfirmDecision)> onDecision)
{
    onDecision_ = std::move(onDecision);
    summary_->setText(request.summary);
    detail_->setText(request.detail);
    modal_->showModal();
}

void ConfirmDialog::close()
{
    if (!modal_->isOpen())
        return;
    modal_->hideModal();
    auto cb = std::move(onDecision_);
    onDecision_ = nullptr;
    if (cb) cb(ConfirmDecision::Deny);
    if (onClosed_) onClosed_();
}

bool ConfirmDialog::isOpen() const
{
    return modal_ && modal_->isOpen();
}

void ConfirmDialog::applyTheme(const sg::Theme& theme)
{
    if (modal_)
        modal_->setOverlayColor(theme.overlay);
    if (panel_)
        panel_->setColors(theme.dialogBg, theme.border, 1);
    if (title_)
        title_->setTextColor(theme.dialogTitleText);
    if (summary_)
        summary_->setTextColor(theme.menuAccent);
}

//==============================================================================
// FontDialog
//==============================================================================

FontDialog::FontDialog(sg::Widget* root, std::function<void()> onClosed)
    : onClosed_(std::move(onClosed))
{
    modal_ = root->addChild<Modal>();
    modal_->pos(0, 0)->sz(root->width(), root->height());
    modal_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                       sg::AnchorBottom, 0, 0, 0, 0);

    panel_ = modal_->addChild<Panel>();
    panel_->pos(0, 0)->sz(860, 560);
    panel_->setColors(sg::Color(38, 40, 46), sg::Color(70, 73, 80), 1);
    modal_->setPanel(panel_);

    title_ = addLabel(panel_, 16, 8, 300, tr().fontDialogTitle,
                      sg::Color(226, 228, 233));
    listLabel_ = addLabel(panel_, 16, 48, 340, tr().fontSection,
                          sg::Color(150, 153, 160));

    list_ = panel_->addChild<sg::ListBox>();
    list_->pos(16, 70)->sz(340, 438);
    list_->onItemSelected = [this](int index)
    {
        selected_ = index;
        updatePreview();
    };

    previewLabel_ = addLabel(panel_, 372, 48, 472, tr().fontPreview,
                             sg::Color(150, 153, 160));

    preview_ = panel_->addChild<sg::Canvas>();
    preview_->pos(372, 70)->sz(472, 260);
    preview_->setContinuousRedraw(false);
    preview_->onDraw = [this](SDL_Renderer* renderer, const sg::Rect& area)
    {
        fillRect(renderer, area, previewBg_);
        SDL_SetRenderDrawColor(renderer, borderColor_.r, borderColor_.g,
                               borderColor_.b, borderColor_.a);
        SDL_FRect fr = { (float)area.x, (float)area.y, (float)area.w,
                         (float)area.h };
        SDL_RenderRect(renderer, &fr);

        if (!previewFont_)
        {
            std::string message = previewError_.empty()
                                      ? std::string("没有找到可用的字体")
                                      : previewError_;
            if (sg::currentFont())
                sg::currentFont()->draw(renderer, message.c_str(), area.x + 16,
                                        area.y + 16, errorColor_);
            return;
        }

        int lineH = previewFont_->lineHeight();
        int y = area.y + 14;
        const char* lines[] = { "The quick brown fox jumps over the lazy dog.",
                                "欢迎使用 SDL Agent",
                                "😀 🎉 ❤️ 👍 🚀 👨‍👩‍👧",
                                "0123456789 !@#$%^&*()" };
        for (int i = 0; i < 4; ++i)
        {
            if (y + lineH > area.y + area.h - 28)
                break;
            previewFont_->draw(renderer, lines[i], area.x + 16, y,
                               i == 0 ? accentColor_ : previewText_);
            y += lineH + 8;
        }

        std::string sizeText =
            std::string(tr().fontSize) + " " + std::to_string(size_) + " px";
        if (sg::currentFont())
            sg::currentFont()->draw(renderer, sizeText.c_str(), area.x + 16,
                                    area.y + area.h - 24, mutedColor_);
    };

    sizeLabel_ = addLabel(panel_, 372, 338, 52, tr().fontSize,
                          sg::Color(190, 193, 202));

    sizeCombo_ = panel_->addChild<sg::ComboBox>();
    sizeCombo_->pos(430, 336)->sz(80, 26);
    for (int i = 10; i <= 24; ++i)
        sizeCombo_->addItem(std::to_string(i));
    sizeCombo_->setSelectedIndex(6);
    sizeCombo_->onItemSelected = [this](int index)
    {
        size_ = 10 + index;
        updatePreview();
    };

    info_ = panel_->addChild<sg::Label>();
    info_->pos(372, 370)->sz(472, 140);
    info_->setWordWrap(true);
    info_->setTextColor(sg::Color(150, 155, 168));

    cancel_ = panel_->addChild<sg::Button>();
    cancel_->pos(652, 518)->sz(92, 30);
    cancel_->setText(tr().cancel);
    cancel_->onClick = [this](sg::MouseEvent&) { close(); };

    apply_ = panel_->addChild<sg::Button>();
    apply_->pos(752, 518)->sz(92, 30);
    apply_->setText(tr().ok);
    apply_->onClick = [this](sg::MouseEvent&) { accept(); };

    modal_->hideModal();
}

void FontDialog::open(
    const std::string& backend, const std::string& path, int size,
    std::function<void(std::string, std::string, int)> onApply)
{
    onApply_ = std::move(onApply);
    entries_ = font::enumerate();

    //Keep a previously selected custom font even if it is outside the
    //standard font directories.
    if (backend == "ttf" && !path.empty())
    {
        bool found = false;
        for (const auto& entry : entries_)
        {
            if (entry.path == path)
            {
                found = true;
                break;
            }
        }
        if (!found)
            entries_.push_back({ paths::toUtf8(paths::fromUtf8(path).filename()),
                                 path, false });
    }

    list_->clear();
    for (const auto& entry : entries_)
        list_->addItem(entry.display);

    int selected = 0;
    std::string desired = path;
    if (desired.empty())
        desired = font::detectSystemFontPath();

    bool desiredFound = false;
    for (const auto& entry : entries_)
    {
        if (!desired.empty() && entry.path == desired)
        {
            desiredFound = true;
            break;
        }
    }
    if (!desiredFound && !desired.empty())
    {
        entries_.push_back(
            { paths::toUtf8(paths::fromUtf8(desired).filename()), desired,
              false });
        list_->clear();
        for (const auto& entry : entries_)
            list_->addItem(entry.display);
    }

    for (int i = 0; i < (int)entries_.size(); ++i)
    {
        if (!desired.empty() && entries_[i].path == desired)
        {
            selected = i;
            break;
        }
    }

    selected_ = selected;
    list_->setSelectedIndex(selected);
    size_ = std::max(10, std::min(size, 24));
    sizeCombo_->setSelectedIndex(size_ - 10);
    backend_ = backend;
    path_ = path;

    updatePreview();
    modal_->showModal();
}

void FontDialog::close()
{
    if (!modal_->isOpen())
        return;
    modal_->hideModal();
    previewFont_.reset();
    previewError_.clear();
    if (onClosed_)
        onClosed_();
}

bool FontDialog::isOpen() const
{
    return modal_ && modal_->isOpen();
}

void FontDialog::updatePreview()
{
    previewFont_.reset();
    previewError_.clear();

    if (entries_.empty())
        return;

    selected_ = std::max(0, std::min(selected_, (int)entries_.size() - 1));
    const font::FontEntry& entry = entries_[selected_];

    previewFont_ = font::load(entry.path, size_, previewError_);
    backend_ = "ttf";
    path_ = entry.path;

    std::string text = entry.display + "\n" + entry.path;

    if (info_)
        info_->setText(text);
    if (preview_)
        preview_->requestRedraw();
}

void FontDialog::accept()
{
    if (entries_.empty())
        return;

    selected_ = std::max(0, std::min(selected_, (int)entries_.size() - 1));
    const font::FontEntry& entry = entries_[selected_];

    auto cb = std::move(onApply_);
    onApply_ = nullptr;
    close();

    if (cb)
        cb(std::string("ttf"), entry.path, size_);
}

void FontDialog::applyTheme(const sg::Theme& theme)
{
    if (modal_)
        modal_->setOverlayColor(modalBackdrop(theme));
    if (panel_)
        panel_->setColors(theme.dialogBg, theme.border, 1);
    if (title_)
        title_->setTextColor(theme.dialogTitleText);
    if (listLabel_)
        listLabel_->setTextColor(theme.mutedText);
    if (previewLabel_)
        previewLabel_->setTextColor(theme.mutedText);
    if (sizeLabel_)
        sizeLabel_->setTextColor(theme.mutedText);
    if (info_)
        info_->setTextColor(theme.mutedText);

    previewBg_ = theme.textAreaBg;
    previewText_ = theme.textAreaText;
    accentColor_ = theme.menuAccent;
    borderColor_ = theme.panelBorder;
    mutedColor_ = theme.mutedText;
    errorColor_ = sg::Color(235, 110, 110);

    if (preview_)
        preview_->requestRedraw();
}

//==============================================================================
// ModelCatalogDialog
//==============================================================================

namespace
{

std::string lowerAscii(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string groupNumber(long long v)
{
    std::string s = std::to_string(v);
    std::string out;
    out.reserve(s.size() + s.size() / 3);
    int count = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i)
    {
        out.push_back(s[i]);
        if (++count % 3 == 0 && i > 0)
            out.push_back(',');
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string priceText(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

} //namespace

ModelCatalogDialog::ModelCatalogDialog(sg::Widget* root,
                                       std::function<void()> onUpdateApi,
                                       std::function<void()> onClosed)
    : onUpdateApi_(std::move(onUpdateApi))
    , onClosed_(std::move(onClosed))
{
    modal_ = root->addChild<Modal>();
    modal_->pos(0, 0)->sz(root->width(), root->height());
    modal_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                       sg::AnchorBottom, 0, 0, 0, 0);

    panel_ = modal_->addChild<Panel>();
    panel_->pos(0, 0)->sz(900, 640);
    panel_->setColors(sg::Color(38, 40, 46), sg::Color(70, 73, 80), 1);
    modal_->setPanel(panel_);

    title_ = addLabel(panel_, 16, 8, 600, tr().catalogTitle,
                      sg::Color(226, 228, 233));

    source_ = addLabel(panel_, 16, 42, 660, "", sg::Color(150, 153, 160));

    reload_ = panel_->addChild<sg::Button>();
    reload_->pos(760, 36)->sz(120, 28);
    reload_->setText(tr().catalogReload);
    reload_->onClick = [this](sg::MouseEvent&) { reload(true); };

    providerLabel_ = addLabel(panel_, 16, 76, 260, tr().catalogProvider,
                              sg::Color(150, 153, 160));
    providerSearch_ = panel_->addChild<sg::TextBox>();
    providerSearch_->pos(16, 96)->sz(260, 28);
    providerSearch_->setPlaceholder(tr().catalogSearchProvider);
    providerSearch_->onKeyDown = [this](sg::KeyEvent& e)
    {
        if (e.key == SDLK_RETURN || e.key == SDLK_KP_ENTER)
            rebuildProviders();
    };

    providerList_ = panel_->addChild<sg::ListBox>();
    providerList_->pos(16, 130)->sz(260, 420);
    providerList_->onItemSelected = [this](int row)
    {
        if (row < 0 || row >= (int)providerRows_.size())
            return;
        selectedProvider_ = providerRows_[row];
        rebuildModels();
    };

    modelLabel_ = addLabel(panel_, 292, 76, 590, tr().catalogModel,
                           sg::Color(150, 153, 160));
    modelSearch_ = panel_->addChild<sg::TextBox>();
    modelSearch_->pos(292, 96)->sz(590, 28);
    modelSearch_->setPlaceholder(tr().catalogSearchModel);
    modelSearch_->onKeyDown = [this](sg::KeyEvent& e)
    {
        if (e.key == SDLK_RETURN || e.key == SDLK_KP_ENTER)
            rebuildModels();
    };

    modelList_ = panel_->addChild<sg::ListBox>();
    modelList_->pos(292, 130)->sz(590, 300);
    modelList_->onItemSelected = [this](int row)
    {
        if (row < 0 || row >= (int)modelRows_.size())
            return;
        selectedModel_ = modelRows_[row];
        updateDetails();
    };

    details_ = panel_->addChild<sg::Label>();
    details_->pos(292, 440)->sz(590, 116);
    details_->setWordWrap(true);
    details_->setTextColor(sg::Color(150, 153, 160));

    updateApi_ = panel_->addChild<sg::Button>();
    updateApi_->pos(16, 580)->sz(150, 32);
    updateApi_->setText(tr().updateApi);
    updateApi_->onClick = [this](sg::MouseEvent&)
    {
        if (onUpdateApi_)
            onUpdateApi_();
    };

    cancel_ = panel_->addChild<sg::Button>();
    cancel_->pos(680, 580)->sz(92, 32);
    cancel_->setText(tr().cancel);
    cancel_->onClick = [this](sg::MouseEvent&) { close(); };

    ok_ = panel_->addChild<sg::Button>();
    ok_->pos(782, 580)->sz(92, 32);
    ok_->setText(tr().ok);
    ok_->onClick = [this](sg::MouseEvent&) { accept(); };

    missingText_ = addLabel(panel_, 60, 140, 780, "", textColor_);
    missingText_->setWordWrap(true);
    missingText_->setSize(780, 120);

    missingFetch_ = panel_->addChild<sg::Button>();
    missingFetch_->pos(60, 280)->sz(200, 38);
    missingFetch_->setText(tr().fetchApiJson);
    missingFetch_->onClick = [this](sg::MouseEvent&)
    {
        if (onUpdateApi_)
            onUpdateApi_();
    };

    setMissingView(false);
    modal_->hideModal();
}

void ModelCatalogDialog::open(ApplyFn onApply)
{
    onApply_ = std::move(onApply);

    suppress_ = true;
    providerSearch_->setText("");
    modelSearch_->setText("");
    suppress_ = false;

    reload(false);
    modal_->showModal();
}

void ModelCatalogDialog::close()
{
    if (!modal_->isOpen())
        return;
    modal_->hideModal();
    onApply_ = nullptr;
    if (onClosed_)
        onClosed_();
}

bool ModelCatalogDialog::isOpen() const
{
    return modal_ && modal_->isOpen();
}

void ModelCatalogDialog::reload(bool force)
{
    cat_ = &catalog::load(force);

    selectedProvider_ = -1;
    selectedModel_ = -1;

    if (!cat_->loaded)
    {
        providerRows_.clear();
        modelRows_.clear();
        providerList_->clear();
        modelList_->clear();
        setMissingView(true);
        missingText_->setText(
            std::string(tr().apiJsonMissing) + "\n" +
            tr().apiJsonMissingHint + "\n\n" + cat_->error + "\n" +
            paths::toUtf8(cat_->path));
        return;
    }

    setMissingView(false);

    source_->setText(ellipsize(std::string(tr().catalogSource) + ": " +
                                   paths::toUtf8(cat_->path),
                               660));
    rebuildProviders();
}

void ModelCatalogDialog::refresh()
{
    if (modal_ && modal_->isOpen())
        reload(true);
}

void ModelCatalogDialog::setMissingView(bool missing)
{
    missingMode_ = missing;

    if (providerLabel_) providerLabel_->setVisible(!missing);
    if (providerSearch_) providerSearch_->setVisible(!missing);
    if (providerList_) providerList_->setVisible(!missing);
    if (modelLabel_) modelLabel_->setVisible(!missing);
    if (modelSearch_) modelSearch_->setVisible(!missing);
    if (modelList_) modelList_->setVisible(!missing);
    if (details_) details_->setVisible(!missing);
    if (source_) source_->setVisible(!missing);
    if (reload_) reload_->setVisible(!missing);
    if (updateApi_) updateApi_->setVisible(!missing);
    if (ok_) ok_->setVisible(!missing);
    if (missingText_) missingText_->setVisible(missing);
    if (missingFetch_) missingFetch_->setVisible(missing);
    if (cancel_)
        cancel_->setText(missing ? tr().back : tr().cancel);
}

void ModelCatalogDialog::rebuildProviders()
{
    providerList_->clear();
    providerRows_.clear();
    selectedProvider_ = -1;
    modelRows_.clear();
    modelList_->clear();
    selectedModel_ = -1;
    details_->setText("");

    if (!cat_ || !cat_->loaded)
        return;

    std::string query = lowerAscii(trim(providerSearch_->text()));

    for (int i = 0; i < (int)cat_->providers.size(); ++i)
    {
        const catalog::Provider& p = cat_->providers[i];
        if (!query.empty())
        {
            std::string hay = lowerAscii(p.name + " " + p.id);
            if (hay.find(query) == std::string::npos)
                continue;
        }

        providerRows_.push_back(i);
        std::string tag = p.compatible ? tr().catalogCompatible
                                       : tr().catalogGateway;
        providerList_->addItem(p.name + "   [" + tag + "]");
    }

    if (providerRows_.empty())
        return;

    providerList_->setSelectedIndex(0);
    selectedProvider_ = providerRows_.front();
    rebuildModels();
}

void ModelCatalogDialog::rebuildModels()
{
    modelList_->clear();
    modelRows_.clear();
    selectedModel_ = -1;
    details_->setText("");

    if (!cat_ || !cat_->loaded || selectedProvider_ < 0 ||
        selectedProvider_ >= (int)cat_->providers.size())
        return;

    const catalog::Provider& p = cat_->providers[selectedProvider_];
    std::string query = lowerAscii(trim(modelSearch_->text()));

    for (int i = 0; i < (int)p.models.size(); ++i)
    {
        const catalog::Model& m = p.models[i];
        if (!query.empty())
        {
            std::string hay = lowerAscii(m.name + " " + m.id);
            if (hay.find(query) == std::string::npos)
                continue;
        }

        modelRows_.push_back(i);
        modelList_->addItem(m.name);
    }

    if (modelRows_.empty())
        return;

    modelList_->setSelectedIndex(0);
    selectedModel_ = modelRows_.front();
    updateDetails();
}

void ModelCatalogDialog::updateDetails()
{
    if (!cat_ || !cat_->loaded || selectedProvider_ < 0 ||
        selectedModel_ < 0)
    {
        details_->setText("");
        return;
    }

    const catalog::Provider& p = cat_->providers[selectedProvider_];
    if (selectedModel_ < 0 || selectedModel_ >= (int)p.models.size())
    {
        details_->setText("");
        return;
    }

    const catalog::Model& m = p.models[selectedModel_];

    std::string text = m.name;
    if (m.name != m.id)
        text += "   (" + m.id + ")";
    text += "\n";
    text += std::string(tr().catalogContext) + ": " +
            (m.context > 0 ? groupNumber(m.context) : std::string("-"));
    text += "    " + std::string(tr().catalogOutput) + ": " +
            (m.output > 0 ? groupNumber(m.output) : std::string("-"));
    text += "    " + std::string(tr().catalogTools) + ": " +
            (m.toolCall ? tr().catalogToolsYes : tr().catalogToolsNo);

    if (m.costInput > 0.0 || m.costOutput > 0.0)
    {
        text += "\n" + std::string(tr().catalogCost) + ": $" +
                priceText(m.costInput) + " / $" + priceText(m.costOutput);
    }

    if (p.api.empty())
        text += "\nAPI 地址未提供，请手动填写。";

    details_->setText(text);
}

void ModelCatalogDialog::accept()
{
    if (!cat_ || !cat_->loaded || selectedProvider_ < 0 ||
        selectedModel_ < 0)
        return;

    const catalog::Provider& p = cat_->providers[selectedProvider_];
    if (selectedModel_ >= (int)p.models.size())
        return;

    catalog::Provider provider = p;
    catalog::Model model = p.models[selectedModel_];

    auto cb = std::move(onApply_);
    onApply_ = nullptr;
    close();

    if (cb)
        cb(provider, model);
}

void ModelCatalogDialog::applyTheme(const sg::Theme& theme)
{
    if (modal_)
        modal_->setOverlayColor(modalBackdrop(theme));
    if (panel_)
        panel_->setColors(theme.dialogBg, theme.border, 1);
    if (title_)
        title_->setTextColor(theme.dialogTitleText);
    if (source_)
        source_->setTextColor(theme.mutedText);
    if (providerLabel_)
        providerLabel_->setTextColor(theme.mutedText);
    if (modelLabel_)
        modelLabel_->setTextColor(theme.mutedText);
    if (details_)
        details_->setTextColor(theme.mutedText);
    if (missingText_)
        missingText_->setTextColor(theme.text);

    textColor_ = theme.text;
    mutedColor_ = theme.mutedText;
    errorColor_ = sg::Color(235, 110, 110);

    if (details_)
        details_->setTextColor(mutedColor_);
}

//==============================================================================
// ApiUpdateDialog
//==============================================================================

namespace
{

bool validHttpUrl(const std::string& text)
{
    std::string lower = toLower(trim(text));
    return lower.rfind("http://", 0) == 0 ||
           lower.rfind("https://", 0) == 0;
}

bool parsePort(const std::string& text, int& port)
{
    std::string value = trim(text);
    if (value.empty() || value.size() > 5)
        return false;
    for (char c : value)
    {
        if (c < '0' || c > '9')
            return false;
    }
    int parsed = std::atoi(value.c_str());
    if (parsed < 1 || parsed > 65535)
        return false;
    port = parsed;
    return true;
}

} //namespace

ApiUpdateDialog::ApiUpdateDialog(sg::Widget* root, sg::Window* window,
                                 std::function<void()> onClosed)
    : window_(window)
    , onClosed_(std::move(onClosed))
{
    modal_ = root->addChild<Modal>();
    modal_->pos(0, 0)->sz(root->width(), root->height());
    modal_->setAnchors(sg::AnchorLeft | sg::AnchorTop |
                       sg::AnchorRight | sg::AnchorBottom,
                       0, 0, 0, 0);

    panel_ = modal_->addChild<Panel>();
    panel_->pos(0, 0)->sz(720, 430);
    panel_->setColors(sg::Color(42, 44, 50), sg::Color(70, 73, 80), 1);
    modal_->setPanel(panel_);
    modal_->setResponsivePanel(true, 32, 24, 720, 430, 900, 620);

    title_ = addLabel(panel_, 24, 16, 640, tr().updateApi,
                      sg::Color(225, 228, 234));

    urlLabel_ = addLabel(panel_, 24, 64, 300, tr().downloadUrl,
                         sg::Color(200, 203, 210));
    url_ = addTextBox(panel_, 24, 86, 660);
    url_->setText("https://models.dev/api.json");

    proxy_ = panel_->addChild<sg::CheckBox>();
    proxy_->pos(24, 132)->sz(200, 24);
    proxy_->setText(tr().useProxy);
    auto oldProxyClick = proxy_->onClick;
    proxy_->onClick = [this, oldProxyClick](sg::MouseEvent& e)
    {
        if (oldProxyClick)
            oldProxyClick(e);
        updateProxyRow();
    };

    portLabel_ = addLabel(panel_, 250, 134, 100, tr().proxyPort,
                          sg::Color(200, 203, 210));
    port_ = addTextBox(panel_, 360, 128, 130);
    port_->setMaxLength(5);
    port_->setPlaceholder("7890");
    portLabel_->setVisible(false);
    port_->setVisible(false);

    addLabel(panel_, 24, 168, 660, tr().apiUpdateHint,
             sg::Color(135, 138, 145));

    progress_ = panel_->addChild<ProgressBar>();
    progress_->pos(24, 210)->sz(660, 22);
    progress_->setVisible(false);

    status_ = addLabel(panel_, 24, 244, 660, "", sg::Color(200, 203, 210));
    status_->setVisible(false);
    error_ = addLabel(panel_, 24, 272, 660, "", errorColor_);
    error_->setWordWrap(true);
    error_->setSize(660, 70);

    start_ = panel_->addChild<sg::Button>();
    start_->pos(0, 0)->sz(180, 40);
    start_->setAnchors(sg::AnchorLeft | sg::AnchorBottom, 24, 0, 0, 24);
    start_->setText(tr().startUpdate);
    start_->onClick = [this](sg::MouseEvent&) { start(); };

    back_ = panel_->addChild<sg::Button>();
    back_->pos(0, 0)->sz(180, 40);
    back_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 24, 24);
    back_->setText(tr().back);
    back_->onClick = [this](sg::MouseEvent&) { close(); };

    modal_->hideModal();
}

ApiUpdateDialog::~ApiUpdateDialog()
{
    cancel_.store(true);
    joinWorker();
}

void ApiUpdateDialog::open()
{
    cancel_.store(true);
    joinWorker();
    cancel_.store(false);
    finished_.store(false);
    percent_.store(0);

    url_->setText("https://models.dev/api.json");
    proxy_->setChecked(false);
    port_->setText("");
    progress_->setValue(0.0f);
    progress_->setVisible(false);
    status_->setText("");
    status_->setVisible(false);
    error_->setText("");
    setRunning(false);
    updateProxyRow();

    modal_->showModal();
}

void ApiUpdateDialog::close()
{
    cancel_.store(true);
    joinWorker();

    if (!modal_->isOpen())
        return;
    modal_->hideModal();
    if (onClosed_)
        onClosed_();
}

bool ApiUpdateDialog::isOpen() const
{
    return modal_ && modal_->isOpen();
}

void ApiUpdateDialog::setRunning(bool running)
{
    running_ = running;
    if (url_) url_->setEnabled(!running);
    if (proxy_) proxy_->setEnabled(!running);
    if (port_) port_->setEnabled(!running);
    if (start_) start_->setEnabled(!running);
    if (back_) back_->setEnabled(true);
    updateProxyRow();
}

void ApiUpdateDialog::updateProxyRow()
{
    if (!proxy_ || !port_ || !portLabel_)
        return;
    bool visible = proxy_->isChecked();
    portLabel_->setVisible(visible);
    port_->setVisible(visible);
    port_->setEnabled(visible && !running_);
}

void ApiUpdateDialog::start()
{
    if (running_)
        return;

    std::string url = trim(url_->text());
    if (!validHttpUrl(url))
    {
        error_->setText(tr().invalidUrl);
        return;
    }

    int proxyPort = 0;
    if (proxy_->isChecked() && !parsePort(port_->text(), proxyPort))
    {
        error_->setText(tr().invalidPort);
        return;
    }

    error_->setText("");
    status_->setText(tr().downloading);
    progress_->setValue(0.0f);
    progress_->setVisible(true);
    status_->setVisible(true);
    percent_.store(0);
    cancel_.store(false);
    finished_.store(false);
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_ = apiupdate::Result{};
    }

    apiupdate::Request request;
    request.url = url;
    request.proxyPort = proxyPort;
    request.target = paths::apiCatalogFile();

    setRunning(true);
    worker_ = std::thread([this, request]()
    {
        apiupdate::Result result =
            apiupdate::download(request, percent_, cancel_);
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_ = std::move(result);
        }
        finished_.store(true);
    });
}

void ApiUpdateDialog::joinWorker()
{
    if (worker_.joinable())
        worker_.join();
}

void ApiUpdateDialog::poll()
{
    if (!modal_ || !modal_->isOpen())
        return;

    updateProxyRow();

    if (running_)
    {
        int percent = percent_.load();
        progress_->setValue((float)percent / 100.0f);
        status_->setText(std::string(tr().downloading) + " " +
                         std::to_string(percent) + "%");
    }

    if (finished_.load())
    {
        joinWorker();
        setRunning(false);
        finished_.store(false);

        apiupdate::Result result;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result = result_;
        }

        if (result.ok)
        {
            progress_->setValue(1.0f);
            status_->setText(tr().downloadDone);
            error_->setText("");
            catalog::load(true);
        }
        else
        {
            status_->setText("");
            std::string message = std::string(tr().downloadFailed) +
                                  result.error;
            if (!result.savedPath.empty())
                message += "\n" + std::string(tr().downloadSaved) +
                           paths::toUtf8(result.savedPath);
            error_->setText(message);
        }

        if (panel_ && panel_->getWindow())
            panel_->getWindow()->requestRedraw();
    }
}

void ApiUpdateDialog::applyTheme(const sg::Theme& theme)
{
    if (modal_)
        modal_->setOverlayColor(modalBackdrop(theme));
    if (panel_)
        panel_->setColors(theme.dialogBg, theme.border, 1);
    if (title_)
        title_->setTextColor(theme.dialogTitleText);
    if (urlLabel_)
        urlLabel_->setTextColor(theme.mutedText);
    if (portLabel_)
        portLabel_->setTextColor(theme.mutedText);
    if (status_)
        status_->setTextColor(theme.mutedText);
    if (progress_)
        progress_->setColors(theme.inputBg, theme.menuAccent, theme.border);

    textColor_ = theme.text;
    mutedColor_ = theme.mutedText;
    errorColor_ = sg::Color(235, 110, 110);
}

} //namespace sdlagent::ui
