// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "sdlgui.hpp"
#include "session.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sdlagent::ui
{

class ChatView;
class InputBox;
class Panel;
class GlyphButton;

//Right-hand pane: chat history + input box, split by a draggable divider.
class ChatPanel : public sdlgui::SplitView
{
public:
    explicit ChatPanel(sdlgui::Widget* parent = nullptr);
    ~ChatPanel() override;

    void setSession(Session* session);
    void setShowReasoning(bool show);
    void setStatus(const std::string& status);
    void setBusy(bool busy);
    void setContextUsage(int tokens, int limit);
    void setModelName(const std::string& model);
    void setImageInputSupported(bool supported);
    void setSessionsRoot(const std::filesystem::path& root);
    void setWebSearchEnabled(bool enabled);
    void setWebSearchAvailable(bool available);

    void        refresh(bool forceBottom = false);
    std::string inputText() const;
    void        clearInput();
    void        focusInput();
    sdlgui::Widget* inputWidget() const;

    //Attachments currently staged in the composer. `path` is the original
    //absolute path until the app copies them into the session directory.
    const std::vector<Attachment>& pendingAttachments() const
    {
        return pending_;
    }
    void clearAttachments();

    enum class ConfirmChoice { Allow, Deny };

    void setConfirmation(const std::string& tool, const std::string& summary,
                         const std::string& detail);
    void clearConfirmation();
    bool hasConfirmation() const { return confirmActive_; }

    void applyTheme(const sdlgui::Theme& theme);

    //Stage a file chosen outside the built-in picker (e.g. camera capture).
    void addPickedFile(const std::string& path, std::string& err);

    std::function<void(const std::string&)> onSend;
    std::function<void()>                   onCancel;
    std::function<void()>                   onNewChat;
    std::function<void()>                   onOpenSettings;
    std::function<void()>                   onCamera;
    std::function<void(ConfirmChoice)>      onConfirmation;
    std::function<void(bool)>               onWebSearchToggle;

private:
    void buildConfirmation(std::vector<sdlgui::RichTextBlock>& blocks) const;

    void onFontChanged() override;
    void openFileDialog();
    void removeAttachment(std::size_t index);
    void rebuildAttachmentRow();
    void updateComposerLayout();
    void updateHint();

    ChatView*            view_ = nullptr;
    Panel*               inputPanel_ = nullptr;
    InputBox*            input_ = nullptr;
    sdlgui::Label*       hint_ = nullptr;
    sdlgui::Label*       usageLabel_ = nullptr;
    GlyphButton*         send_ = nullptr;
    sdlgui::Button*      stop_ = nullptr;
    GlyphButton*         attachButton_ = nullptr;
    GlyphButton*         cameraButton_ = nullptr;
    GlyphButton*         webSearchButton_ = nullptr;
    sdlgui::Widget*      attachRow_ = nullptr;
    Session*             session_ = nullptr;
    std::filesystem::path sessionsRoot_;
    std::vector<Attachment> pending_;
    std::shared_ptr<std::atomic<bool>> alive_;
    bool                 imageAllowed_ = true;
    bool                 showReasoning_ = false;
    bool                 busy_ = false;
    bool                 webSearchEnabled_ = false;
    bool                 webSearchAvailable_ = false;
    std::string          status_;
    std::string          modelName_;
    int                  usageTokens_ = 0;
    int                  usageLimit_ = 0;
    bool                 confirmActive_ = false;
    std::string          confirmTool_;
    std::string          confirmSummary_;
    std::string          confirmDetail_;
    sdlgui::Color        accentColor_{ 88, 150, 230, 255 };
    sdlgui::Color        mutedColor_{ 150, 153, 160, 255 };
    sdlgui::Color        userBubbleBg_{ 44, 52, 66, 255 };
    sdlgui::Color        userBubbleText_{ 232, 234, 238, 255 };
    sdlgui::Color        cardBg_{ 24, 25, 29, 255 };
    sdlgui::Color        cardBorder_{ 58, 60, 66, 255 };
};

} //namespace sdlagent::ui
