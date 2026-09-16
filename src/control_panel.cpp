#include "control_panel.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include <GL/gl.h>

#include "XPLMGraphics.h"
#include "execution_engine.h"

namespace {

constexpr int kMargin = 16;
constexpr int kLineHeight = 16;
constexpr int kTabHeight = 32;
constexpr int kTabWidth = 104;

constexpr float kPanelColor[] = {0.08F, 0.09F, 0.10F};
constexpr float kInactiveTabColor[] = {0.21F, 0.24F, 0.27F};
constexpr float kActiveTabColor[] = {0.42F, 0.45F, 0.48F};
constexpr float kTabBorderColor[] = {0.11F, 0.13F, 0.15F};

void fill_rect(int left, int top, int right, int bottom, const float* color) {
    glColor3fv(color);
    glBegin(GL_QUADS);
    glVertex2i(left, top);
    glVertex2i(right, top);
    glVertex2i(right, bottom);
    glVertex2i(left, bottom);
    glEnd();
}

void draw_tab(int left, int top, bool active) {
    fill_rect(left, top, left + kTabWidth, top - kTabHeight, kTabBorderColor);
    fill_rect(left + 1, top - 1, left + kTabWidth - 1, top - kTabHeight + 1,
              active ? kActiveTabColor : kInactiveTabColor);
}

void draw_text(float* color, int x, int y, const std::string& text) {
    XPLMDrawString(color, x, y, const_cast<char*>(text.c_str()), nullptr, xplmFont_Proportional);
}

void draw_lines(float* color, int x, int& y, int bottom, const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (y >= bottom && std::getline(stream, line)) {
        draw_text(color, x, y, line);
        y -= kLineHeight;
    }
}

std::string yield_description(const ActiveCoroutineInfo& active) {
    switch (active.reason) {
    case ActiveCoroutineInfo::YieldReason::WaitMs:
        return "Yielded: wait_ms (" + std::to_string(static_cast<int>(active.remaining_ms + 0.5)) + " ms remaining)";
    case ActiveCoroutineInfo::YieldReason::WaitUntil:
        return "Yielded: wait_until (waiting for predicate)";
    case ActiveCoroutineInfo::YieldReason::WaitPhrase:
        return "Yielded: wait_for_phrase (waiting for recognized phrase)";
    }
    return "Yielded";
}

} // namespace

ControlPanel::ControlPanel(Sources sources) : sources_(std::move(sources)) {
    int screen_left = 0;
    int screen_top = 0;
    int screen_right = 0;
    int screen_bottom = 0;
    XPLMGetScreenBoundsGlobal(&screen_left, &screen_top, &screen_right, &screen_bottom);

    XPLMCreateWindow_t params{};
    params.structSize = sizeof(params);
    params.left = screen_left + 60;
    params.top = screen_top - 60;
    params.right = params.left + 760;
    params.bottom = params.top - 520;
    params.visible = 0;
    params.drawWindowFunc = draw_callback;
    params.handleMouseClickFunc = mouse_callback;
    params.handleKeyFunc = key_callback;
    params.handleCursorFunc = cursor_callback;
    params.handleMouseWheelFunc = wheel_callback;
    params.refcon = this;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer = xplm_WindowLayerFloatingWindows;
    params.handleRightClickFunc = mouse_callback;
    window_ = XPLMCreateWindowEx(&params);
}

ControlPanel::~ControlPanel() {
    if (window_) XPLMDestroyWindow(window_);
}

void ControlPanel::open() {
    if (!window_) return;
    XPLMSetWindowIsVisible(window_, 1);
    XPLMBringWindowToFront(window_);
}

void ControlPanel::draw_callback(XPLMWindowID, void* refcon) {
    static_cast<ControlPanel*>(refcon)->draw();
}

int ControlPanel::mouse_callback(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon) {
    return static_cast<ControlPanel*>(refcon)->handle_mouse(x, y, status);
}

XPLMCursorStatus ControlPanel::cursor_callback(XPLMWindowID, int, int, void*) {
    return xplm_CursorDefault;
}

int ControlPanel::wheel_callback(XPLMWindowID, int x, int y, int, int clicks, void* refcon) {
    return static_cast<ControlPanel*>(refcon)->handle_wheel(x, y, clicks);
}

void ControlPanel::key_callback(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {
}

void ControlPanel::draw() {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);

    float title[] = {0.85F, 0.92F, 1.0F};
    float muted[] = {0.64F, 0.70F, 0.76F};

    const char* labels[] = {"Status", "Commands", "Grammar", "Settings"};
    for (int index = 0; index < 4; ++index) {
        const int x = left + kMargin + index * kTabWidth;
        const bool selected = static_cast<int>(tab_) == index;
        draw_tab(x, top - 8, selected);
        draw_text(selected ? title : muted, x + 12, top - 29, labels[index]);
    }

    const int content_top = top - kTabHeight - 16;
    if (tab_ == Tab::Status) draw_status(left + kMargin, content_top, right - kMargin, bottom + kMargin);
    else if (tab_ == Tab::Commands) draw_commands(left + kMargin, content_top, right - kMargin, bottom + kMargin);
    else if (tab_ == Tab::Grammar) draw_grammar(left + kMargin, content_top, right - kMargin, bottom + kMargin);
    else draw_settings(left + kMargin, content_top, right - kMargin, bottom + kMargin);
}

void ControlPanel::draw_status(int left, int top, int right, int bottom) {
    float normal[] = {0.92F, 0.92F, 0.92F};
    float muted[] = {0.64F, 0.70F, 0.76F};
    const int panel_left = left;
    const int panel_right = right;
    const int log_top = bottom + 180;
    fill_rect(panel_left, top + 8, panel_right, log_top + 6, kPanelColor);
    fill_rect(panel_left, log_top, panel_right, bottom, kPanelColor);

    int y = top - 10;
    const int text_left = left + 8;
    draw_text(normal, text_left, y, std::string("Listening: ") + (sources_.is_listening() ? "yes" : "no"));
    y -= kLineHeight;
    draw_text(normal, text_left, y, "Command engine: " + sources_.engine_status());
    y -= kLineHeight;
    const ExecutionEngine* engine = sources_.execution_engine();
    if (engine) {
        if (const auto active = engine->active_coroutine()) {
            draw_text(normal, text_left, y, "Active coroutine: " + active->command_id);
            y -= kLineHeight;
            draw_text(normal, text_left, y, yield_description(*active));
            y -= kLineHeight;
            if (!active->accepted_grammar.empty()) {
                draw_text(muted, text_left, y, "Accepted grammar:");
                y -= kLineHeight;
                draw_lines(muted, text_left + 12, y, log_top + 12, active->accepted_grammar);
            }
        } else {
            draw_text(normal, text_left, y, "Active coroutine: none");
            y -= kLineHeight;
        }
    }

    const auto logs = sources_.log_lines();
    const int visible = (std::max)(1, (log_top - bottom - 8) / kLineHeight);
    const int max_scroll = (std::max)(0, static_cast<int>(logs.size()) - visible);
    log_scroll_ = std::clamp(log_scroll_, 0, max_scroll);
    const int start = (std::max)(0, static_cast<int>(logs.size()) - visible - log_scroll_);
    int log_y = log_top - 12;
    for (int index = start; index < static_cast<int>(logs.size()) && log_y >= bottom + 4; ++index) {
        draw_text(normal, text_left, log_y, logs[index]);
        log_y -= kLineHeight;
    }
}

void ControlPanel::draw_commands(int left, int top, int right, int bottom) {
    float normal[] = {0.92F, 0.92F, 0.92F};
    float selected[] = {0.85F, 0.92F, 1.0F};
    float muted[] = {0.64F, 0.70F, 0.76F};
    float yielded[] = {1.0F, 0.72F, 0.25F};
    const ExecutionEngine* engine = sources_.execution_engine();
    if (!engine) {
        draw_text(muted, left, top, "No aircraft command file is currently loaded.");
        return;
    }
    const auto commands = engine->commands();
    if (commands.empty()) {
        draw_text(muted, left, top, "The loaded command file registered no commands.");
        return;
    }
    selected_command_ = std::clamp(selected_command_, 0, static_cast<int>(commands.size()) - 1);
    const int list_right = left + (std::max)(180, (right - left) / 3);
    fill_rect(left, top + 8, list_right, bottom, kPanelColor);
    fill_rect(list_right + 6, top + 8, right, bottom, kPanelColor);
    const int visible = (std::max)(1, (top - bottom - 8) / kLineHeight);
    const int max_scroll = (std::max)(0, static_cast<int>(commands.size()) - visible);
    command_scroll_ = std::clamp(command_scroll_, 0, max_scroll);
    const int start = command_scroll_;
    const auto active = engine->active_coroutine();
    int y = top - kLineHeight;
    for (int index = start; index < static_cast<int>(commands.size()) && y >= bottom + 4; ++index) {
        const bool is_yielded = active && active->command_id == commands[index].id;
        draw_text(is_yielded ? yielded : (index == selected_command_ ? selected : normal),
                  left + 16, y, commands[index].id);
        y -= kLineHeight;
    }

    const auto& command = commands[selected_command_];
    int detail_y = top;
    draw_text(selected, list_right + 24, detail_y, "Command: " + command.id);
    detail_y -= 2 * kLineHeight;
    draw_text(muted, list_right + 24, detail_y, "Triggers:");
    detail_y -= kLineHeight;
    for (const auto& trigger : command.triggers) {
        draw_text(normal, list_right + 36, detail_y, trigger);
        detail_y -= kLineHeight;
    }
    if (active && active->command_id == command.id &&
        active->reason == ActiveCoroutineInfo::YieldReason::WaitPhrase) {
        detail_y -= kLineHeight;
        draw_text(yielded, list_right + 24, detail_y, "Active grammar (currently accepted):");
        detail_y -= kLineHeight;
        draw_lines(normal, list_right + 36, detail_y, bottom + 4, active->accepted_grammar);
    }
}

void ControlPanel::draw_grammar(int left, int top, int right, int bottom) {
    float normal[] = {0.92F, 0.92F, 0.92F};
    float muted[] = {0.64F, 0.70F, 0.76F};
    const ExecutionEngine* engine = sources_.execution_engine();
    if (!engine) {
        draw_text(muted, left, top, "No aircraft command file is currently loaded.");
        return;
    }
    fill_rect(left, top + 8, right, bottom, kPanelColor);
    draw_text(muted, left + 8, top - 10, "Top-level grammar for all registered commands:");
    const std::string& grammar = engine->top_level_grammar_text();
    std::istringstream stream(grammar);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) lines.push_back(std::move(line));
    const int visible = (std::max)(1, (top - bottom - 2 * kLineHeight) / kLineHeight);
    const int max_scroll = (std::max)(0, static_cast<int>(lines.size()) - visible);
    grammar_scroll_ = std::clamp(grammar_scroll_, 0, max_scroll);
    int y = top - 2 * kLineHeight - 2;
    for (int index = grammar_scroll_; index < static_cast<int>(lines.size()) && y >= bottom + 4; ++index) {
        draw_text(normal, left + 20, y, lines[index]);
        y -= kLineHeight;
    }
}

void ControlPanel::draw_settings(int left, int top, int right, int bottom) {
    float muted[] = {0.64F, 0.70F, 0.76F};
    fill_rect(left, top + 8, right, bottom, kPanelColor);
    draw_text(muted, left + 8, top - 10, "Settings are not available yet.");
}

int ControlPanel::handle_mouse(int x, int y, XPLMMouseStatus status) {
    if (status != xplm_MouseDown) return 1;
    int left = 0;
    int top = 0;
    int right = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, nullptr);
    const int tab_index = (x - (left + kMargin)) / kTabWidth;
    if (y >= top - 40 && y <= top - 8 && tab_index >= 0 && tab_index < 4) {
        tab_ = static_cast<Tab>(tab_index);
        return 1;
    }
    if (tab_ == Tab::Commands && y < top - 48) {
        const int list_right = left + kMargin + (std::max)(180, (right - left - 2 * kMargin) / 3);
        if (x <= list_right) {
            const int index = command_scroll_ + (top - 48 - y) / kLineHeight;
            if (const auto* engine = sources_.execution_engine()) {
                const auto commands = engine->commands();
                if (index >= 0 && index < static_cast<int>(commands.size())) selected_command_ = index;
            }
        }
    }
    return 1;
}

int ControlPanel::handle_wheel(int, int, int clicks) {
    if (tab_ == Tab::Status) log_scroll_ = (std::max)(0, log_scroll_ + clicks);
    if (tab_ == Tab::Commands) command_scroll_ = (std::max)(0, command_scroll_ - clicks);
    if (tab_ == Tab::Grammar) grammar_scroll_ = (std::max)(0, grammar_scroll_ - clicks);
    return 1;
}
