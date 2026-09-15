#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"
#include "whisper.h"

#include "command_processor.h"
#include "voice_service.h"

namespace {

constexpr char kPluginName[] = "Pilot Monitoring";
constexpr char kPluginSignature[] = "com.example.pilotmonitoring";
constexpr char kPluginDescription[] = "Voice-command plugin scaffold powered by whisper.cpp.";
constexpr char kPttCommandName[] = "pilotmonitoring/ptt";
char kMenuActionReloadPlugins[] = "reload_plugins";

XPLMCommandRef g_ptt_command = nullptr;
XPLMMenuID g_menu = nullptr;
std::unique_ptr<VoiceService> g_voice_service;
std::unique_ptr<CommandProcessor> g_command_processor;
bool g_flight_loop_registered = false;

void log_message(const char* message) {
    XPLMDebugString(message);
}

void log_line(std::string_view line) {
    std::string text(line);
    text.push_back('\n');
    log_message(text.c_str());
}

int handle_ptt_command(XPLMCommandRef, XPLMCommandPhase phase, void*) {
    if (!g_voice_service) {
        return 1;
    }

    if (phase == xplm_CommandBegin) {
        log_message("Pilot Monitoring: PTT pressed; listening.\n");
        g_voice_service->set_listening(true);
    } else if (phase == xplm_CommandEnd) {
        log_message("Pilot Monitoring: PTT released; transcribing.\n");
        g_voice_service->set_listening(false);
    }
    return 1;
}

std::filesystem::path plugin_resources_path() {
    char plugin_path[512]{};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, plugin_path, nullptr, nullptr);
    return std::filesystem::path(plugin_path).parent_path().parent_path() / "resources";
}

std::filesystem::path model_path() {
    return plugin_resources_path() / "models" / "ggml-base.en.bin";
}

std::filesystem::path commands_path() {
    return plugin_resources_path() / "commands.json";
}

void log_grammar(std::string_view grammar_text) {
    log_message("Pilot Monitoring: generated grammar:\n");

    std::istringstream stream{std::string(grammar_text)};
    std::string line;
    while (std::getline(stream, line)) {
        log_line(line);
    }
}

void execute_command(const MatchedCommand& command) {
    for (const auto& action : command.actions) {
        if (action.type == ResolvedAction::Type::CommandOnce) {
            const XPLMCommandRef command_ref = XPLMFindCommand(action.command.c_str());
            if (command_ref == nullptr) {
                log_message("Pilot Monitoring: command_once target is unavailable.\n");
                continue;
            }

            XPLMCommandOnce(command_ref);
            continue;
        }

        const XPLMDataRef dataref = XPLMFindDataRef(action.dataref.c_str());
        if (dataref == nullptr) {
            log_message("Pilot Monitoring: dataref target is unavailable.\n");
            continue;
        }
        if (XPLMCanWriteDataRef(dataref) == 0) {
            log_message("Pilot Monitoring: dataref target is read-only.\n");
            continue;
        }

        if (action.value_type == "float") {
            XPLMSetDataf(dataref, action.float_value);
        } else if (action.value_type == "<integer>") {
            XPLMSetDatai(dataref, action.integer_value);
        } else {
            log_message("Pilot Monitoring: unsupported resolved value type.\n");
        }
    }
}

float process_voice_results(float, float, int, void*) {
    if (!g_voice_service) {
        return 0.25F;
    }

    while (const auto result = g_voice_service->pop_result()) {
        if (!result->recognized) {
            log_message("Pilot Monitoring: ");
            log_message(result->text.c_str());
            log_message("\n");
            continue;
        }

        log_message("Pilot Monitoring: transcript: \"");
        log_message(result->text.c_str());
        log_message("\"\n");

        if (!g_command_processor) {
            log_message("Pilot Monitoring: command processor is unavailable.\n");
            continue;
        }

        const auto matched_command = g_command_processor->match(result->text);
        if (!matched_command) {
            log_message("Pilot Monitoring: transcript did not match a configured command.\n");
            continue;
        }

        execute_command(*matched_command);
    }
    return 0.1F;
}

void handle_menu(void*, void* item_ref) {
    if (item_ref == kMenuActionReloadPlugins) {
        log_message("Pilot Monitoring: reloading all plug-ins.\n");
        XPLMReloadPlugins();
    }
}

} // namespace

PLUGIN_API int XPluginStart(char* out_name, char* out_signature, char* out_description) {
    std::strcpy(out_name, kPluginName);
    std::strcpy(out_signature, kPluginSignature);
    std::strcpy(out_description, kPluginDescription);

    g_ptt_command = XPLMCreateCommand(
        kPttCommandName,
        "Pilot Monitoring: hold to capture a voice command");
    XPLMRegisterCommandHandler(g_ptt_command, handle_ptt_command, 1, nullptr);

    const XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
    const int menu_item = XPLMAppendMenuItem(plugins_menu, "Pilot Monitoring", nullptr, 1);
    g_menu = XPLMCreateMenu("Pilot Monitoring", plugins_menu, menu_item, handle_menu, nullptr);
    XPLMAppendMenuItem(g_menu, "Reload all plug-ins", kMenuActionReloadPlugins, 1);

    log_message("Pilot Monitoring: plugin started.\n");
    log_message("Pilot Monitoring: whisper.cpp linked: ");
    log_message(whisper_print_system_info());
    log_message("\n");
    return 1;
}

PLUGIN_API void XPluginStop() {
    if (g_flight_loop_registered) {
        XPLMUnregisterFlightLoopCallback(process_voice_results, nullptr);
        g_flight_loop_registered = false;
    }
    if (g_voice_service) {
        g_voice_service->stop();
        g_voice_service.reset();
    }
    g_command_processor.reset();
    if (g_menu != nullptr) {
        XPLMDestroyMenu(g_menu);
        g_menu = nullptr;
    }
    if (g_ptt_command != nullptr) {
        XPLMUnregisterCommandHandler(g_ptt_command, handle_ptt_command, 1, nullptr);
        g_ptt_command = nullptr;
    }
    log_message("Pilot Monitoring: plugin stopped.\n");
}

PLUGIN_API int XPluginEnable() {
    std::string error_message;
    if (const auto processor = CommandProcessor::load_from_file(commands_path(), error_message)) {
        g_command_processor = std::make_unique<CommandProcessor>(*processor);
        log_grammar(g_command_processor->grammar_text());
    } else {
        log_message("Pilot Monitoring: failed to load commands.\n");
        log_line(error_message);
        g_command_processor.reset();
    }

    const grammar_parser::parse_state grammar =
        g_command_processor ? g_command_processor->grammar() : grammar_parser::parse_state{};
    g_voice_service = std::make_unique<VoiceService>(model_path().string(), grammar);
    g_voice_service->start();
    XPLMRegisterFlightLoopCallback(process_voice_results, 0.1F, nullptr);
    g_flight_loop_registered = true;
    log_message("Pilot Monitoring: plugin enabled.\n");
    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (g_flight_loop_registered) {
        XPLMUnregisterFlightLoopCallback(process_voice_results, nullptr);
        g_flight_loop_registered = false;
    }
    if (g_voice_service) {
        g_voice_service->stop();
        g_voice_service.reset();
    }
    g_command_processor.reset();
    log_message("Pilot Monitoring: plugin disabled.\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {
}
