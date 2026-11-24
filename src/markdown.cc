// SPDX-License-Identifier: GPL-3.0-only

#include "markdown.hpp"

#include "attachments.hpp"
#include "i18n.hpp"
#include "json.hpp"
#include "session.hpp"
#include "ui_widgets.hpp"
#include "util.hpp"

#include <md4c.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace sdlagent::ui
{
namespace
{
namespace sg = sdlgui;

//==============================================================================
// md4c -> RichTextView
//==============================================================================

struct SpanState
{
    bool        bold = false;
    bool        italic = false;
    bool        code = false;
    bool        strike = false;
    std::string href;
};

struct MdState
{
    std::vector<sg::RichTextBlock> blocks;
    std::vector<int>               prevCurrent;
    int current = -1;

    int quoteDepth = 0;
    int listDepth = 0;
    std::vector<bool> ordered;
    std::vector<int>  counters;
    std::vector<int>  liBlocks;

    std::string codeBuf;
    bool        inCode = false;

    bool inTable = false;
    int  tableIndex = -1;
    int  row = -1;
    int  col = -1;
    bool sawHeader = false;

    std::vector<SpanState> spans;

    sg::Color inlineCodeBg{ 48, 49, 56, 255 };
    sg::Color inlineCodeFg{ 228, 184, 126, 255 };
    sg::Color headingColor{ 118, 170, 255, 255 };
    sg::Color dimColor{ 150, 153, 160, 255 };
    sg::Color strikeColor{ 128, 130, 136, 255 };
    sg::Color blockColor{ 0, 0, 0, 0 };
};

bool sameStyle(const sg::RichTextStyle& a, const sg::RichTextStyle& b)
{
    return a.font == b.font && a.color.r == b.color.r &&
           a.color.g == b.color.g && a.color.b == b.color.b &&
           a.color.a == b.color.a && a.background.r == b.background.r &&
           a.background.g == b.background.g &&
           a.background.b == b.background.b &&
           a.background.a == b.background.a && a.underline == b.underline;
}

sg::RichTextStyle currentStyle(const MdState& st)
{
    bool bold = false, code = false, strike = false;
    std::string href;

    for (const SpanState& s : st.spans)
    {
        bold = bold || s.bold;
        code = code || s.code;
        strike = strike || s.strike;
        if (!s.href.empty())
            href = s.href;
    }

    sg::RichTextStyle style;
    style.underline = false;

    if (code)
    {
        style.background = st.inlineCodeBg;
        style.color = st.inlineCodeFg;
    }
    else if (st.blockColor.a != 0)
    {
        style.color = st.blockColor;
    }
    else if (strike)
    {
        style.color = st.strikeColor;
    }

    if (bold)
        style.font = boldFont();

    return style;
}

std::string currentHref(const MdState& st)
{
    for (auto it = st.spans.rbegin(); it != st.spans.rend(); ++it)
        if (!it->href.empty())
            return it->href;
    return {};
}

void appendRunTo(std::vector<sg::RichTextRun>& runs, const std::string& text,
                 const sg::RichTextStyle& style, const std::string& href)
{
    if (text.empty())
        return;

    if (!runs.empty() && runs.back().href == href &&
        sameStyle(runs.back().style, style))
    {
        runs.back().text += text;
        return;
    }

    sg::RichTextRun run;
    run.text = text;
    run.style = style;
    run.href = href;
    runs.push_back(std::move(run));
}

sg::RichTextBlock makeBlock(sg::RichBlockType type)
{
    sg::RichTextBlock b;
    b.type = type;
    switch (type)
    {
        case sg::RichBlockType::Paragraph:
            b.marginTop = 2;
            b.marginBottom = 8;
            break;
        case sg::RichBlockType::Heading:
            b.marginTop = 10;
            b.marginBottom = 6;
            break;
        case sg::RichBlockType::CodeBlock:
            b.marginTop = 4;
            b.marginBottom = 8;
            b.wrap = false;
            break;
        case sg::RichBlockType::Quote:
            b.marginTop = 4;
            b.marginBottom = 8;
            b.indent = 10;
            break;
        case sg::RichBlockType::ListItem:
            b.marginTop = 0;
            b.marginBottom = 3;
            b.indent = 12;
            break;
        case sg::RichBlockType::Table:
            b.marginTop = 4;
            b.marginBottom = 8;
            break;
        case sg::RichBlockType::Separator:
            b.marginTop = 6;
            b.marginBottom = 6;
            break;
        case sg::RichBlockType::Actions:
            b.marginTop = 4;
            b.marginBottom = 8;
            break;
    }
    return b;
}

void pushBlock(MdState& st, sg::RichTextBlock block)
{
    st.prevCurrent.push_back(st.current);
    st.blocks.push_back(std::move(block));
    st.current = (int)st.blocks.size() - 1;
}

void popBlock(MdState& st)
{
    if (st.prevCurrent.empty())
    {
        st.current = -1;
        return;
    }
    st.current = st.prevCurrent.back();
    st.prevCurrent.pop_back();
}

bool isInsideList(const MdState& st)
{
    return !st.liBlocks.empty() && st.current == st.liBlocks.back();
}

void appendText(MdState& st, const std::string& text)
{
    if (text.empty())
        return;

    if (st.inCode)
    {
        st.codeBuf += text;
        return;
    }

    if (st.inTable && st.tableIndex >= 0 && st.row >= 0 && st.col >= 0)
    {
        sg::RichTextBlock& block = st.blocks[(size_t)st.tableIndex];
        sg::RichTableRow& r = block.table.rows[(size_t)st.row];
        if (st.col < (int)r.cells.size())
            appendRunTo(r.cells[(size_t)st.col].runs, text, currentStyle(st),
                        currentHref(st));
        return;
    }

    if (st.current < 0)
        pushBlock(st, makeBlock(sg::RichBlockType::Paragraph));

    sg::RichTextBlock& block = st.blocks[(size_t)st.current];
    appendRunTo(block.runs, text, currentStyle(st), currentHref(st));
}

std::string decodeEntity(const std::string& entity)
{
    if (entity.size() < 3 || entity.front() != '&' || entity.back() != ';')
        return entity;

    std::string body = entity.substr(1, entity.size() - 2);
    if (body == "amp")  return "&";
    if (body == "lt")   return "<";
    if (body == "gt")   return ">";
    if (body == "quot") return "\"";
    if (body == "apos") return "'";
    if (body == "nbsp") return " ";

    if (!body.empty() && body[0] == '#')
    {
        long value = 0;
        if (body.size() > 1 && (body[1] == 'x' || body[1] == 'X'))
            value = std::strtol(body.c_str() + 2, nullptr, 16);
        else
            value = std::strtol(body.c_str() + 1, nullptr, 10);

        if (value <= 0)
            return entity;
        if (value < 0x80)
            return std::string(1, (char)value);

        std::string out;
        if (value < 0x800)
        {
            out += (char)(0xC0 | (value >> 6));
            out += (char)(0x80 | (value & 0x3F));
        }
        else if (value < 0x10000)
        {
            out += (char)(0xE0 | (value >> 12));
            out += (char)(0x80 | ((value >> 6) & 0x3F));
            out += (char)(0x80 | (value & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (value >> 18));
            out += (char)(0x80 | ((value >> 12) & 0x3F));
            out += (char)(0x80 | ((value >> 6) & 0x3F));
            out += (char)(0x80 | (value & 0x3F));
        }
        return out;
    }
    return entity;
}

int onEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    MdState& st = *static_cast<MdState*>(userdata);

    switch (type)
    {
        case MD_BLOCK_DOC:
            break;

        case MD_BLOCK_QUOTE:
            ++st.quoteDepth;
            break;

        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            ++st.listDepth;
            st.ordered.push_back(type == MD_BLOCK_OL);
            st.counters.push_back(0);
            break;

        case MD_BLOCK_LI:
        {
            std::string bullet = "•";
            if (!st.ordered.empty() && st.ordered.back())
            {
                if (!st.counters.empty())
                    ++st.counters.back();
                bullet = std::to_string(st.counters.empty() ? 1 : st.counters.back()) + ".";
            }

            if (detail)
            {
                auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
                if (li->is_task)
                    bullet = li->task_mark ? "☑" : "☐";
            }

            sg::RichTextBlock b = makeBlock(sg::RichBlockType::ListItem);
            b.indent = std::max(0, st.listDepth - 1) * 16;
            b.marginBottom = 3;

            sg::RichTextRun run;
            run.text = bullet + " ";
            run.style.color = st.dimColor;
            b.runs.push_back(std::move(run));

            pushBlock(st, std::move(b));
            st.liBlocks.push_back(st.current);
            break;
        }

        case MD_BLOCK_P:
            if (st.inTable)
                break;

            if (isInsideList(st))
            {
                if (!st.blocks[(size_t)st.current].runs.empty())
                    appendText(st, "\n");
                break;
            }

            pushBlock(st, makeBlock(st.quoteDepth > 0
                                        ? sg::RichBlockType::Quote
                                        : sg::RichBlockType::Paragraph));
            break;

        case MD_BLOCK_H:
        {
            int level = 1;
            if (detail)
                level = ((MD_BLOCK_H_DETAIL*)detail)->level;
            level = std::max(1, std::min(level, 6));

            sg::RichTextBlock b = makeBlock(sg::RichBlockType::Heading);
            b.headingLevel = level;
            if (level == 1)
            {
                b.marginTop = 12;
                b.marginBottom = 8;
            }
            pushBlock(st, std::move(b));
            st.blockColor = st.headingColor;
            break;
        }

        case MD_BLOCK_CODE:
            pushBlock(st, makeBlock(sg::RichBlockType::CodeBlock));
            st.codeBuf.clear();
            st.inCode = true;
            break;

        case MD_BLOCK_HR:
        {
            //Use the rich-text separator primitive: a string of box-drawing
            //characters is wider than a phone screen and forces the view to
            //scroll sideways.
            sg::RichTextBlock b = makeBlock(sg::RichBlockType::Separator);
            b.marginTop = 6;
            b.marginBottom = 10;
            b.separatorMarginTop = 6;
            b.separatorMarginBottom = 6;
            b.separatorThickness = 1;
            st.blocks.push_back(std::move(b));
            break;
        }

        case MD_BLOCK_TABLE:
        {
            sg::RichTextBlock b = makeBlock(sg::RichBlockType::Table);
            pushBlock(st, std::move(b));
            st.tableIndex = st.current;
            st.inTable = true;
            st.sawHeader = false;
            break;
        }

        case MD_BLOCK_THEAD:
            st.sawHeader = true;
            break;

        case MD_BLOCK_TR:
        {
            if (st.tableIndex < 0)
                break;
            sg::RichTextBlock& b = st.blocks[(size_t)st.tableIndex];
            b.table.rows.push_back(sg::RichTableRow{});
            st.row = (int)b.table.rows.size() - 1;
            st.col = -1;
            break;
        }

        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
        {
            if (st.tableIndex < 0 || st.row < 0)
                break;
            sg::RichTextBlock& b = st.blocks[(size_t)st.tableIndex];
            sg::RichTableRow& row = b.table.rows[(size_t)st.row];
            row.cells.push_back(sg::RichTableCell{});
            st.col = (int)row.cells.size() - 1;
            break;
        }

        case MD_BLOCK_TBODY:
        case MD_BLOCK_HTML:
            break;
    }
    return 0;
}

int onLeaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    (void)detail;
    MdState& st = *static_cast<MdState*>(userdata);

    switch (type)
    {
        case MD_BLOCK_QUOTE:
            --st.quoteDepth;
            break;

        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            --st.listDepth;
            if (!st.ordered.empty()) st.ordered.pop_back();
            if (!st.counters.empty()) st.counters.pop_back();
            break;

        case MD_BLOCK_LI:
            if (!st.liBlocks.empty())
            {
                st.liBlocks.pop_back();
                popBlock(st);
            }
            break;

        case MD_BLOCK_P:
            if (!st.inTable && !isInsideList(st))
                popBlock(st);
            break;

        case MD_BLOCK_H:
            st.blockColor = sg::Color(0, 0, 0, 0);
            popBlock(st);
            break;

        case MD_BLOCK_CODE:
        {
            if (st.current >= 0)
            {
                sg::RichTextBlock& b = st.blocks[(size_t)st.current];
                sg::RichTextRun run;
                run.text = st.codeBuf;
                b.runs.push_back(std::move(run));
            }
            st.codeBuf.clear();
            st.inCode = false;
            popBlock(st);
            break;
        }

        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            st.col = -1;
            break;

        case MD_BLOCK_TR:
            st.row = -1;
            break;

        case MD_BLOCK_TABLE:
        {
            if (st.tableIndex >= 0)
            {
                sg::RichTextBlock& b = st.blocks[(size_t)st.tableIndex];
                b.table.headerRow = st.sawHeader;
            }
            st.inTable = false;
            st.tableIndex = -1;
            popBlock(st);
            break;
        }

        default:
            break;
    }
    return 0;
}

int onEnterSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    (void)detail;
    MdState& st = *static_cast<MdState*>(userdata);

    SpanState span;
    if (!st.spans.empty())
        span = st.spans.back();

    switch (type)
    {
        case MD_SPAN_STRONG: span.bold = true; break;
        case MD_SPAN_EM:     span.italic = true; break;
        case MD_SPAN_CODE:   span.code = true; break;
        case MD_SPAN_DEL:    span.strike = true; break;
        case MD_SPAN_A:
            if (detail)
                span.href = ((MD_SPAN_A_DETAIL*)detail)->href.text
                                ? std::string(((MD_SPAN_A_DETAIL*)detail)->href.text,
                                              ((MD_SPAN_A_DETAIL*)detail)->href.size)
                                : std::string();
            break;
        default:
            break;
    }
    st.spans.push_back(std::move(span));
    return 0;
}

int onLeaveSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    (void)type;
    (void)detail;
    MdState& st = *static_cast<MdState*>(userdata);
    if (!st.spans.empty())
        st.spans.pop_back();
    return 0;
}

int onText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    MdState& st = *static_cast<MdState*>(userdata);
    if (!text)
        return 0;

    std::string chunk(text, (std::size_t)size);

    switch (type)
    {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
        case MD_TEXT_HTML:
        case MD_TEXT_LATEXMATH:
            appendText(st, chunk);
            break;

        case MD_TEXT_ENTITY:
            appendText(st, decodeEntity(chunk));
            break;

        case MD_TEXT_BR:
            appendText(st, "\n");
            break;

        case MD_TEXT_SOFTBR:
            appendText(st, " ");
            break;

        case MD_TEXT_NULLCHAR:
            break;
    }
    return 0;
}

//==============================================================================
// message helpers
//==============================================================================
void addLabel(std::vector<sg::RichTextBlock>& out, const std::string& text,
              sg::Color color)
{
    sg::RichTextBlock b;
    b.type = sg::RichBlockType::Paragraph;
    b.marginTop = 10;
    b.marginBottom = 2;

    sg::RichTextRun run;
    run.text = text;
    run.style.color = color;
    run.style.font = boldFont();
    b.runs.push_back(std::move(run));
    out.push_back(std::move(b));
}

void addCode(std::vector<sg::RichTextBlock>& out, const std::string& text)
{
    sg::RichTextBlock b;
    b.type = sg::RichBlockType::CodeBlock;
    b.marginTop = 2;
    b.marginBottom = 8;
    b.wrap = false;

    sg::RichTextRun run;
    run.text = text;
    b.runs.push_back(std::move(run));
    out.push_back(std::move(b));
}

std::string prettyJson(const std::string& text)
{
    Json j = Json::parse(text);
    if (!j.valid())
        return text;
    std::string pretty = j.dump(true);
    return pretty.empty() ? text : pretty;
}

std::vector<std::string> splitLines(const std::string& text)
{
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
        std::string line = text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        start = nl + 1;
    }
    return lines;
}

void appendRun(sg::RichTextBlock& block, const std::string& text,
               sg::Color color)
{
    sg::RichTextRun run;
    run.text = text;
    run.style.color = color;
    block.runs.push_back(std::move(run));
}

void appendToolDiffRuns(sg::RichTextBlock& block, const std::string& path,
                        const std::string& oldText,
                        const std::string& newText)
{
    std::vector<std::string> a = splitLines(oldText);
    std::vector<std::string> b = splitLines(newText);

    appendRun(block, "@ " + path + "\n", sg::Color(120, 150, 190, 255));

    const std::size_t maxCells = 40000;
    if (!a.empty() && !b.empty() && a.size() * b.size() <= maxCells &&
        a.size() <= 400 && b.size() <= 400)
    {
        std::vector<std::vector<int>> lcs(
            a.size() + 1, std::vector<int>(b.size() + 1, 0));
        for (int i = (int)a.size() - 1; i >= 0; --i)
            for (int j = (int)b.size() - 1; j >= 0; --j)
                lcs[i][j] = (a[(size_t)i] == b[(size_t)j])
                                ? lcs[i + 1][j + 1] + 1
                                : std::max(lcs[i + 1][j], lcs[i][j + 1]);

        std::size_t i = 0, j = 0;
        while (i < a.size() && j < b.size())
        {
            if (a[i] == b[j])
            {
                appendRun(block, " " + a[i] + "\n",
                          sg::Color(0, 0, 0, 0));
                ++i;
                ++j;
            }
            else if (lcs[i + 1][j] >= lcs[i][j + 1])
            {
                appendRun(block, "-" + a[i] + "\n",
                          sg::Color(225, 95, 95, 255));
                ++i;
            }
            else
            {
                appendRun(block, "+" + b[j] + "\n",
                          sg::Color(95, 180, 115, 255));
                ++j;
            }
        }
        while (i < a.size())
        {
            appendRun(block, "-" + a[i++] + "\n",
                      sg::Color(225, 95, 95, 255));
        }
        while (j < b.size())
        {
            appendRun(block, "+" + b[j++] + "\n",
                      sg::Color(95, 180, 115, 255));
        }
    }
    else
    {
        for (const auto& line : a)
            appendRun(block, "-" + line + "\n",
                      sg::Color(225, 95, 95, 255));
        for (const auto& line : b)
            appendRun(block, "+" + line + "\n",
                      sg::Color(95, 180, 115, 255));
    }
}

void appendToolCallRuns(sg::RichTextBlock& block, const std::string& name,
                        const std::string& argsJson)
{
    Json args = Json::parse(argsJson);
    if (!args.valid())
    {
        appendRun(block, argsJson + "\n", sg::Color(0, 0, 0, 0));
        return;
    }

    auto clip = [](std::string s, size_t maxCp)
    {
        if (utf8Length(s) > maxCp)
            s = utf8Prefix(s, maxCp) + "\n…[已截断]";
        return s;
    };

    if (name == "shell")
    {
        std::string command = args["command"].str();
        std::string cwd = args["cwd"].str();
        std::string text = "$ " + clip(command, 4000) + "\n";
        if (!cwd.empty())
            text += "\ncwd: " + cwd + "\n";
        appendRun(block, text, sg::Color(0, 0, 0, 0));
        return;
    }

    if (name == "edit_file")
    {
        std::string path = args["path"].str();
        std::string oldText = args["old_string"].str();
        std::string newText = args["new_string"].str();
        bool replaceAll = args["replace_all"].boolean(false);
        if (replaceAll)
            path += "  (replace_all)";
        appendToolDiffRuns(block, path, oldText, newText);
        return;
    }

    if (name == "write_file")
    {
        std::string path = args["path"].str();
        std::string content = args["content"].str();
        appendRun(block,
                  "path: " + path + "\n\n" + clip(content, 4000) + "\n",
                  sg::Color(0, 0, 0, 0));
        return;
    }

    if (name == "read_file")
    {
        std::string path = args["path"].str();
        long long offset = args["offset"].integer(1);
        long long limit = args["limit"].integer(2000);
        appendRun(block,
                  "path: " + path + "\noffset: " +
                      std::to_string(offset > 0 ? offset : 1) +
                      "\nlimit: " + std::to_string(limit) + "\n",
                  sg::Color(0, 0, 0, 0));
        return;
    }

    if (name == "web_search")
    {
        appendRun(block, "query: " + args["query"].str() + "\n",
                  sg::Color(0, 0, 0, 0));
        return;
    }

    appendRun(block, prettyJson(argsJson) + "\n", sg::Color(0, 0, 0, 0));
}

void appendToolResultRuns(sg::RichTextBlock& block, const std::string& content,
                          bool error)
{
    std::string text = content.empty() ? "(无输出)" : content;
    if (text.empty() || text.back() != '\n')
        text += "\n";
    appendRun(block, text,
              error ? sg::Color(235, 105, 105, 255)
                    : sg::Color(0, 0, 0, 0));
}

} //namespace

std::vector<sg::RichTextBlock> renderMarkdown(const std::string& text)
{
    //hard limit so one giant message cannot freeze the UI
    std::string input = text;
    bool clipped = false;
    if (input.size() > 200000)
    {
        input = utf8Prefix(input, 120000);
        clipped = true;
    }

    MdState st;

    MD_PARSER parser;
    std::memset(&parser, 0, sizeof(parser));
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    parser.enter_block = onEnterBlock;
    parser.leave_block = onLeaveBlock;
    parser.enter_span = onEnterSpan;
    parser.leave_span = onLeaveSpan;
    parser.text = onText;

    md_parse(input.c_str(), (MD_SIZE)input.size(), &parser, &st);

    if (clipped)
    {
        sg::RichTextBlock b;
        b.type = sg::RichBlockType::Paragraph;
        sg::RichTextRun run;
        run.text = "[内容过长，仅显示前一部分]";
        run.style.color = st.dimColor;
        b.runs.push_back(std::move(run));
        st.blocks.push_back(std::move(b));
    }

    return st.blocks;
}

std::vector<sg::RichTextBlock> renderMessage(
    const Message& m, int messageIndex,
    const std::vector<std::pair<int, const Message*>>* toolResults)
{
    using namespace sdlagent;

    std::vector<sg::RichTextBlock> out;
    const Strings& s = tr();

    if (m.role == Role::System)
        return out;

    if (m.role == Role::User)
    {
        for (std::size_t ai = 0; ai < m.attachments.size(); ++ai)
        {
            const Attachment& a = m.attachments[ai];
            sg::RichTextBlock meta;
            meta.type = sg::RichBlockType::Paragraph;
            meta.marginTop = 6;
            meta.marginBottom = 0;
            meta.align = sg::RichTextAlign::Right;
            meta.maxWidth = 560;

            sg::RichTextRun run;
            run.text = attachment::historyLine(a);
            run.href = "attach:" + std::to_string(messageIndex) + ":" +
                       std::to_string(ai);
            run.style.color = sg::Color(140, 143, 150, 255);
            meta.runs.push_back(std::move(run));
            out.push_back(std::move(meta));
        }

        if (m.content.empty())
            return out;

        sg::RichTextBlock b;
        b.type = sg::RichBlockType::Paragraph;
        b.marginTop = 8;
        b.marginBottom = 12;
        b.background = sg::Color(38, 46, 62, 255);
        b.align = sg::RichTextAlign::Right;
        b.maxWidth = 560;
        b.paddingX = 12;

        sg::RichTextRun body;
        body.text = m.content;
        body.style.color = sg::Color(232, 234, 238, 255);
        b.runs.push_back(std::move(body));

        out.push_back(std::move(b));
        return out;
    }

    if (m.role == Role::Tool)
    {
        addLabel(out, std::string(s.toolResultLabel) + " · " + m.toolName,
                 m.error ? sg::Color(235, 120, 120, 255)
                         : sg::Color(140, 160, 190, 255));
        addCode(out, m.content.empty() ? "(无输出)" : m.content);

        for (std::size_t ai = 0; ai < m.attachments.size(); ++ai)
        {
            const Attachment& a = m.attachments[ai];
            sg::RichTextBlock meta;
            meta.type = sg::RichBlockType::Paragraph;
            meta.marginTop = 4;
            meta.marginBottom = 0;

            sg::RichTextRun run;
            run.text = attachment::historyLine(a);
            run.href = "attach:" + std::to_string(messageIndex) + ":" +
                       std::to_string(ai);
            run.style.color = sg::Color(140, 143, 150, 255);
            meta.runs.push_back(std::move(run));
            out.push_back(std::move(meta));
        }
        return out;
    }

    //assistant
    if (!m.reasoning.empty())
    {
        sg::RichTextBlock header;
        header.type = sg::RichBlockType::Paragraph;
        header.marginTop = 8;
        header.marginBottom = m.reasoningExpanded ? 2 : 8;

        sg::RichTextRun toggle;
        toggle.text = s.reasoningToggle;
        toggle.href = "reasoning:" + std::to_string(messageIndex);
        toggle.style.color = sg::Color(140, 143, 150, 255);
        header.runs.push_back(std::move(toggle));
        out.push_back(std::move(header));

        if (m.reasoningExpanded)
        {
            sg::RichTextBlock quote;
            quote.type = sg::RichBlockType::Quote;
            quote.indent = 10;
            quote.marginTop = 2;
            quote.marginBottom = 10;

            sg::RichTextRun run;
            run.text = m.reasoning;
            run.style.color = sg::Color(140, 143, 150, 255);
            quote.runs.push_back(std::move(run));
            out.push_back(std::move(quote));
        }
    }

    if (m.error && !m.content.empty())
    {
        addLabel(out, s.errorLabel, sg::Color(235, 100, 100, 255));

        sg::RichTextBlock b;
        b.type = sg::RichBlockType::Paragraph;
        b.marginTop = 2;
        b.marginBottom = 8;

        sg::RichTextRun run;
        run.text = m.content;
        run.style.color = sg::Color(235, 120, 120, 255);
        b.runs.push_back(std::move(run));
        out.push_back(std::move(b));
        return out;
    }

    if (!m.content.empty())
    {
        std::vector<sg::RichTextBlock> md = renderMarkdown(m.content);
        for (auto& b : md)
            out.push_back(std::move(b));
    }

    if (!m.toolCalls.empty())
    {
        Json calls = Json::parse(m.toolCalls);
        if (calls.isArray())
        {
            for (std::size_t i = 0; i < calls.size(); ++i)
            {
                Json call = calls[i];
                std::string name = call["function"]["name"].str("tool");
                std::string args = call["function"]["arguments"].str();
                std::string callId = call["id"].str();

                const Message* result = nullptr;
                int resultIndex = -1;
                if (toolResults)
                {
                    for (const auto& item : *toolResults)
                    {
                        const Message* r = item.second;
                        if (r && !callId.empty() && r->toolCallId == callId)
                        {
                            result = r;
                            resultIndex = item.first;
                            break;
                        }
                    }
                    if (!result && i < toolResults->size())
                    {
                        result = (*toolResults)[i].second;
                        resultIndex = (*toolResults)[i].first;
                    }
                }

                sg::RichTextBlock card;
                card.isCard = true;
                card.paddingX = 0;
                card.paddingY = 10;
                card.borderWidth = 1;
                card.marginTop = 2;
                card.marginBottom = 10;

                sg::RichTextBlock title;
                title.type = sg::RichBlockType::Paragraph;
                title.indent = 12;
                title.marginTop = 0;
                title.marginBottom = 4;
                appendRun(title,
                          std::string(s.toolCallLabel) + " · " + name,
                          sg::Color(150, 190, 140, 255));
                title.runs.back().style.font = boldFont();
                card.children.push_back(std::move(title));

                sg::RichTextBlock body;
                body.type = sg::RichBlockType::Paragraph;
                body.indent = 12;
                body.marginTop = 0;
                body.marginBottom = 0;
                appendToolCallRuns(body, name, args);
                card.children.push_back(std::move(body));

                if (result)
                {
                    sg::RichTextBlock sep;
                    sep.type = sg::RichBlockType::Separator;
                    sep.separatorMarginTop = 6;
                    sep.separatorMarginBottom = 6;
                    card.children.push_back(std::move(sep));

                    sg::RichTextBlock res;
                    res.type = sg::RichBlockType::Paragraph;
                    res.indent = 12;
                    res.marginTop = 0;
                    res.marginBottom = 0;
                    appendToolResultRuns(res, result->content, result->error);
                    card.children.push_back(std::move(res));

                    for (std::size_t ai = 0;
                         ai < result->attachments.size(); ++ai)
                    {
                        const Attachment& a = result->attachments[ai];
                        sg::RichTextBlock meta;
                        meta.type = sg::RichBlockType::Paragraph;
                        meta.indent = 12;
                        meta.marginTop = 4;
                        meta.marginBottom = 0;

                        sg::RichTextRun run;
                        run.text = attachment::historyLine(a);
                        if (resultIndex >= 0)
                            run.href = "attach:" +
                                       std::to_string(resultIndex) + ":" +
                                       std::to_string(ai);
                        run.style.color = sg::Color(140, 143, 150, 255);
                        meta.runs.push_back(std::move(run));
                        card.children.push_back(std::move(meta));
                    }
                }

                out.push_back(std::move(card));
            }
        }
    }

    return out;
}

} //namespace sdlagent::ui
