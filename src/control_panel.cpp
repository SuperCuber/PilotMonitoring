#include "control_panel.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include <GL/gl.h>

#include "XPLMGraphics.h"
#include "XPLMDefs.h"
#include "execution_engine.h"

namespace {

constexpr int kMargin = 16;
constexpr int kLineHeight = 16;
constexpr int kManualInputBaselineOffset = 5 * kLineHeight + 10;
constexpr int kTabHeight = 32;
constexpr int kTabWidth = 104;

constexpr float kPanelColor[] = {0.08F, 0.09F, 0.10F};
constexpr float kInactiveTabColor[] = {0.21F, 0.24F, 0.27F};
constexpr float kActiveTabColor[] = {0.42F, 0.45F, 0.48F};
constexpr float kTabBorderColor[] = {0.11F, 0.13F, 0.15F};
constexpr float kStatusGreen[] = {0.38F, 0.90F, 0.52F};
constexpr float kStatusAmber[] = {1.0F, 0.72F, 0.25F};
constexpr float kStatusRed[] = {1.0F, 0.30F, 0.30F};
constexpr float kDropdownColor[] = {0.16F, 0.18F, 0.20F};
constexpr float kDropdownOpenColor[] = {0.24F, 0.27F, 0.30F};

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

void draw_text(const float* color, int x, int y, const std::string& text) {
    XPLMDrawString(const_cast<float*>(color), x, y, const_cast<char*>(text.c_str()), nullptr, xplmFont_Basic);
}

void draw_status_field(
    int x,
    int y,
    const std::string& key,
    const std::string& value,
    const float* value_color) {
    float key_color[] = {0.92F, 0.92F, 0.92F};
    int character_width = 0;
    XPLMGetFontDimensions(xplmFont_Basic, &character_width, nullptr, nullptr);
    draw_text(key_color, x, y, key);
    draw_text(value_color, x + static_cast<int>(key.size() + 1) * character_width, y, value);
}

void draw_lines(const float* color, int x, int& y, int bottom, const std::string& text) {
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
        return "wait_ms (" + std::to_string(static_cast<int>(active.remaining_ms + 0.5)) + " ms remaining)";
    case ActiveCoroutineInfo::YieldReason::WaitUntil:
        return "wait_until";
    case ActiveCoroutineInfo::YieldReason::WaitPhrase:
        return "wait_for_phrase";
    }
    return "no";
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

void ControlPanel::key_callback(XPLMWindowID, char key, XPLMKeyFlags flags, char virtual_key, void* refcon, int losing_focus) {
    static_cast<ControlPanel*>(refcon)->handle_key(key, virtual_key, flags, losing_focus);
}

void ControlPanel::draw() {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);

    float title[] = {0.85F, 0.92F, 1.0F};

    const char* labels[] = {"Status", "Commands", "Grammar", "Settings"};
    for (int index = 0; index < 4; ++index) {
        const int x = left + kMargin + index * kTabWidth;
        const bool selected = static_cast<int>(tab_) == index;
        draw_tab(x, top - 8, selected);
        std::string label(labels[index]);
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        draw_text(title, x + 12, top - 29, label);
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
    float cyan[] = {0.32F, 0.82F, 0.94F};
    const int panel_left = left;
    const int panel_right = right;
    const int log_top = bottom + 180;
    fill_rect(panel_left, top + 8, panel_right, log_top + 6, kPanelColor);
    fill_rect(panel_left, log_top, panel_right, bottom, kPanelColor);

    int y = top - 10;
    const int text_left = left + 8;
    draw_status_field(text_left, y, "ENGINE:", sources_.engine_status(), kStatusGreen);
    y -= kLineHeight;
    draw_status_field(text_left, y, "LUA FILE:", sources_.lua_file(), kStatusGreen);
    y -= kLineHeight;
    const bool listening = sources_.is_listening();
    const std::string listening_status = sources_.listening_status
        ? sources_.listening_status()
        : (listening ? "yes" : "no");
    const float* listening_color = listening_status == "device disconnected"
        ? kStatusRed
        : listening ? kStatusAmber : kStatusGreen;
    draw_status_field(text_left, y, "LISTENING:", listening_status, listening_color);
    y -= kLineHeight;
    const ExecutionEngine* engine = sources_.execution_engine();
    if (engine) {
        if (const auto active = engine->active_coroutine()) {
            draw_status_field(text_left, y, "ACTIVE COMMAND:", active->command_id, kStatusAmber);
            y -= kLineHeight;
            draw_status_field(text_left, y, "WAITING:", yield_description(*active), kStatusAmber);
        } else {
            draw_status_field(text_left, y, "ACTIVE COMMAND:", "none", kStatusGreen);
            y -= kLineHeight;
            draw_status_field(text_left, y, "WAITING:", "no", kStatusGreen);
        }
        y -= kLineHeight;

        float cyan[] = {0.32F, 0.82F, 0.94F};
        constexpr char kManualInputLabel[] = "MAN INPUT:";
        int character_width = 0;
        XPLMGetFontDimensions(xplmFont_Basic, &character_width, nullptr, nullptr);
        const int input_left = text_left + static_cast<int>(sizeof(kManualInputLabel)) * character_width;
        draw_text(cyan, text_left, y, kManualInputLabel);
        fill_rect(input_left, y + 10, right - 8, y - 5,
                  text_input_active_ ? kActiveTabColor : kInactiveTabColor);
        draw_text(normal, input_left + 6, y,
                  typed_command_.empty()
                      ? (text_input_active_ ? "Enter to submit" : "click here")
                      : typed_command_);
        y -= kLineHeight;

        const auto triggers = engine->expected_triggers();
        draw_text(cyan, text_left, y, "TRIGGERS:");
        y -= kLineHeight;
        if (triggers.empty()) {
            const std::string message = engine->active_coroutine()
                ? "none while the active coroutine waits"
                : "none";
            draw_text(normal, text_left + 12, y, message);
            y -= kLineHeight;
        } else {
            constexpr std::size_t kStatusTriggerLimit = 6;
            const std::size_t count = (std::min)(triggers.size(), kStatusTriggerLimit);
            for (std::size_t index = 0; index < count; ++index) {
                draw_text(normal, text_left + 12, y, triggers[index]);
                y -= kLineHeight;
            }
            if (triggers.size() > count) {
                draw_text(muted, text_left + 12, y,
                          "... " + std::to_string(triggers.size() - count) + " more; see Commands");
                y -= kLineHeight;
            }
        }
    } else {
        draw_status_field(text_left, y, "ACTIVE COMMAND:", "none", kStatusGreen);
        y -= kLineHeight;
        draw_status_field(text_left, y, "WAITING:", "no", kStatusGreen);
    }

    const auto logs = sources_.log_lines();
    const int visible = (std::max)(1, (log_top - bottom - 8) / kLineHeight - 2);
    const int max_scroll = (std::max)(0, static_cast<int>(logs.size()) - visible);
    log_scroll_ = std::clamp(log_scroll_, 0, max_scroll);
    const int start = (std::max)(0, static_cast<int>(logs.size()) - visible - log_scroll_);
    int log_y = log_top - 12;
    if (log_scroll_ < max_scroll) {
        draw_text(kStatusGreen, text_left, log_y, "^^^");
    }
    log_y -= kLineHeight;
    for (int index = start, drawn = 0;
         index < static_cast<int>(logs.size()) && drawn < visible;
         ++index, ++drawn) {
        draw_text(normal, text_left, log_y, logs[index]);
        log_y -= kLineHeight;
    }
    if (log_scroll_ > 0) draw_text(kStatusGreen, text_left, bottom + 4, "VVV");
}

void ControlPanel::draw_commands(int left, int top, int right, int bottom) {
    float normal[] = {0.92F, 0.92F, 0.92F};
    float muted[] = {0.64F, 0.70F, 0.76F};
    float amber[] = {1.0F, 0.72F, 0.25F};
    float green[] = {0.38F, 0.90F, 0.52F};
    float cyan[] = {0.32F, 0.82F, 0.94F};
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
    const int visible = (std::max)(1, (top - bottom - 8) / kLineHeight - 2);
    const int max_scroll = (std::max)(0, static_cast<int>(commands.size()) - visible);
    command_scroll_ = std::clamp(command_scroll_, 0, max_scroll);
    const int start = command_scroll_;
    const auto active = engine->active_coroutine();
    int y = top - kLineHeight;
    if (command_scroll_ > 0) {
        draw_text(green, left + 12, y, "^^^");
    }
    y -= kLineHeight;
    for (int index = start, drawn = 0;
         index < static_cast<int>(commands.size()) && drawn < visible;
         ++index, ++drawn) {
        const bool is_yielded = active && active->command_id == commands[index].id;
        draw_text(green, left + 12, y, index == selected_command_ ? ">" : " ");
        draw_text(is_yielded ? amber : normal, left + 24, y, commands[index].id);
        y -= kLineHeight;
    }
    if (command_scroll_ < max_scroll) draw_text(green, left + 12, bottom + 4, "VVV");

    const auto& command = commands[selected_command_];
    int detail_y = top - 10;
    draw_text(cyan, list_right + 24, detail_y, "TRIGGERS:");
    detail_y -= kLineHeight;
    for (const auto& trigger : command.triggers) {
        draw_text(normal, list_right + 36, detail_y, trigger);
        detail_y -= kLineHeight;
    }
    if (active && active->command_id == command.id &&
        active->reason == ActiveCoroutineInfo::YieldReason::WaitPhrase) {
        detail_y -= kLineHeight;
        draw_text(amber, list_right + 24, detail_y, "ACTIVE GRAMMAR:");
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
    const std::string& grammar = engine->top_level_grammar_text();
    std::istringstream stream(grammar);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) lines.push_back(std::move(line));
    const int visible = (std::max)(1, (top - bottom - kLineHeight) / kLineHeight - 2);
    const int max_scroll = (std::max)(0, static_cast<int>(lines.size()) - visible);
    grammar_scroll_ = std::clamp(grammar_scroll_, 0, max_scroll);
    int y = top - 10;
    if (grammar_scroll_ > 0) {
        draw_text(kStatusGreen, left + 20, y, "^^^");
    }
    y -= kLineHeight;
    for (int index = grammar_scroll_, drawn = 0;
         index < static_cast<int>(lines.size()) && drawn < visible;
         ++index, ++drawn) {
        draw_text(normal, left + 20, y, lines[index]);
        y -= kLineHeight;
    }
    if (grammar_scroll_ < max_scroll) draw_text(kStatusGreen, left + 20, bottom + 4, "VVV");
}

void ControlPanel::draw_settings(int left, int top, int right, int bottom) {
    float normal[] = {0.92F, 0.92F, 0.92F};
    float cyan[] = {0.32F, 0.82F, 0.94F};
    fill_rect(left, top + 8, right, bottom, kPanelColor);
    draw_text(cyan, left + 8, top - 18, "INPUT DEVICE:");

    std::vector<std::pair<std::string, std::string>> options{{"", "System default"}};
    if (sources_.input_devices) {
        for (const auto& device : sources_.input_devices()) {
            options.emplace_back(device.first, device.second);
        }
    }
    const std::string selected_id = sources_.selected_input_device_id
        ? sources_.selected_input_device_id() : std::string{};
    const bool disconnected = sources_.input_device_disconnected &&
                              sources_.input_device_disconnected();
    std::string selected_name = "System default";
    for (const auto& option : options) {
        if (option.first == selected_id) {
            selected_name = option.second;
            break;
        }
    }
    if (disconnected && !selected_id.empty()) {
        selected_name = "Device disconnected";
        bool already_present = false;
        for (const auto& option : options) already_present |= option.first == selected_id;
        if (!already_present) options.emplace_back(selected_id, selected_name);
    }

    const int row_baseline = top - 18;
    const int dropdown_left = left + 120;
    const int dropdown_right = left + 480;
    const int dropdown_top = row_baseline + 10;
    const int dropdown_bottom = row_baseline - 6;
    fill_rect(dropdown_left, dropdown_top, dropdown_right, dropdown_bottom,
              settings_device_menu_open_ ? kDropdownOpenColor : kDropdownColor);
    draw_text(disconnected ? kStatusRed : normal, dropdown_left + 8, row_baseline, selected_name);

    int character_width = 0;
    XPLMGetFontDimensions(xplmFont_Basic, &character_width, nullptr, nullptr);
    constexpr char kRefreshLabel[] = "REFRESH";
    constexpr int kRefreshPadding = 12;
    const int refresh_left = dropdown_right + 12;
    const int refresh_right = refresh_left +
                              static_cast<int>(sizeof(kRefreshLabel) - 1) * character_width +
                              2 * kRefreshPadding;
    fill_rect(refresh_left, dropdown_top, refresh_right, dropdown_bottom, kInactiveTabColor);
    draw_text(normal, refresh_left + kRefreshPadding, row_baseline, kRefreshLabel);

    constexpr char kReloadLuaLabel[] = "RELOAD LUA FILE";
    constexpr int kReloadLuaPadding = 12;
    const int reload_lua_left = left + 8;
    const int reload_lua_baseline = row_baseline - 40;
    const int reload_lua_top = reload_lua_baseline + 10;
    const int reload_lua_bottom = reload_lua_baseline - 6;
    const int reload_lua_right = reload_lua_left +
                                 static_cast<int>(sizeof(kReloadLuaLabel) - 1) * character_width +
                                 2 * kReloadLuaPadding;
    fill_rect(reload_lua_left, reload_lua_top, reload_lua_right, reload_lua_bottom,
              kInactiveTabColor);
    draw_text(normal, reload_lua_left + kReloadLuaPadding, reload_lua_baseline, kReloadLuaLabel);

    if (settings_device_menu_open_) {
        const int option_height = 22;
        int option_top = dropdown_bottom - 2;
        for (const auto& option : options) {
            const int option_bottom = option_top - option_height;
            fill_rect(dropdown_left, option_top, dropdown_right, option_bottom, kDropdownColor);
            const bool option_is_disconnected = disconnected && option.first == selected_id;
            draw_text(option_is_disconnected ? kStatusRed : option.first == selected_id ? cyan : normal,
                      dropdown_left + 8, option_top - 16, option.second);
            option_top = option_bottom;
        }
    }

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
        text_input_active_ = false;
        XPLMTakeKeyboardFocus(nullptr);
        return 1;
    }
    if (tab_ == Tab::Status) {
        const int content_top = top - kTabHeight - 16;
        constexpr char kManualInputLabel[] = "MAN INPUT:";
        int character_width = 0;
        XPLMGetFontDimensions(xplmFont_Basic, &character_width, nullptr, nullptr);
        const int input_left = left + kMargin + 8 +
                               static_cast<int>(sizeof(kManualInputLabel)) * character_width;
        const int input_baseline = content_top - kManualInputBaselineOffset;
        const int input_top = input_baseline + 10;
        const int input_bottom = input_baseline - 5;
        if (x >= input_left && x <= right - kMargin - 8 &&
            y >= input_bottom && y <= input_top) {
            text_input_active_ = true;
            XPLMBringWindowToFront(window_);
            XPLMTakeKeyboardFocus(window_);
            return 1;
        }
        text_input_active_ = false;
        XPLMTakeKeyboardFocus(nullptr);
    }
    if (tab_ == Tab::Commands && y < top - 48) {
        const int list_right = left + kMargin + (std::max)(180, (right - left - 2 * kMargin) / 3);
        if (x <= list_right) {
            const int index = command_scroll_ + (top - 48 - y) / kLineHeight - 1;
            if (const auto* engine = sources_.execution_engine()) {
                const auto commands = engine->commands();
                if (index >= 0 && index < static_cast<int>(commands.size())) selected_command_ = index;
            }
        }
    }
    if (tab_ == Tab::Settings) {
        const int content_top = top - kTabHeight - 16;
        const int row_baseline = content_top - 18;
        const int dropdown_left = left + kMargin + 120;
        const int dropdown_right = left + kMargin + 480;
        const int dropdown_top = row_baseline + 10;
        const int dropdown_bottom = row_baseline - 6;
        int character_width = 0;
        XPLMGetFontDimensions(xplmFont_Basic, &character_width, nullptr, nullptr);
        constexpr int kRefreshPadding = 12;
        const int refresh_left = dropdown_right + 12;
        const int refresh_right = refresh_left + 7 * character_width + 2 * kRefreshPadding;
        constexpr char kReloadLuaLabel[] = "RELOAD LUA FILE";
        constexpr int kReloadLuaPadding = 12;
        const int reload_lua_left = left + kMargin + 8;
        const int reload_lua_baseline = row_baseline - 40;
        const int reload_lua_top = reload_lua_baseline + 10;
        const int reload_lua_bottom = reload_lua_baseline - 6;
        const int reload_lua_right = reload_lua_left +
                                     static_cast<int>(sizeof(kReloadLuaLabel) - 1) * character_width +
                                     2 * kReloadLuaPadding;

        if (x >= reload_lua_left && x <= reload_lua_right &&
            y <= reload_lua_top && y >= reload_lua_bottom) {
            if (sources_.reload_lua_file) sources_.reload_lua_file();
            settings_device_menu_open_ = false;
            return 1;
        }

        std::vector<std::pair<std::string, std::string>> options{{"", "System default"}};
        if (sources_.input_devices) {
            for (const auto& device : sources_.input_devices()) {
                options.emplace_back(device.first, device.second);
            }
        }
        const std::string selected_id = sources_.selected_input_device_id
            ? sources_.selected_input_device_id() : std::string{};
        const bool disconnected = sources_.input_device_disconnected &&
                                  sources_.input_device_disconnected();
        if (disconnected && !selected_id.empty()) {
            bool already_present = false;
            for (const auto& option : options) already_present |= option.first == selected_id;
            if (!already_present) options.emplace_back(selected_id, "Device disconnected");
        }

        if (x >= refresh_left && x <= refresh_right &&
            y <= dropdown_top && y >= dropdown_bottom) {
            if (sources_.refresh_input_devices) sources_.refresh_input_devices();
            settings_device_menu_open_ = false;
            return 1;
        }
        if (settings_device_menu_open_) {
            const int option_height = 22;
            int option_top = dropdown_bottom - 2;
            for (const auto& option : options) {
                const int option_bottom = option_top - option_height;
                if (x >= dropdown_left && x <= dropdown_right &&
                    y <= option_top && y >= option_bottom) {
                    const bool is_disconnected_state = disconnected && option.first == selected_id;
                    if (!is_disconnected_state && sources_.select_input_device) {
                        sources_.select_input_device(option.first);
                    }
                    settings_device_menu_open_ = false;
                    return 1;
                }
                option_top = option_bottom;
            }
        }
        if (x >= dropdown_left && x <= dropdown_right &&
            y <= dropdown_top && y >= dropdown_bottom) {
            settings_device_menu_open_ = !settings_device_menu_open_;
            return 1;
        }
        settings_device_menu_open_ = false;
    }
    return 1;
}

void ControlPanel::handle_key(char key, char virtual_key, XPLMKeyFlags flags, int losing_focus) {
    if (losing_focus) {
        text_input_active_ = false;
        return;
    }
    if (!text_input_active_) return;
    if ((flags & xplm_UpFlag) != 0) return;
    const unsigned char virtual_code = static_cast<unsigned char>(virtual_key);
    if (virtual_code == XPLM_VK_RETURN || key == '\r' || key == '\n') {
        if (!typed_command_.empty()) {
            sources_.submit_transcript(std::move(typed_command_));
            typed_command_.clear();
        }
        return;
    }
    if (virtual_code == XPLM_VK_BACK || key == '\b') {
        if (!typed_command_.empty()) typed_command_.pop_back();
        return;
    }
    if (virtual_code == XPLM_VK_ESCAPE) {
        typed_command_.clear();
        text_input_active_ = false;
        XPLMTakeKeyboardFocus(nullptr);
        return;
    }
    if (std::isprint(static_cast<unsigned char>(key)) && typed_command_.size() < 240) {
        typed_command_.push_back(key);
    }
}

int ControlPanel::handle_wheel(int x, int y, int clicks) {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    const int content_left = left + kMargin;
    const int content_right = right - kMargin;
    const int content_top = top - kTabHeight - 16;
    const int content_bottom = bottom + kMargin;
    auto move_scroll = [clicks](int& scroll, int maximum, int delta) {
        const int previous = scroll;
        scroll = std::clamp(scroll + delta * clicks, 0, maximum);
        return scroll != previous;
    };

    if (tab_ == Tab::Status) {
        const int log_top = content_bottom + 180;
        if (x < content_left || x > content_right || y < content_bottom || y > log_top) return 0;
        const auto logs = sources_.log_lines();
        const int visible = (std::max)(1, (log_top - content_bottom - 8) / kLineHeight - 2);
        const int maximum = (std::max)(0, static_cast<int>(logs.size()) - visible);
        return move_scroll(log_scroll_, maximum, 1) ? 1 : 0;
    }

    if (tab_ == Tab::Commands) {
        const int list_right = content_left + (std::max)(180, (content_right - content_left) / 3);
        if (x < content_left || x > list_right || y < content_bottom || y > content_top + 8) return 0;
        const auto* engine = sources_.execution_engine();
        if (!engine) return 0;
        const int visible = (std::max)(1, (content_top - content_bottom - 8) / kLineHeight - 2);
        const int maximum = (std::max)(0, static_cast<int>(engine->commands().size()) - visible);
        return move_scroll(command_scroll_, maximum, -1) ? 1 : 0;
    }

    if (tab_ == Tab::Grammar) {
        if (x < content_left || x > content_right || y < content_bottom || y > content_top + 8) return 0;
        const auto* engine = sources_.execution_engine();
        if (!engine) return 0;
        const int lines = static_cast<int>(std::count(
            engine->top_level_grammar_text().begin(), engine->top_level_grammar_text().end(), '\n')) + 1;
        const int visible = (std::max)(1, (content_top - content_bottom - kLineHeight) / kLineHeight - 2);
        const int maximum = (std::max)(0, lines - visible);
        return move_scroll(grammar_scroll_, maximum, -1) ? 1 : 0;
    }

    return 0;
}
