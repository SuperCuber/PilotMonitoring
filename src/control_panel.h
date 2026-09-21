#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "XPLMDisplay.h"

class ExecutionEngine;

class ControlPanel {
public:
    struct Sources {
        std::function<bool()> is_listening;
        std::function<std::string()> listening_status;
        std::function<const ExecutionEngine*()> execution_engine;
        std::function<std::string()> engine_status;
        std::function<std::string()> lua_file;
        std::function<std::vector<std::string>()> log_lines;
        std::function<void(std::string)> submit_transcript;
        std::function<std::vector<std::pair<std::string, std::string>>()> input_devices;
        std::function<std::string()> selected_input_device_id;
        std::function<bool()> input_device_disconnected;
        std::function<void()> refresh_input_devices;
        std::function<void(std::string)> select_input_device;
    };

    explicit ControlPanel(Sources sources);
    ~ControlPanel();

    ControlPanel(const ControlPanel&) = delete;
    ControlPanel& operator=(const ControlPanel&) = delete;

    void open();

private:
    enum class Tab { Status, Commands, Grammar, Settings };

    static void draw_callback(XPLMWindowID window, void* refcon);
    static int mouse_callback(XPLMWindowID window, int x, int y,
                              XPLMMouseStatus status, void* refcon);
    static XPLMCursorStatus cursor_callback(XPLMWindowID window, int x, int y, void* refcon);
    static int wheel_callback(XPLMWindowID window, int x, int y, int wheel,
                              int clicks, void* refcon);
    static void key_callback(XPLMWindowID window, char key, XPLMKeyFlags flags,
                             char virtual_key, void* refcon, int losing_focus);

    void draw();
    int handle_mouse(int x, int y, XPLMMouseStatus status);
    int handle_wheel(int x, int y, int clicks);
    void draw_status(int left, int top, int right, int bottom);
    void draw_commands(int left, int top, int right, int bottom);
    void draw_grammar(int left, int top, int right, int bottom);
    void draw_settings(int left, int top, int right, int bottom);
    void handle_key(char key, char virtual_key, XPLMKeyFlags flags, int losing_focus);

    Sources sources_;
    XPLMWindowID window_ = nullptr;
    Tab tab_ = Tab::Status;
    int log_scroll_ = 0;
    int command_scroll_ = 0;
    int grammar_scroll_ = 0;
    int selected_command_ = 0;
    bool text_input_active_ = false;
    std::string typed_command_;
    bool settings_device_menu_open_ = false;
};
