#include "execution_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#include "lua.hpp"

struct Part {
    enum class Type { Literal, Integer, Float };
    Type type;
    std::string text;
    std::string name;
};

struct Handler {
    std::string id;
    std::vector<std::vector<Part>> phrases;
    int function_ref = LUA_NOREF;
};

struct ExecutionEngine::Impl {
    enum class Suspension { None, WaitMs, WaitUntil, WaitPhrase };

    struct ActiveCoroutine {
        lua_State* thread = nullptr;
        int thread_ref = LUA_NOREF;
        std::string command_id;
        Suspension suspension = Suspension::None;
        double remaining_ms = 0.0;
        int condition_ref = LUA_NOREF;
        std::vector<std::vector<Part>> phrases;
    };

    lua_State* lua = nullptr;
    std::vector<Handler> handlers;
    std::vector<Action> actions;
    std::unique_ptr<ActiveCoroutine> active;
    ExecutionEngine* owner = nullptr;
    ExecutionEngine::LogCallback logger;
};

namespace {

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool space = true;
    for (unsigned char character : text) {
        if (std::isspace(character)) {
            if (!space) out.push_back(' ');
            space = true;
        } else {
            out.push_back(static_cast<char>(std::tolower(character)));
            space = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string literal(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char character : text) {
        out.push_back(std::isspace(character) ? ' '
                                                : static_cast<char>(std::tolower(character)));
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string escape(std::string_view text) {
    std::string out;
    for (char character : text) {
        if (character == '\\') out += "\\\\";
        else if (character == '"') out += "\\\"";
        else if (character == '\n') out += "\\n";
        else if (character == '\r') out += "\\r";
        else if (character == '\t') out += "\\t";
        else out += character;
    }
    return out;
}

std::string grammar_for_phrases(
    const std::vector<std::vector<Part>>& phrases,
    bool allow_commands) {
    std::ostringstream output;
    output << (allow_commands
        ? "root ::= init command (\",\" init command)*\n"
        : "root ::= init command\n");
    output << "init ::= \" \"\n\ncommand ::= (\n";

    bool first_phrase = true;
    for (const auto& phrase : phrases) {
        output << (first_phrase ? "    " : "  | ");
        first_phrase = false;

        bool first_part = true;
        for (const auto& part : phrase) {
            if (!first_part) output << ' ';
            first_part = false;
            if (part.type == Part::Type::Literal) {
                output << '"' << escape(part.text) << '"';
            } else if (part.type == Part::Type::Integer) {
                output << "integer_slot";
            } else {
                output << "float_slot";
            }
        }
        output << '\n';
    }

    output << ")\n\n"
            << "integer_slot ::= digit+ | digit (\" \" digit)+\n"
            << "float_slot ::= digit+ (\".\" | \"decimal\") digit+\n"
            << "digit ::= [0-9]\n";
    return output.str();
}

std::string grammar_for(const std::vector<Handler>& handlers) {
    std::vector<std::vector<Part>> phrases;
    for (const auto& handler : handlers) {
        phrases.insert(phrases.end(), handler.phrases.begin(), handler.phrases.end());
    }
    return grammar_for_phrases(phrases, true);
}

std::string trigger_text(const std::vector<Part>& parts) {
    std::ostringstream output;
    for (const auto& part : parts) {
        if (part.type == Part::Type::Literal) {
            output << part.text;
        } else {
            output << '<' << part.name << ':'
                   << (part.type == Part::Type::Integer ? "integer" : "float") << '>';
        }
    }
    return output.str();
}

bool integer_text(std::string_view text, std::string& value) {
    if (text.empty()) return false;
    for (char character : text) {
        if (character != ' ' && !std::isdigit(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    if (text.front() == ' ' || text.back() == ' ') return false;

    value.clear();
    for (char character : text) {
        if (character != ' ') value.push_back(character);
    }
    return !value.empty();
}

bool float_text(std::string_view text, std::string& value) {
    const auto decimal = text.find("decimal");
    const auto dot = text.find('.');
    const auto separator = dot != std::string_view::npos &&
                                   (decimal == std::string_view::npos || dot < decimal)
                               ? dot
                               : decimal;
    if (separator == std::string_view::npos) return false;

    const auto separator_length = separator == decimal ? 7 : 1;
    auto trim = [](std::string_view input) {
        while (!input.empty() && input.front() == ' ') input.remove_prefix(1);
        while (!input.empty() && input.back() == ' ') input.remove_suffix(1);
        return input;
    };

    std::string whole;
    std::string fraction;
    if (!integer_text(trim(text.substr(0, separator)), whole) ||
        !integer_text(trim(text.substr(separator + separator_length)), fraction)) {
        return false;
    }
    value = whole + "." + fraction;
    return true;
}

bool match_parts(
    const std::vector<Part>& parts,
    std::size_t part_index,
    std::string_view input,
    std::size_t input_index,
    std::vector<std::string>& slots) {
    if (part_index == parts.size()) return input_index == input.size();

    const auto& part = parts[part_index];
    if (part.type == Part::Type::Literal) {
        return input.substr(input_index, part.text.size()) == part.text &&
               match_parts(parts, part_index + 1, input,
                           input_index + part.text.size(), slots);
    }

    std::size_t end = input_index;
    while (end < input.size() &&
           (std::isdigit(static_cast<unsigned char>(input[end])) || input[end] == ' ' ||
            (part.type == Part::Type::Float &&
             (input[end] == '.' || (input[end] >= 'a' && input[end] <= 'z'))))) {
        ++end;
    }

    for (std::size_t candidate = end; candidate > input_index; --candidate) {
        std::string normalized;
        const bool valid = part.type == Part::Type::Integer
                               ? integer_text(input.substr(input_index, candidate - input_index), normalized)
                               : float_text(input.substr(input_index, candidate - input_index), normalized);
        if (!valid) continue;

        slots.push_back(normalized);
        if (match_parts(parts, part_index + 1, input, candidate, slots)) return true;
        slots.pop_back();
    }
    return false;
}

ExecutionEngine::Impl* context(lua_State* lua) {
    lua_getglobal(lua, "__pilotmonitoring_context");
    auto* value = static_cast<ExecutionEngine::Impl*>(lua_touserdata(lua, -1));
    lua_pop(lua, 1);
    return value;
}

void log_lua_call(ExecutionEngine::Impl* context, std::string message) {
    if (context->logger) context->logger(message);
}

int slot_function(lua_State* lua) {
    luaL_checkstring(lua, 1);
    luaL_checkstring(lua, 2);
    lua_createtable(lua, 0, 3);
    lua_pushboolean(lua, 1);
    lua_setfield(lua, -2, "__slot");
    lua_pushvalue(lua, 1);
    lua_setfield(lua, -2, "name");
    lua_pushvalue(lua, 2);
    lua_setfield(lua, -2, "type");
    return 1;
}

int trigger_command(lua_State* lua) {
    auto* context_value = context(lua);
    const char* command = luaL_checkstring(lua, 1);
    log_lua_call(context_value, "trigger_command(\"" + std::string(command) + "\")");
    context_value->actions.emplace_back(TriggerCommandAction{command});
    return 0;
}

int say(lua_State* lua) {
    auto* context_value = context(lua);
    const char* message = luaL_checkstring(lua, 1);
    log_lua_call(context_value, "say(\"" + std::string(message) + "\")");
    context_value->actions.emplace_back(SpeakAction{message});
    return 0;
}

bool valid_sound_filename(std::string_view filename) {
    if (filename.empty()) return false;
    for (const unsigned char character : filename) {
        if (!std::isalnum(character) && character != '_') return false;
    }
    return true;
}

int play_sound(lua_State* lua) {
    auto* context_value = context(lua);
    const char* filename = luaL_checkstring(lua, 1);
    if (!valid_sound_filename(filename)) {
        return luaL_error(lua, "play_sound filename must contain only letters, numbers, and underscores");
    }
    log_lua_call(context_value, "play_sound(\"" + escape(filename) + "\")");
    context_value->actions.emplace_back(PlaySoundAction{filename});
    return 0;
}

bool validate_dataref(lua_State* lua, ExecutionEngine::Impl* context_value, std::string_view name,
                      DatarefType expected_type, bool write) {
    if (!context_value->owner->dataref_host()) {
        luaL_error(lua, "dataref host unavailable while accessing %s", std::string(name).c_str());
        return false;
    }
    std::string error;
    if (!context_value->owner->dataref_host()->validate_dataref(name, expected_type, write, error)) {
        luaL_error(lua, "%s", error.c_str());
        return false;
    }
    return true;
}

int set_integer(lua_State* lua) {
    auto* context_value = context(lua);
    const char* dataref = luaL_checkstring(lua, 1);
    const auto value = static_cast<int>(luaL_checkinteger(lua, 2));
    if (!validate_dataref(lua, context_value, dataref, DatarefType::Integer, true)) return 0;
    log_lua_call(context_value, "set_dataref_integer(\"" + escape(dataref) + "\", " + std::to_string(value) + ")");
    context_value->actions.emplace_back(SetIntegerDatarefAction{dataref, value});
    return 0;
}

int set_float(lua_State* lua) {
    auto* context_value = context(lua);
    const char* dataref = luaL_checkstring(lua, 1);
    const auto value = static_cast<float>(luaL_checknumber(lua, 2));
    if (!validate_dataref(lua, context_value, dataref, DatarefType::Float, true)) return 0;
    std::ostringstream formatted;
    formatted << value;
    log_lua_call(context_value, "set_dataref_float(\"" + escape(dataref) + "\", " + formatted.str() + ")");
    context_value->actions.emplace_back(SetFloatDatarefAction{dataref, value});
    return 0;
}

int set_boolean(lua_State* lua) {
    auto* context_value = context(lua);
    const char* dataref = luaL_checkstring(lua, 1);
    const bool value = lua_toboolean(lua, 2) != 0;
    if (!validate_dataref(lua, context_value, dataref, DatarefType::Integer, true)) return 0;
    log_lua_call(context_value, "set_dataref_boolean(\"" + escape(dataref) + "\", " + (value ? "true" : "false") + ")");
    context_value->actions.emplace_back(
        SetBooleanDatarefAction{dataref, value});
    return 0;
}

int get_dataref(lua_State* lua, int kind) {
    auto* context_value = context(lua);
    const char* name = luaL_checkstring(lua, 1);
    std::string error;
    const char* kind_name = kind == 0 ? "integer" : kind == 1 ? "float" : "boolean";
    const std::string call = "get_dataref_" + std::string(kind_name) +
                             "(\"" + escape(name) + "\")";
    auto log_result = [&](std::string_view value) {
        std::string message = call + " -> " + std::string(value);
        if (!error.empty()) {
            message += ", \"" + escape(error) + "\"";
        }
        log_lua_call(context_value, message);
    };

    if (!context_value->owner->dataref_host()) {
        error = "no dataref host";
        log_result("nil");
        lua_pushnil(lua);
        lua_pushliteral(lua, "no dataref host");
        return 2;
    }

    const DatarefType expected_type = kind == 1 ? DatarefType::Float : DatarefType::Integer;
    if (!validate_dataref(lua, context_value, name, expected_type, false)) return 0;

    if (kind == 0) {
        auto value = context_value->owner->dataref_host()->get_integer(name, error);
        if (value) log_result(std::to_string(*value)); else log_result("nil");
        if (value) lua_pushinteger(lua, *value); else lua_pushnil(lua);
    } else if (kind == 1) {
        auto value = context_value->owner->dataref_host()->get_float(name, error);
        if (value) {
            std::ostringstream formatted;
            formatted << *value;
            log_result(formatted.str());
        } else {
            log_result("nil");
        }
        if (value) lua_pushnumber(lua, *value); else lua_pushnil(lua);
    } else {
        auto value = context_value->owner->dataref_host()->get_boolean(name, error);
        log_result(value ? (*value ? "true" : "false") : "nil");
        if (value) lua_pushboolean(lua, *value); else lua_pushnil(lua);
    }
    lua_pushstring(lua, error.c_str());
    return 2;
}

int get_integer(lua_State* lua) { return get_dataref(lua, 0); }
int get_float(lua_State* lua) { return get_dataref(lua, 1); }
int get_boolean(lua_State* lua) { return get_dataref(lua, 2); }

bool parse_one_phrase(
    lua_State* lua,
    int index,
    std::vector<Part>& phrase,
    std::string& error) {
    const lua_Integer count = luaL_len(lua, index);
    if (count == 0) {
        error = "phrase must not be empty";
        return false;
    }

    for (lua_Integer part_index = 1; part_index <= count; ++part_index) {
        lua_geti(lua, index, part_index);
        if (part_index > 1) phrase.push_back({Part::Type::Literal, " ", {}});

        if (lua_type(lua, -1) == LUA_TSTRING) {
            phrase.push_back({Part::Type::Literal, literal(lua_tostring(lua, -1)), {}});
        } else {
            lua_getfield(lua, -1, "__slot");
            const bool is_slot = lua_toboolean(lua, -1) != 0;
            lua_pop(lua, 1);
            if (!is_slot) {
                lua_pop(lua, 1);
                error = "phrase part must be a string or slot";
                return false;
            }

            lua_getfield(lua, -1, "name");
            const std::string name = luaL_checkstring(lua, -1);
            lua_pop(lua, 1);
            lua_getfield(lua, -1, "type");
            const std::string type = luaL_checkstring(lua, -1);
            lua_pop(lua, 1);
            if (type != "integer" && type != "float") {
                lua_pop(lua, 1);
                error = "unsupported slot type: " + type;
                return false;
            }
            phrase.push_back({type == "integer" ? Part::Type::Integer : Part::Type::Float,
                              {}, name});
        }
        lua_pop(lua, 1);
    }
    return true;
}

bool parse_phrases(
    lua_State* lua,
    int index,
    std::vector<std::vector<Part>>& phrases,
    std::string& error) {
    luaL_checktype(lua, index, LUA_TTABLE);
    const lua_Integer count = luaL_len(lua, index);
    if (count == 0) {
        error = "phrase list must not be empty";
        return false;
    }

    for (lua_Integer phrase_index = 1; phrase_index <= count; ++phrase_index) {
        lua_geti(lua, index, phrase_index);
        if (lua_type(lua, -1) != LUA_TTABLE) {
            lua_pop(lua, 1);
            error = "phrase list entries must be phrases";
            return false;
        }

        std::vector<Part> phrase;
        if (!parse_one_phrase(lua, -1, phrase, error)) {
            lua_pop(lua, 1);
            return false;
        }
        phrases.push_back(std::move(phrase));
        lua_pop(lua, 1);
    }
    return true;
}

int yield_continuation(lua_State* lua, int, lua_KContext kind) {
    if (kind == 3) {
        const int top = lua_gettop(lua);
        if (top >= 2 && lua_isnil(lua, top - 1)) {
            return luaL_error(lua, "%s", luaL_checkstring(lua, top));
        }
        if (top > 0) {
            lua_pushvalue(lua, top);
            return 1;
        }
    }
    return 0;
}

int wait_ms(lua_State* lua) {
    auto* context_value = context(lua);
    const double milliseconds = luaL_checknumber(lua, 1);
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
        return luaL_error(lua, "wait_ms requires a finite, non-negative duration");
    }
    if (!context_value->active) {
        return luaL_error(lua, "wait_ms may only be called from an active handler");
    }

    context_value->active->suspension = ExecutionEngine::Impl::Suspension::WaitMs;
    context_value->active->remaining_ms = milliseconds;
    return lua_yieldk(lua, 0, 1, yield_continuation);
}

int wait_until(lua_State* lua) {
    auto* context_value = context(lua);
    luaL_checktype(lua, 1, LUA_TFUNCTION);
    if (!context_value->active) {
        return luaL_error(lua, "wait_until may only be called from an active handler");
    }

    context_value->active->condition_ref = luaL_ref(lua, LUA_REGISTRYINDEX);
    context_value->active->suspension = ExecutionEngine::Impl::Suspension::WaitUntil;
    return lua_yieldk(lua, 0, 2, yield_continuation);
}

int wait_for_phrase(lua_State* lua) {
    auto* context_value = context(lua);
    if (!context_value->active) {
        return luaL_error(lua, "wait_for_phrase may only be called from an active handler");
    }

    std::vector<std::vector<Part>> phrases;
    std::string error;
    if (!parse_phrases(lua, 1, phrases, error)) return luaL_error(lua, "%s", error.c_str());

    const std::string grammar_text = grammar_for_phrases(phrases, false);
    const auto grammar = grammar_parser::parse(grammar_text.c_str());
    if (grammar.rules.empty()) return luaL_error(lua, "could not generate phrase wait grammar");

    context_value->active->phrases = std::move(phrases);
    context_value->active->suspension = ExecutionEngine::Impl::Suspension::WaitPhrase;
    context_value->owner->set_active_grammar(grammar_text, grammar);
    return lua_yieldk(lua, 0, 3, yield_continuation);
}

int register_handler(lua_State* lua) {
    auto* context_value = context(lua);
    luaL_checktype(lua, 1, LUA_TTABLE);

    lua_getfield(lua, 1, "id");
    Handler handler;
    handler.id = luaL_checkstring(lua, -1);
    lua_pop(lua, 1);

    for (const auto& existing : context_value->handlers) {
        if (existing.id == handler.id) {
            return luaL_error(lua, "duplicate handler id: %s", handler.id.c_str());
        }
    }

    lua_getfield(lua, 1, "phrases");
    std::string error;
    if (!parse_phrases(lua, -1, handler.phrases, error)) {
        lua_pop(lua, 1);
        return luaL_error(lua, "%s", error.c_str());
    }
    lua_pop(lua, 1);

    lua_getfield(lua, 1, "handler");
    if (!lua_isfunction(lua, -1)) return luaL_error(lua, "handler must be a function");
    handler.function_ref = luaL_ref(lua, LUA_REGISTRYINDEX);
    context_value->handlers.push_back(std::move(handler));
    return 0;
}

void install_api(lua_State* lua) {
    luaL_openlibs(lua);
    for (const char* name : {"io", "os", "debug", "package", "require", "dofile", "loadfile"}) {
        lua_pushnil(lua);
        lua_setglobal(lua, name);
    }

    lua_pushcfunction(lua, register_handler); lua_setglobal(lua, "register_handler");
    lua_pushcfunction(lua, slot_function); lua_setglobal(lua, "slot");
    lua_pushcfunction(lua, say); lua_setglobal(lua, "say");
    lua_pushcfunction(lua, play_sound); lua_setglobal(lua, "play_sound");
    lua_pushcfunction(lua, trigger_command); lua_setglobal(lua, "trigger_command");
    lua_pushcfunction(lua, set_integer); lua_setglobal(lua, "set_dataref_integer");
    lua_pushcfunction(lua, set_float); lua_setglobal(lua, "set_dataref_float");
    lua_pushcfunction(lua, set_boolean); lua_setglobal(lua, "set_dataref_boolean");
    lua_pushcfunction(lua, get_integer); lua_setglobal(lua, "get_dataref_integer");
    lua_pushcfunction(lua, get_float); lua_setglobal(lua, "get_dataref_float");
    lua_pushcfunction(lua, get_boolean); lua_setglobal(lua, "get_dataref_boolean");
    lua_pushcfunction(lua, wait_ms); lua_setglobal(lua, "wait_ms");
    lua_pushcfunction(lua, wait_until); lua_setglobal(lua, "wait_until");
    lua_pushcfunction(lua, wait_for_phrase); lua_setglobal(lua, "wait_for_phrase");
    lua_pushnil(lua); lua_setglobal(lua, "__pilotmonitoring_context");
}

} // namespace

Event Event::transcript_event(std::string text) { return {Type::Transcript, std::move(text), 0.0F}; }
Event Event::tick_event(float delta) { return {Type::Tick, {}, delta}; }

ExecutionEngine::ExecutionEngine(DatarefHost* host, LogCallback logger)
    : impl_(std::make_unique<Impl>()), dataref_host_(host) {
    impl_->logger = std::move(logger);
    impl_->owner = this;
    impl_->lua = luaL_newstate();
    install_api(impl_->lua);
    lua_pushlightuserdata(impl_->lua, impl_.get());
    lua_setglobal(impl_->lua, "__pilotmonitoring_context");
}

void ExecutionEngine::set_active_grammar(
    std::string text,
    grammar_parser::parse_state grammar) {
    grammar_text_ = std::move(text);
    grammar_ = std::move(grammar);
}

ExecutionEngine::~ExecutionEngine() {
    if (impl_->active) {
        if (impl_->active->condition_ref != LUA_NOREF) {
            luaL_unref(impl_->lua, LUA_REGISTRYINDEX, impl_->active->condition_ref);
        }
        luaL_unref(impl_->lua, LUA_REGISTRYINDEX, impl_->active->thread_ref);
    }
    if (impl_->lua) {
        for (auto& handler : impl_->handlers) {
            luaL_unref(impl_->lua, LUA_REGISTRYINDEX, handler.function_ref);
        }
        lua_close(impl_->lua);
    }
}

std::vector<CommandInfo> ExecutionEngine::commands() const {
    std::vector<CommandInfo> result;
    result.reserve(impl_->handlers.size());
    for (const auto& handler : impl_->handlers) {
        CommandInfo info;
        info.id = handler.id;
        for (const auto& phrase : handler.phrases) {
            info.triggers.push_back(trigger_text(phrase));
        }
        result.push_back(std::move(info));
    }
    return result;
}

std::vector<std::string> ExecutionEngine::expected_triggers() const {
    std::vector<std::string> result;
    if (impl_->active) {
        if (impl_->active->suspension != Impl::Suspension::WaitPhrase) return result;
        for (const auto& phrase : impl_->active->phrases) {
            result.push_back(trigger_text(phrase));
        }
        return result;
    }
    for (const auto& handler : impl_->handlers) {
        for (const auto& phrase : handler.phrases) {
            result.push_back(trigger_text(phrase));
        }
    }
    return result;
}

std::optional<ActiveCoroutineInfo> ExecutionEngine::active_coroutine() const {
    if (!impl_->active || impl_->active->suspension == Impl::Suspension::None) {
        return std::nullopt;
    }
    const auto& active = *impl_->active;
    ActiveCoroutineInfo info;
    info.command_id = active.command_id;
    info.remaining_ms = active.remaining_ms;
    switch (active.suspension) {
    case Impl::Suspension::WaitMs:
        info.reason = ActiveCoroutineInfo::YieldReason::WaitMs;
        break;
    case Impl::Suspension::WaitUntil:
        info.reason = ActiveCoroutineInfo::YieldReason::WaitUntil;
        break;
    case Impl::Suspension::WaitPhrase:
        info.reason = ActiveCoroutineInfo::YieldReason::WaitPhrase;
        info.accepted_grammar = grammar_text_;
        break;
    case Impl::Suspension::None:
        break;
    }
    return info;
}

bool ExecutionEngine::load_from_file(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "could not open Lua config: " + path.string();
        return false;
    }
    std::ostringstream text;
    text << input.rdbuf();
    return load_from_lua_text(text.str(), error);
}

bool ExecutionEngine::load_from_lua_text(std::string_view source, std::string& error) {
    if (impl_->active) {
        if (impl_->active->condition_ref != LUA_NOREF) {
            luaL_unref(impl_->lua, LUA_REGISTRYINDEX, impl_->active->condition_ref);
        }
        luaL_unref(impl_->lua, LUA_REGISTRYINDEX, impl_->active->thread_ref);
        impl_->active.reset();
    }
    for (auto& handler : impl_->handlers) {
        luaL_unref(impl_->lua, LUA_REGISTRYINDEX, handler.function_ref);
    }

    failed_ = false;
    impl_->handlers.clear();
    grammar_text_.clear();
    top_level_grammar_text_.clear();
    grammar_ = {};

    if (luaL_loadbuffer(impl_->lua, source.data(), source.size(), "commands.lua") != LUA_OK ||
        lua_pcall(impl_->lua, 0, 0, 0) != LUA_OK) {
        error = lua_tostring(impl_->lua, -1);
        lua_pop(impl_->lua, 1);
        failed_ = true;
        return false;
    }

    top_level_grammar_text_ = grammar_for(impl_->handlers);
    grammar_text_ = top_level_grammar_text_;
    grammar_ = grammar_parser::parse(grammar_text_.c_str());
    if (grammar_.rules.empty()) {
        error = "Lua config registered no valid grammar";
        failed_ = true;
        return false;
    }
    return true;
}

void ExecutionEngine::clear_active() {
    if (!impl_->active) return;
    if (impl_->active->condition_ref != LUA_NOREF) {
        luaL_unref(impl_->lua, LUA_REGISTRYINDEX, impl_->active->condition_ref);
    }
    luaL_unref(impl_->lua, LUA_REGISTRYINDEX, impl_->active->thread_ref);
    impl_->active.reset();

    grammar_text_ = top_level_grammar_text_;
    grammar_ = grammar_parser::parse(grammar_text_.c_str());
}

bool ExecutionEngine::resume_active(int arguments, std::string& error) {
    if (!impl_->active) return true;
    int results = 0;
    const int status = lua_resume(impl_->active->thread, impl_->lua, arguments, &results);
    if (status == LUA_YIELD) return true;
    if (status != LUA_OK) {
        error = lua_tostring(impl_->active->thread, -1);
        clear_active();
        return false;
    }
    clear_active();
    return true;
}

std::vector<Action> ExecutionEngine::handle_event(const Event& event, std::string& error) {
    impl_->actions.clear();
    error.clear();
    if (failed_) {
        error = "execution engine is failed";
        return {};
    }

    if (impl_->active) {
        if (event.type == Event::Type::Tick) {
            auto& active = *impl_->active;
            if (active.suspension == Impl::Suspension::WaitMs) {
                active.remaining_ms -= std::max(
                    0.0, static_cast<double>(event.delta_seconds) * 1000.0);
                if (active.remaining_ms <= 0.0) resume_active(0, error);
            } else if (active.suspension == Impl::Suspension::WaitUntil) {
                lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, active.condition_ref);
                if (lua_pcall(impl_->lua, 0, 1, 0) != LUA_OK) {
                    error = lua_tostring(impl_->lua, -1);
                    lua_pop(impl_->lua, 1);
                    clear_active();
                } else {
                    const bool ready = lua_toboolean(impl_->lua, -1) != 0;
                    lua_pop(impl_->lua, 1);
                    if (ready) {
                        luaL_unref(impl_->lua, LUA_REGISTRYINDEX, active.condition_ref);
                        active.condition_ref = LUA_NOREF;
                        active.suspension = Impl::Suspension::None;
                        resume_active(0, error);
                    }
                }
            }
        } else if (impl_->active->suspension == Impl::Suspension::WaitPhrase) {
            const std::string input = lower(event.transcript);
            auto& active = *impl_->active;
            std::vector<std::string> values;
            bool matched = false;

            for (const auto& phrase : active.phrases) {
                if (!match_parts(phrase, 0, input, 0, values)) continue;
                matched = true;
                lua_createtable(impl_->lua, 0, static_cast<int>(values.size()));
                std::size_t value_index = 0;
                for (const auto& part : phrase) {
                    if (part.type == Part::Type::Literal) continue;
                    if (part.type == Part::Type::Integer) {
                        lua_pushinteger(impl_->lua, std::stoi(values[value_index]));
                    } else {
                        lua_pushnumber(impl_->lua, std::stod(values[value_index]));
                    }
                    lua_setfield(impl_->lua, -2, part.name.c_str());
                    ++value_index;
                }
                lua_xmove(impl_->lua, active.thread, 1);
                active.suspension = Impl::Suspension::None;
                resume_active(1, error);
                break;
            }

            if (!matched && error.empty() && impl_->active) {
                impl_->actions.emplace_back(PlaySoundAction{"negative_beep"});
                lua_pushnil(impl_->active->thread);
                lua_pushliteral(impl_->active->thread,
                                "phrase wait did not match the transcript");
                impl_->active->suspension = Impl::Suspension::None;
                resume_active(2, error);
            }
        }
        return impl_->actions;
    }

    if (event.type == Event::Type::Tick) return {};
    const std::string input = lower(event.transcript);
    std::size_t command_start = 0;
    while (command_start <= input.size()) {
        const std::size_t comma = input.find(',', command_start);
        const std::size_t length = comma == std::string::npos
                                       ? input.size() - command_start
                                       : comma - command_start;
        std::string_view command(input.data() + command_start, length);
        while (!command.empty() && command.front() == ' ') command.remove_prefix(1);
        while (!command.empty() && command.back() == ' ') command.remove_suffix(1);

        bool matched = false;
        for (auto& handler : impl_->handlers) {
            for (const auto& phrase : handler.phrases) {
                std::vector<std::string> values;
                if (!match_parts(phrase, 0, command, 0, values)) continue;
                matched = true;

                impl_->active = std::make_unique<Impl::ActiveCoroutine>();
                impl_->active->thread = lua_newthread(impl_->lua);
                impl_->active->thread_ref = luaL_ref(impl_->lua, LUA_REGISTRYINDEX);
                impl_->active->command_id = handler.id;
                lua_rawgeti(impl_->active->thread, LUA_REGISTRYINDEX, handler.function_ref);
                lua_createtable(impl_->active->thread, 0, static_cast<int>(values.size()));

                std::size_t value_index = 0;
                for (const auto& part : phrase) {
                    if (part.type == Part::Type::Literal) continue;
                    if (part.type == Part::Type::Integer) {
                        lua_pushinteger(impl_->active->thread,
                                        std::stoi(values[value_index]));
                    } else {
                        lua_pushnumber(impl_->active->thread,
                                       std::stod(values[value_index]));
                    }
                    lua_setfield(impl_->active->thread, -2, part.name.c_str());
                    ++value_index;
                }
                resume_active(1, error);
                break;
            }
            if (matched) break;
        }

        if (!matched) {
            impl_->actions.emplace_back(PlaySoundAction{"negative_beep"});
        }
        if (!error.empty() || (matched && impl_->active) || comma == std::string::npos) break;
        command_start = comma + 1;
    }
    return impl_->actions;
}
