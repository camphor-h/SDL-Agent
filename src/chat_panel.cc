// SPDX-License-Identifier: GPL-3.0-only

#include "chat_panel.hpp"

#include "attachments.hpp"
#include "i18n.hpp"
#include "markdown.hpp"
#include "paths.hpp"
#include "session.hpp"
#include "ui_widgets.hpp"
#include "util.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace sdlagent::ui
{
namespace sg = sdlgui;

namespace
{

struct FilePickCtx
{
    std::shared_ptr<std::atomic<bool>> alive;
    std::function<void(const std::vector<std::string>&)> done;
};

void SDLCALL attachmentPicked(void* userdata, const char* const* files, int)
{
    std::unique_ptr<FilePickCtx> ctx(static_cast<FilePickCtx*>(userdata));
    if (!ctx->alive || !ctx->alive->load() || !files)
        return;

    std::vector<std::string> paths;
    for (int i = 0; files[i]; ++i)
        if (files[i][0])
            paths.push_back(files[i]);
    if (ctx->done)
        ctx->done(paths);
}

int estimateButtonWidth(const std::string& text)
{
    int w = (int)utf8Length(text) * 8 + 26;
    return std::clamp(w, 72, 220);
}

//Group thousands with commas, e.g. 1234567 -> "1,234,567".
std::string formatCount(long long value)
{
    if (value < 0)
        value = 0;
    std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    int lead = (int)(digits.size() % 3);
    for (std::size_t i = 0; i < digits.size(); ++i)
    {
        if (i > 0 && (int)(i % 3) == lead)
            out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

//Percent-encoded file:// URL so paths with spaces or CJK characters open
//correctly through the platform's default handler.
std::string fileUrl(const std::filesystem::path& p)
{
    std::string path = paths::toUtf8(p);
    std::replace(path.begin(), path.end(), '\\', '/');

    std::string url = "file://";
    if (path.empty() || path[0] != '/')
        url += "/";

    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : path)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
            c == '.' || c == '~' || c == ':')
        {
            url += (char)c;
        }
        else
        {
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 0x0F];
        }
    }
    return url;
}

} //namespace

//TextArea with a placeholder drawn while empty.
class InputBox : public sg::TextArea
{
public:
    using sg::TextArea::TextArea;

    std::string  placeholder;
    sg::Color    placeholderColor{ 130, 133, 140, 255 };

    void onRender(SDL_Renderer* renderer) override
    {
        sg::TextArea::onRender(renderer);

        if (!text().empty() || !composition().empty() || placeholder.empty())
            return;

        sg::Rect r = absoluteRect();
        drawClipped(renderer, placeholder, r.x + 5, r.y + 3,
                    std::max(0, r.w - 10), placeholderColor);
    }
};

ChatPanel::ChatPanel(sg::Widget* parent)
    : sg::SplitView(parent)
{
    alive_ = std::make_shared<std::atomic<bool>>(true);

    setOrientation(sg::SplitView::Orientation::Stacked);
    setSplit(0.78f);
    setMinPaneSize(120);
    setDividerWidth(1);
    setDividerHitWidth(8);

    view_ = addChild<ChatView>();
    view_->onLinkClick = [this](const std::string& href)
    {
        if (href.rfind("attach:", 0) == 0)
        {
            int messageIndex = 0;
            int attachmentIndex = 0;
            if (std::sscanf(href.c_str(), "attach:%d:%d", &messageIndex,
                            &attachmentIndex) == 2 &&
                session_ && messageIndex >= 0 &&
                messageIndex < (int)session_->messages.size())
            {
                const Message& m =
                    session_->messages[(std::size_t)messageIndex];
                if (attachmentIndex >= 0 &&
                    attachmentIndex < (int)m.attachments.size())
                {
                    const Attachment& a =
                        m.attachments[(std::size_t)attachmentIndex];
                    std::filesystem::path path;
                    std::string err;
                    if (!a.path.empty())
                        path = attachment::storedPath(sessionsRoot_, a);
                    else if (!attachment::materializeInline(a, path, err))
                        path.clear();

                    std::error_code ec;
                    bool exists = !path.empty() &&
                                  std::filesystem::exists(path, ec) && !ec;
                    if (exists)
                    {
                        std::string url = fileUrl(path);
                        SDL_OpenURL(url.c_str());
                    }
                    else if (getWindow())
                    {
                        getWindow()->showMessageBox(
                            tr().attach, tr().attachMissing,
                            sg::MessageBoxType::Warning,
                            sg::MessageBoxButtons::OK, nullptr);
                    }
                }
            }
            return;
        }
        if (href.rfind("reasoning:", 0) == 0)
        {
            int index = std::atoi(href.c_str() + 10);
            if (session_ && index >= 0 &&
                index < (int)session_->messages.size())
            {
                Message& m = session_->messages[(std::size_t)index];
                m.reasoningExplicit = true;
                m.reasoningExpanded = !m.reasoningExpanded;
                refresh();
            }
            return;
        }
        if (href == "confirm:allow")
        {
            if (onConfirmation)
                onConfirmation(ConfirmChoice::Allow);
            return;
        }
        if (href == "confirm:deny")
        {
            if (onConfirmation)
                onConfirmation(ConfirmChoice::Deny);
            return;
        }
        if (href == "confirm:stop")
        {
            if (onConfirmation)
                onConfirmation(ConfirmChoice::Stop);
            return;
        }
        if (!href.empty())
            SDL_OpenURL(href.c_str());
    };

    view_->onContextMenu = [this](sg::MouseEvent&, sg::ContextMenu& menu)
    {
        std::string selected = view_->selectedText();
        if (!selected.empty())
        {
            menu.addItem(tr().copy, [selected]
            {
                SDL_SetClipboardText(selected.c_str());
            });
        }
        menu.addItem(tr().selectAll, [this] { view_->selectAll(); });
    };

    inputPanel_ = addChild<Panel>();
    inputPanel_->setColors(sg::Color(30, 31, 35), sg::Color(52, 54, 60), 1);

    attachRow_ = inputPanel_->addChild<sg::Widget>();
    attachRow_->pos(8, 6)->sz(400, 28);
    attachRow_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight,
                           8, 6, 8, 0);
    attachRow_->setVisible(false);

    //Buttons float inside the input box; content insets keep text clear of
    //the bottom-right corner.
    input_ = inputPanel_->addChild<InputBox>();
    input_->pos(8, 8)->sz(400, 80);
    input_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                           sg::AnchorBottom,
                       8, 8, 8, 34);
    input_->setContentInsets(5, 5, 5, 44);
    input_->setWordWrap(true);
    input_->setLineSpacing(2);
    input_->scrollbarH(false);

    //Enter sends, Shift+Enter inserts a newline.
    input_->keyMap().remove(sg::TextEditAction::InsertNewline);
    input_->keyMap().add(sg::TextEditAction::InsertNewline, SDLK_RETURN,
                         SDL_KMOD_SHIFT);

    input_->onKeyDown = [this](sg::KeyEvent& e)
    {
        if ((e.mod & SDL_KMOD_CTRL) && e.key == SDLK_N)
        {
            if (onNewChat)
                onNewChat();
            return;
        }
        if ((e.mod & SDL_KMOD_CTRL) && e.key == SDLK_COMMA)
        {
            if (onOpenSettings)
                onOpenSettings();
            return;
        }
        if ((e.key == SDLK_RETURN || e.key == SDLK_KP_ENTER) &&
            (e.mod & SDL_KMOD_SHIFT) == 0)
        {
            if (onSend)
                onSend(input_->text());
        }
    };

    attachButton_ = inputPanel_->addChild<GlyphButton>();
    attachButton_->pos(0, 0)->sz(30, 30);
    attachButton_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 72, 41);
    attachButton_->setGlyph(GlyphButton::Glyph::Plus);
    attachButton_->onClick = [this](sg::MouseEvent&) { openFileDialog(); };

    if (paths::isMobile())
    {
        cameraButton_ = inputPanel_->addChild<GlyphButton>();
        cameraButton_->pos(0, 0)->sz(30, 30);
        cameraButton_->setAnchors(sg::AnchorLeft | sg::AnchorBottom,
                                  16, 0, 0, 41);
        cameraButton_->setGlyph(GlyphButton::Glyph::Camera);
        cameraButton_->onClick = [this](sg::MouseEvent&)
        {
            if (onCamera)
                onCamera();
        };
    }

    hint_ = inputPanel_->addChild<sg::Label>();
    hint_->pos(14, 18)->sz(420, 18);
    hint_->setAnchors(sg::AnchorLeft | sg::AnchorRight | sg::AnchorBottom,
                      14, 0, 14, 14);
    hint_->setText("");
    hint_->setTextColor(sg::Color(150, 153, 160));

    usageLabel_ = inputPanel_->addChild<sg::Label>();
    usageLabel_->pos(0, 0)->sz(160, 18);
    usageLabel_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 14, 14);
    usageLabel_->setTextAlign(sg::RichTextAlign::Right);
    usageLabel_->setText("");
    usageLabel_->setTextColor(sg::Color(150, 153, 160));
    usageLabel_->setVisible(false);

    //Golden-ratio rectangle (48 x 30) with the return symbol.
    send_ = inputPanel_->addChild<GlyphButton>();
    send_->pos(0, 0)->sz(48, 30);
    send_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 16, 41);
    send_->setGlyph(GlyphButton::Glyph::Return);
    send_->onClick = [this](sg::MouseEvent&)
    {
        if (onSend)
            onSend(input_->text());
    };

    stop_ = inputPanel_->addChild<sg::Button>();
    stop_->pos(0, 0)->sz(48, 30);
    stop_->setAnchors(sg::AnchorRight | sg::AnchorBottom, 0, 0, 16, 41);
    stop_->setText(tr().stop);
    stop_->setVisible(false);
    stop_->onClick = [this](sg::MouseEvent&)
    {
        if (onCancel)
            onCancel();
    };
}

ChatPanel::~ChatPanel()
{
    if (alive_)
        alive_->store(false);
}

void ChatPanel::openFileDialog()
{
    static const SDL_DialogFileFilter filters[] = {
        { "文本与代码", "txt;md;markdown;json;csv;log;xml;yaml;yml;c;h;cc;cpp;"
                        "hpp;py;js;ts;rs;go;java;sh;ps1;html;css;sql" },
        { "图片", "png;jpg;jpeg;webp;gif;bmp" },
        { "所有文件", "*" },
    };

    auto* ctx = new FilePickCtx;
    ctx->alive = alive_;
    ctx->done = [this](const std::vector<std::string>& paths)
    {
        std::string firstError;
        for (const std::string& path : paths)
        {
            std::string err;
            addPickedFile(path, err);
            if (firstError.empty() && !err.empty())
                firstError = err;
        }
        if (!firstError.empty() && getWindow())
        {
            getWindow()->showMessageBox(tr().attach, firstError.c_str(),
                                        sg::MessageBoxType::Warning,
                                        sg::MessageBoxButtons::OK, nullptr);
        }
    };

    SDL_ShowOpenFileDialog(attachmentPicked, ctx,
                           getWindow() ? getWindow()->handle() : nullptr,
                           filters, (int)(sizeof(filters) / sizeof(filters[0])),
                           nullptr, true);
}

void ChatPanel::addPickedFile(const std::string& path, std::string& err)
{
    Attachment a;
    if (!attachment::inspect(paths::fromUtf8(path), imageAllowed_, a, err))
        return;

    int images = 0;
    int texts = 0;
    uint64_t textBytes = 0;
    uint64_t imageBytes = 0;
    for (const Attachment& p : pending_)
    {
        if (p.kind == "image")
        {
            ++images;
            imageBytes += p.size;
        }
        else
        {
            ++texts;
            textBytes += p.size;
        }
    }

    if ((int)pending_.size() >= attachment::kMaxFiles)
    {
        err = tr().attachTooMany;
        return;
    }
    if (a.kind == "image")
    {
        if (images >= attachment::kMaxImages ||
            imageBytes + a.size > attachment::kMaxImageTotal)
        {
            err = tr().attachTooMany;
            return;
        }
    }
    else
    {
        if (texts >= attachment::kMaxFiles ||
            textBytes + a.size > attachment::kMaxTextTotal)
        {
            err = tr().attachTooMany;
            return;
        }
    }

    pending_.push_back(std::move(a));
    rebuildAttachmentRow();
}

void ChatPanel::removeAttachment(std::size_t index)
{
    if (index >= pending_.size())
        return;
    pending_.erase(pending_.begin() + (std::ptrdiff_t)index);
    rebuildAttachmentRow();
}

void ChatPanel::rebuildAttachmentRow()
{
    if (!attachRow_)
        return;

    std::vector<sg::Widget*> old(attachRow_->children().size(), nullptr);
    for (std::size_t i = 0; i < attachRow_->children().size(); ++i)
        old[i] = attachRow_->children()[i].get();
    for (sg::Widget* w : old)
        attachRow_->removeChild(w);

    int x = 0;
    for (std::size_t i = 0; i < pending_.size(); ++i)
    {
        const Attachment& a = pending_[i];
        std::string name = a.name;
        if (utf8Length(name) > 16)
            name = utf8Prefix(name, 16) + "…";

        auto* b = attachRow_->addChild<sg::Button>();
        std::string text = name + " ×";
        int width = estimateButtonWidth(text);
        b->pos(x, 2)->sz(width, 23);
        b->setText(text);
        b->onClick = [this, i](sg::MouseEvent&) { removeAttachment(i); };
        x += width + 6;
    }

    attachRow_->setVisible(!pending_.empty());
    updateComposerLayout();
}

void ChatPanel::updateComposerLayout()
{
    if (!input_)
        return;
    int top = pending_.empty() ? 8 : 40;
    input_->setAnchors(sg::AnchorLeft | sg::AnchorTop | sg::AnchorRight |
                           sg::AnchorBottom,
                       8, top, 8, 34);
    if (getWindow())
        getWindow()->requestRedraw();
}

void ChatPanel::clearAttachments()
{
    pending_.clear();
    rebuildAttachmentRow();
}

void ChatPanel::setSessionsRoot(const std::filesystem::path& root)
{
    sessionsRoot_ = root;
}

void ChatPanel::setImageInputSupported(bool supported)
{
    imageAllowed_ = supported;
    if (!supported)
    {
        bool changed = false;
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            if (it->kind == "image")
            {
                it = pending_.erase(it);
                changed = true;
            }
            else
            {
                ++it;
            }
        }
        if (changed)
            rebuildAttachmentRow();
    }
}

void ChatPanel::setSession(Session* session)
{
    if (session != session_)
        clearAttachments();
    session_ = session;
    refresh(true);
}

void ChatPanel::setShowReasoning(bool show)
{
    showReasoning_ = show;
    refresh();
}

void ChatPanel::setStatus(const std::string& status)
{
    status_ = status;
    updateHint();
}

void ChatPanel::setContextUsage(int tokens, int limit)
{
    usageTokens_ = tokens > 0 ? tokens : 0;
    usageLimit_ = limit > 0 ? limit : 0;
    updateHint();
}

void ChatPanel::setModelName(const std::string& model)
{
    modelName_ = model;
    updateHint();
}

void ChatPanel::updateHint()
{
    if (!hint_)
        return;

    std::string usage;
    if (usageTokens_ > 0)
    {
        usage = formatCount(usageTokens_);
        if (usageLimit_ > 0)
        {
            long long percent =
                ((long long)usageTokens_ * 100 + usageLimit_ / 2) / usageLimit_;
            if (percent > 999)
                percent = 999;
            usage += " (" + std::to_string(percent) + "%)";
        }
    }

    std::string text = modelName_;
    auto append = [&text](const std::string& part)
    {
        if (part.empty())
            return;
        if (!text.empty())
            text += " · ";
        text += part;
    };
    append(status_);
    hint_->setText(text);

    if (!usageLabel_)
        return;

    usageLabel_->setText(usage);
    usageLabel_->setVisible(!usage.empty());
    hint_->setAnchors(sg::AnchorLeft | sg::AnchorRight | sg::AnchorBottom,
                      14, 0, usage.empty() ? 14 : 14 + 160 + 12, 14);
}

void ChatPanel::setBusy(bool busy)
{
    busy_ = busy;
    if (send_)
        send_->setVisible(!busy);
    if (stop_)
        stop_->setVisible(busy);

    status_ = busy ? tr().statusThinking : "";
    updateHint();

    if (getWindow())
        getWindow()->requestRedraw();
}

void ChatPanel::buildConfirmation(std::vector<sg::RichTextBlock>& blocks) const
{
    sg::RichTextBlock card;
    card.isCard = true;
    card.paddingX = 0;
    card.paddingY = 10;
    card.borderWidth = 1;
    card.marginTop = 14;
    card.marginBottom = 14;

    sg::RichTextBlock title;
    title.type = sg::RichBlockType::Paragraph;
    title.indent = 12;
    title.marginTop = 0;
    title.marginBottom = 4;

    sg::RichTextRun titleRun;
    titleRun.text = std::string(tr().confirmTitle) + " · " + confirmTool_;
    titleRun.style.color = accentColor_;
    titleRun.style.font = boldFont();
    title.runs.push_back(std::move(titleRun));
    card.children.push_back(std::move(title));

    if (!confirmSummary_.empty())
    {
        sg::RichTextBlock summary;
        summary.type = sg::RichBlockType::Paragraph;
        summary.indent = 12;
        summary.marginTop = 0;
        summary.marginBottom = 4;

        sg::RichTextRun run;
        run.text = confirmSummary_;
        run.style.color = mutedColor_;
        summary.runs.push_back(std::move(run));
        card.children.push_back(std::move(summary));
    }

    if (!confirmDetail_.empty())
    {
        sg::RichTextBlock detail;
        detail.type = sg::RichBlockType::Paragraph;
        detail.indent = 12;
        detail.marginTop = 0;
        detail.marginBottom = 4;

        sg::RichTextRun run;
        run.text = confirmDetail_;
        run.style.color = sg::Color(0, 0, 0, 0);
        detail.runs.push_back(std::move(run));
        card.children.push_back(std::move(detail));
    }

    sg::RichTextBlock actions;
    actions.type = sg::RichBlockType::Actions;
    actions.actionsVertical = true;
    actions.actionHeight = 34;
    actions.actionGap = 6;
    actions.paddingX = 0;
    actions.marginTop = 4;
    actions.marginBottom = 0;

    auto addAction = [&](const char* text, ConfirmChoice choice)
    {
        sg::RichTextAction action;
        action.text = text;
        action.textAlign = sg::RichTextAlign::Left;
        action.paddingX = 12;
        action.background = sg::Color(30, 31, 35, 255);
        action.textColor = sg::Color(245, 245, 245, 255);
        action.onClick = [this, choice]
        {
            if (onConfirmation)
                onConfirmation(choice);
        };
        actions.actions.push_back(std::move(action));
    };

    addAction(tr().allow, ConfirmChoice::Allow);
    addAction(tr().deny, ConfirmChoice::Deny);
    addAction(tr().stopRun, ConfirmChoice::Stop);
    card.children.push_back(std::move(actions));
    blocks.push_back(std::move(card));
}

void ChatPanel::setConfirmation(const std::string& tool,
                                const std::string& summary,
                                const std::string& detail)
{
    confirmActive_ = true;
    confirmTool_ = tool;
    confirmSummary_ = summary;
    confirmDetail_ = detail;
    refresh(true);
}

void ChatPanel::clearConfirmation()
{
    if (!confirmActive_)
        return;
    confirmActive_ = false;
    confirmTool_.clear();
    confirmSummary_.clear();
    confirmDetail_.clear();
    refresh();
}

void ChatPanel::refresh(bool forceBottom)
{
    if (!view_)
        return;

    if (!session_)
    {
        view_->setEmptyState("");
        view_->clear();
        return;
    }

    bool wasNearBottom = view_->nearBottom();

    std::vector<sg::RichTextBlock> blocks;
    if (session_->messages.empty())
    {
        view_->setEmptyState(tr().emptyChatTitle);
    }
    else
    {
        view_->setEmptyState("");
        const auto& msgs = session_->messages;
        for (std::size_t i = 0; i < msgs.size(); ++i)
        {
            Message& m = session_->messages[i];
            if (!m.reasoningExplicit)
                m.reasoningExpanded = showReasoning_;

            std::vector<std::pair<int, const Message*>> results;
            std::size_t next = i + 1;
            if (m.role == Role::Assistant && !m.toolCalls.empty())
            {
                while (next < msgs.size() && msgs[next].role == Role::Tool)
                {
                    results.push_back({ (int)next, &msgs[next] });
                    ++next;
                }
            }

            std::vector<sg::RichTextBlock> part =
                renderMessage(m, (int)i,
                              results.empty() ? nullptr : &results);

            if (m.role == Role::User)
            {
                for (auto& b : part)
                {
                    //Attachment metadata lines stay muted; only the text
                    //bubble itself gets the user bubble colors.
                    if (b.background.a == 0)
                        continue;
                    b.background = userBubbleBg_;
                    for (auto& run : b.runs)
                        run.style.color = userBubbleText_;
                }
            }
            for (auto& b : part)
                blocks.push_back(std::move(b));

            if (next > i + 1)
                i = next - 1;
        }
    }

    if (confirmActive_)
        buildConfirmation(blocks);

    for (auto& b : blocks)
    {
        if (b.isCard)
        {
            b.background = cardBg_;
            b.borderColor = cardBorder_;
            b.borderWidth = 1;
        }
    }

    view_->setBlocks(std::move(blocks));
    if (forceBottom || wasNearBottom)
        view_->scrollToBottom();
}

std::string ChatPanel::inputText() const
{
    return input_ ? input_->text() : std::string();
}

void ChatPanel::clearInput()
{
    if (input_)
    {
        input_->setText("");
        input_->clearSelection();
    }
}

void ChatPanel::focusInput()
{
    if (input_ && getWindow())
        getWindow()->requestRedraw();
}

sg::Widget* ChatPanel::inputWidget() const
{
    return input_;
}

void ChatPanel::applyTheme(const sg::Theme& theme)
{
    accentColor_ = theme.menuAccent;
    mutedColor_ = theme.mutedText;
    setDividerColor(sg::Color(0, 0, 0, 255));

    int lum = (theme.windowBg.r * 299 + theme.windowBg.g * 587 +
               theme.windowBg.b * 114) / 1000;
    if (lum > 128)
    {
        userBubbleBg_ = sg::Color(226, 235, 248, 255);
        userBubbleText_ = sg::Color(30, 36, 46, 255);
    }
    else
    {
        userBubbleBg_ = sg::Color(44, 52, 66, 255);
        userBubbleText_ = sg::Color(232, 234, 238, 255);
    }

    if (inputPanel_)
        inputPanel_->setColors(theme.panelBg, theme.divider, 1);
    if (input_)
    {
        input_->setBackgroundColor(theme.inputBg);
        input_->setBorderColor(theme.inputBorder);
    }
    if (attachButton_)
    {
        attachButton_->setColors(theme.inputBg, theme.buttonHover,
                                 theme.buttonPressed, theme.mutedText);
    }
    if (cameraButton_)
    {
        cameraButton_->setColors(theme.inputBg, theme.buttonHover,
                                 theme.buttonPressed, theme.mutedText);
    }
    if (send_)
    {
        send_->setColors(theme.inputBg, theme.buttonHover,
                         theme.buttonPressed, theme.mutedText);
    }
    if (stop_)
    {
        stop_->setBgColor(theme.buttonBg, theme.buttonHover,
                          theme.buttonPressed);
        stop_->setTextColor(theme.buttonText);
        stop_->setBorderColor(theme.border);
    }
    if (hint_)
        hint_->setTextColor(theme.mutedText);
    if (usageLabel_)
        usageLabel_->setTextColor(theme.mutedText);
    if (input_)
        input_->placeholderColor = theme.mutedText;
    if (view_)
    {
        view_->setDefaultTextColor(theme.text);
        view_->setEmptyStateColor(theme.menuAccent);
        view_->setLinkColor(theme.menuAccent);
        view_->setCodeBackground(theme.panelBg);
        view_->setCodeBorder(theme.panelBorder);
        view_->setQuoteBarColor(theme.menuAccent);
        view_->setTableColors(theme.panelBg, theme.menuHover, theme.panelBorder);
        view_->setSeparatorColor(theme.border);
        view_->setActionColors(theme.buttonBg, theme.buttonText);
    }

    cardBg_ = theme.inputBg;
    cardBorder_ = theme.border;

    if (session_)
        refresh();
}

} //namespace sdlagent::ui
