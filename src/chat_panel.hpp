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
    void setImageInputSupported(bool supported);
    void setSessionsRoot(const std::filesystem::path& root);

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

    enum class ConfirmChoice { Allow, Deny, Stop };

    void setConfirmation(const std::string& tool, const std::string& summary,
                         const std::string& detail);
    void clearConfirmation();
    bool hasConfirmation() const { return confirmActive_; }

    void applyTheme(const sdlgui::Theme& theme);

    std::function<void(const std::string&)> onSend;
    std::function<void()>                   onCancel;
    std::function<void()>                   onNewChat;
    std::function<void()>                   onOpenSettings;
    std::function<void(ConfirmChoice)>      onConfirmation;

private:
    void buildWelcome(std::vector<sdlgui::RichTextBlock>& blocks) const;
    void buildConfirmation(std::vector<sdlgui::RichTextBlock>& blocks) const;

    void openFileDialog();
    void addPickedFile(const std::string& path, std::string& err);
    void removeAttachment(std::size_t index);
    void rebuildAttachmentRow();
    void updateComposerLayout();

    ChatView*            view_ = nullptr;
    Panel*               inputPanel_ = nullptr;
    InputBox*            input_ = nullptr;
    sdlgui::Label*       hint_ = nullptr;
    sdlgui::Button*      send_ = nullptr;
    sdlgui::Button*      stop_ = nullptr;
    sdlgui::Button*      attachButton_ = nullptr;
    sdlgui::Widget*      attachRow_ = nullptr;
    Session*             session_ = nullptr;
    std::filesystem::path sessionsRoot_;
    std::vector<Attachment> pending_;
    std::shared_ptr<std::atomic<bool>> alive_;
    bool                 imageAllowed_ = true;
    bool                 showReasoning_ = false;
    bool                 busy_ = false;
    bool                 confirmActive_ = false;
    std::string          confirmTool_;
    std::string          confirmSummary_;
    std::string          confirmDetail_;
    Panel*               confirmPanel_ = nullptr;
    sdlgui::Label*       confirmTitle_ = nullptr;
    sdlgui::Label*       confirmSummaryLabel_ = nullptr;
    sdlgui::Label*       confirmDetailLabel_ = nullptr;
    sdlgui::Button*      confirmAllow_ = nullptr;
    sdlgui::Button*      confirmDeny_ = nullptr;
    sdlgui::Button*      confirmStop_ = nullptr;
    sdlgui::Color        accentColor_{ 88, 150, 230, 255 };
    sdlgui::Color        mutedColor_{ 150, 153, 160, 255 };
    sdlgui::Color        userBubbleBg_{ 44, 52, 66, 255 };
    sdlgui::Color        userBubbleText_{ 232, 234, 238, 255 };
    sdlgui::Color        cardBg_{ 24, 25, 29, 255 };
    sdlgui::Color        cardBorder_{ 58, 60, 66, 255 };
};

} //namespace sdlagent::ui
