// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "config.hpp"
#include "font.hpp"
#include "model_catalog.hpp"
#include "api_update.hpp"
#include "sdlgui.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdlagent::ui
{

class Modal;
class Panel;
class FontDialog;
class ModelCatalogDialog;
class ApiUpdateDialog;
class ProgressBar;

class ConfirmDialog;

enum class ConfirmDecision
{
    Allow,
    Deny,
    Stop
};

struct ConfirmationRequestView
{
    std::string tool;
    std::string summary;
    std::string detail;
};

//Settings: API / appearance / custom / about.
class SettingsDialog
{
public:
    using SaveFn = std::function<bool(const Config&, std::string&)>;
    using ThemePreviewFn = std::function<void(const std::string&)>;

    SettingsDialog(sdlgui::Widget* root, sdlgui::Window* window, SaveFn onSave,
                   ThemePreviewFn onThemePreview,
                   std::function<void()> onClosed);
    ~SettingsDialog();

    void open(const Config& cfg);
    void close();
    bool isOpen() const;
    void applyTheme(const sdlgui::Theme& theme);
    void refreshLogsIfVisible();
    void poll();

    sdlgui::Widget* firstField() const;

private:
    void build(sdlgui::Widget* root);
    void accept();
    void layoutFields(int panelW, int panelH);
    void selectPage(int index);
    void rebuildProfileList();
    void editProfile(int index);
    void saveProfileForm();
    void deleteSelectedProfile();
    void updateFontSummary();
    void refreshLogs();
    void exportLogs();

    sdlgui::Window*   window_ = nullptr;
    Modal*    modal_ = nullptr;
    Panel*    panel_ = nullptr;

    sdlgui::Label*    title_ = nullptr;
    sdlgui::Label*    error_ = nullptr;

    //pages
    sdlgui::Button*   tabApi_ = nullptr;
    sdlgui::Button*   tabAppearance_ = nullptr;
    sdlgui::Button*   tabCustom_ = nullptr;
    sdlgui::Button*   tabAbout_ = nullptr;
    sdlgui::Widget*   configListPage_ = nullptr;
    sdlgui::ScrollView* formScroll_ = nullptr;
    sdlgui::Widget*   formContent_ = nullptr;
    sdlgui::ScrollView* appearanceScroll_ = nullptr;
    sdlgui::ScrollView* customScroll_ = nullptr;
    sdlgui::ScrollView* aboutScroll_ = nullptr;
    sdlgui::Widget*   appearanceContent_ = nullptr;
    sdlgui::Widget*   customContent_ = nullptr;
    sdlgui::Widget*   aboutContent_ = nullptr;
    int               page_ = -1;

    //profiles
    sdlgui::ListBox*  profileList_ = nullptr;
    sdlgui::Button*   newProfile_ = nullptr;
    sdlgui::Button*   deleteProfile_ = nullptr;
    sdlgui::Button*   saveProfile_ = nullptr;
    std::vector<ApiProfile> profiles_;
    std::string       activeProfileId_;
    //Profile name that was filled automatically from the catalog; used to
    //keep updating it when another provider is picked, while leaving
    //manually edited names alone.
    std::string       autoProfileName_;
    int               editingProfile_ = -1;
    int               lastClickedProfile_ = -1;

    //profile form
    sdlgui::TextBox*  profileName_ = nullptr;
    sdlgui::TextBox*  baseUrl_ = nullptr;
    sdlgui::TextBox*  apiKey_ = nullptr;
    sdlgui::TextBox*  model_ = nullptr;
    sdlgui::Label*    contextTokensLabel_ = nullptr;
    sdlgui::TextBox*  contextTokens_ = nullptr;
    sdlgui::Label*    maxTokensLabel_ = nullptr;
    sdlgui::TextBox*  maxTokens_ = nullptr;
    sdlgui::Button*   catalogButton_ = nullptr;
    std::unique_ptr<ModelCatalogDialog> catalogDialog_;
    std::unique_ptr<ApiUpdateDialog>    apiUpdateDialog_;

    //appearance
    sdlgui::DropDown* theme_ = nullptr;
    sdlgui::DropDown* language_ = nullptr;
    sdlgui::CheckBox* showReasoning_ = nullptr;
    sdlgui::Label*    fontSummary_ = nullptr;
    sdlgui::Button*   fontButton_ = nullptr;
    std::string       fontBackendValue_ = "ttf";
    std::string       fontPathValue_;
    int               fontPointSizeValue_ = 16;
    std::unique_ptr<FontDialog> fontDialog_;

    //custom
    sdlgui::TextBox*  sessions_ = nullptr;
    sdlgui::Button*   pickSessions_ = nullptr;
    sdlgui::Button*   openSessions_ = nullptr;
    sdlgui::TextBox*  dataDir_ = nullptr;
    sdlgui::Button*   pickDataDir_ = nullptr;
    sdlgui::Button*   openDataDir_ = nullptr;
    sdlgui::Label*    configDirLabel_ = nullptr;
    sdlgui::Label*    configFileLabel_ = nullptr;
    sdlgui::Button*   openConfig_ = nullptr;
    sdlgui::DropDown* toolMode_ = nullptr;
    sdlgui::CheckBox* autoTitle_ = nullptr;
    sdlgui::Label*    sysPath_ = nullptr;
    sdlgui::Label*    toolPath_ = nullptr;
    sdlgui::Label*    titlePath_ = nullptr;
    sdlgui::Button*   editSys_ = nullptr;
    sdlgui::Button*   editTool_ = nullptr;
    sdlgui::Button*   editTitle_ = nullptr;

    //about
    sdlgui::Label* aboutText_ = nullptr;

    //logs
    sdlgui::RichTextView* logView_ = nullptr;
    sdlgui::Button*       logButton_ = nullptr;
    sdlgui::Button*       logExport_ = nullptr;
    uint64_t              logRevision_ = 0;

    SaveFn               onSave_;
    ThemePreviewFn       onThemePreview_;
    std::function<void()> onClosed_;
    std::shared_ptr<std::atomic<bool>> alive_;
    Config original_;
};

//Small single-line prompt (rename).
class InputDialog
{
public:
    InputDialog(sdlgui::Widget* root, std::function<void()> onClosed);

    void open(const std::string& title, const std::string& initial,
              std::function<void(const std::string&)> onAccept);
    void close();
    bool isOpen() const;
    void applyTheme(const sdlgui::Theme& theme);

    sdlgui::Widget* firstField() const;

private:
    void accept();

    Modal*  modal_ = nullptr;
    Panel*  panel_ = nullptr;
    sdlgui::Label*  title_ = nullptr;
    sdlgui::TextBox* input_ = nullptr;
    std::function<void(const std::string&)> onAccept_;
    std::function<void()>                   onClosed_;
};

//Tool approval dialog.
class ConfirmDialog
{
public:
    ConfirmDialog(sdlgui::Widget* root, std::function<void()> onClosed);

    void open(const ConfirmationRequestView& request,
              std::function<void(ConfirmDecision)> onDecision);
    void close();
    bool isOpen() const;
    void applyTheme(const sdlgui::Theme& theme);

private:
    void layoutPanel(int w, int h);

    Modal*    modal_ = nullptr;
    Panel*    panel_ = nullptr;
    sdlgui::Label*    title_ = nullptr;
    sdlgui::Label*    summary_ = nullptr;
    sdlgui::TextArea* detail_ = nullptr;
    sdlgui::Button*   stop_ = nullptr;
    sdlgui::Button*   deny_ = nullptr;
    sdlgui::Button*   allow_ = nullptr;
    std::function<void(ConfirmDecision)> onDecision_;
    std::function<void()>                onClosed_;
};

//SDLEdit-style font picker: font list, rendered preview, size.
class FontDialog
{
public:
    explicit FontDialog(sdlgui::Widget* root,
                        std::function<void()> onClosed = {});

    void open(const std::string& backend, const std::string& path, int size,
              std::function<void(std::string, std::string, int)> onApply);
    void close();
    bool isOpen() const;
    void applyTheme(const sdlgui::Theme& theme);

private:
    void rebuildList();
    void updatePreview();
    void accept();

    Modal*  modal_ = nullptr;
    Panel*  panel_ = nullptr;
    sdlgui::Label*    title_ = nullptr;
    sdlgui::Label*    listLabel_ = nullptr;
    sdlgui::Label*    previewLabel_ = nullptr;
    sdlgui::Label*    sizeLabel_ = nullptr;
    sdlgui::Label*    info_ = nullptr;
    sdlgui::ListBox*  list_ = nullptr;
    sdlgui::Canvas*   preview_ = nullptr;
    sdlgui::ComboBox* sizeCombo_ = nullptr;
    sdlgui::Button*   cancel_ = nullptr;
    sdlgui::Button*   apply_ = nullptr;

    std::vector<font::FontEntry> entries_;
    int         selected_ = 0;
    int         size_ = 16;
    std::string backend_ = "builtin";
    std::string path_;

    std::shared_ptr<sdlgui::Font> previewFont_;
    std::string previewError_;

    sdlgui::Color textColor_{ 226, 228, 233, 255 };
    sdlgui::Color mutedColor_{ 150, 153, 160, 255 };
    sdlgui::Color bgColor_{ 38, 40, 46, 255 };
    sdlgui::Color borderColor_{ 70, 73, 80, 255 };
    sdlgui::Color accentColor_{ 88, 150, 230, 255 };
    sdlgui::Color previewBg_{ 24, 25, 29, 255 };
    sdlgui::Color previewText_{ 226, 228, 233, 255 };
    sdlgui::Color errorColor_{ 235, 110, 110, 255 };

    std::function<void(std::string, std::string, int)> onApply_;
    std::function<void()> onClosed_;
};

//Provider/model picker backed by <config dir>/api.json.
class ModelCatalogDialog
{
public:
    using ApplyFn = std::function<void(const catalog::Provider&,
                                       const catalog::Model&)>;

    explicit ModelCatalogDialog(sdlgui::Widget* root,
                                std::function<void()> onUpdateApi = {},
                                std::function<void()> onClosed = {});

    void open(ApplyFn onApply);
    void close();
    bool isOpen() const;
    void applyTheme(const sdlgui::Theme& theme);
    void refresh();

private:
    void reload(bool force);
    void rebuildProviders();
    void rebuildModels();
    void updateDetails();
    void accept();
    void setMissingView(bool missing);

    Modal*  modal_ = nullptr;
    Panel*  panel_ = nullptr;
    sdlgui::Label*   title_ = nullptr;
    sdlgui::Label*   source_ = nullptr;
    sdlgui::Button*  reload_ = nullptr;
    sdlgui::Label*   providerLabel_ = nullptr;
    sdlgui::Label*   modelLabel_ = nullptr;
    sdlgui::TextBox* providerSearch_ = nullptr;
    sdlgui::TextBox* modelSearch_ = nullptr;
    sdlgui::ListBox* providerList_ = nullptr;
    sdlgui::ListBox* modelList_ = nullptr;
    sdlgui::Label*   details_ = nullptr;
    sdlgui::Button*  updateApi_ = nullptr;
    sdlgui::Button*  cancel_ = nullptr;
    sdlgui::Button*  ok_ = nullptr;
    sdlgui::Label*   missingText_ = nullptr;
    sdlgui::Button*  missingFetch_ = nullptr;
    bool             missingMode_ = false;

    std::vector<int> providerRows_;
    std::vector<int> modelRows_;
    int selectedProvider_ = -1;
    int selectedModel_ = -1;
    bool suppress_ = false;

    const catalog::Catalog* cat_ = nullptr;

    ApplyFn onApply_;
    std::function<void()> onUpdateApi_;
    std::function<void()> onClosed_;

    sdlgui::Color textColor_{ 226, 228, 233, 255 };
    sdlgui::Color mutedColor_{ 150, 153, 160, 255 };
    sdlgui::Color errorColor_{ 235, 110, 110, 255 };
};

//Modal used by the settings page to download a fresh api.json.
class ApiUpdateDialog
{
public:
    ApiUpdateDialog(sdlgui::Widget* root, sdlgui::Window* window,
                    std::function<void()> onClosed = {});
    ~ApiUpdateDialog();

    void open();
    void close();
    bool isOpen() const;
    void applyTheme(const sdlgui::Theme& theme);
    void poll();

private:
    void start();
    void joinWorker();
    void setRunning(bool running);
    void updateProxyRow();

    sdlgui::Window* window_ = nullptr;
    Modal*          modal_ = nullptr;
    Panel*          panel_ = nullptr;

    sdlgui::Label*    title_ = nullptr;
    sdlgui::Label*    urlLabel_ = nullptr;
    sdlgui::TextBox*  url_ = nullptr;
    sdlgui::CheckBox* proxy_ = nullptr;
    sdlgui::Label*    portLabel_ = nullptr;
    sdlgui::TextBox*  port_ = nullptr;
    ProgressBar*      progress_ = nullptr;
    sdlgui::Label*    status_ = nullptr;
    sdlgui::Label*    error_ = nullptr;
    sdlgui::Button*   start_ = nullptr;
    sdlgui::Button*   back_ = nullptr;

    std::thread        worker_;
    std::atomic<bool>  cancel_{ false };
    std::atomic<int>   percent_{ 0 };
    std::atomic<bool>  finished_{ false };
    std::mutex         resultMutex_;
    apiupdate::Result  result_;
    bool               running_ = false;

    std::function<void()> onClosed_;

    sdlgui::Color textColor_{ 226, 228, 233, 255 };
    sdlgui::Color mutedColor_{ 150, 153, 160, 255 };
    sdlgui::Color errorColor_{ 235, 110, 110, 255 };
};

} //namespace sdlagent::ui
