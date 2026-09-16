#include "execution_engine.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "lua.hpp"

struct Part { enum class Type { Literal, Integer, Float }; Type type; std::string text; std::string name; };
struct Handler { std::string id; std::vector<std::vector<Part>> phrases; int function_ref = LUA_NOREF; };

struct ExecutionEngine::Impl {
    lua_State* lua = nullptr;
    std::vector<Handler> handlers;
    std::vector<Action> actions;
    ExecutionEngine* owner = nullptr;
    ExecutionEngine::LogCallback logger;
};

namespace {

std::string lower(std::string_view text) {
    std::string out; out.reserve(text.size());
    bool space = true;
    for (unsigned char c : text) {
        if (std::isspace(c)) { if (!space) out.push_back(' '); space = true; }
        else { out.push_back(static_cast<char>(std::tolower(c))); space = false; }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string literal(std::string_view text) {
    std::string out; out.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isspace(c)) out.push_back(' ');
        else out.push_back(static_cast<char>(std::tolower(c)));
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

std::string grammar_for(const std::vector<Handler>& handlers) {
    std::ostringstream out;
    out << "root ::= init command (\",\" init command)*\ninit ::= \" \"\n\ncommand ::= (\n";
    bool first = true;
    for (const auto& h : handlers) for (const auto& phrase : h.phrases) {
        out << (first ? "    " : "  | "); first = false;
        bool first_part = true;
        for (const auto& part : phrase) {
            if (!first_part) out << ' '; first_part = false;
            if (part.type == Part::Type::Literal) out << '"' << escape(part.text) << '"';
            else if (part.type == Part::Type::Integer) out << "integer_slot";
            else out << "float_slot";
        }
        out << '\n';
    }
    out << ")\n\ninteger_slot ::= digit+ | digit (\" \" digit)+\n"
           << "float_slot ::= digit+ (\".\" | \"decimal\") digit+\n"
           << "digit ::= [0-9]\n";
    return out.str();
}

bool integer_text(std::string_view text, std::string& value) {
    if (text.empty()) return false;
    for (char c : text) if (c != ' ' && !std::isdigit(static_cast<unsigned char>(c))) return false;
    if (text.front() == ' ' || text.back() == ' ') return false;
    value.clear(); for (char c : text) if (c != ' ') value += c;
    return !value.empty();
}

bool float_text(std::string_view text, std::string& value) {
    const auto decimal = text.find("decimal");
    const auto dot = text.find('.');
    const auto pos = dot != std::string_view::npos && (decimal == std::string_view::npos || dot < decimal) ? dot : decimal;
    if (pos == std::string_view::npos) return false;
    const auto len = pos == decimal ? 7 : 1;
    std::string whole, fraction;
    auto trim = [](std::string_view v) { while (!v.empty() && v.front() == ' ') v.remove_prefix(1); while (!v.empty() && v.back() == ' ') v.remove_suffix(1); return v; };
    if (!integer_text(trim(text.substr(0, pos)), whole) || !integer_text(trim(text.substr(pos + len)), fraction)) return false;
    value = whole + "." + fraction; return true;
}

bool match_parts(const std::vector<Part>& parts, std::size_t pi, std::string_view input, std::size_t ti, std::vector<std::string>& slots) {
    if (pi == parts.size()) return ti == input.size();
    const auto& p = parts[pi];
    if (p.type == Part::Type::Literal) {
        return input.substr(ti, p.text.size()) == p.text && match_parts(parts, pi + 1, input, ti + p.text.size(), slots);
    }
    std::size_t end = ti;
    while (end < input.size() && (std::isdigit(static_cast<unsigned char>(input[end])) || input[end] == ' ' || (p.type == Part::Type::Float && (input[end] == '.' || (input[end] >= 'a' && input[end] <= 'z'))))) ++end;
    for (std::size_t candidate = end; candidate > ti; --candidate) {
        std::string normalized;
        if ((p.type == Part::Type::Integer ? integer_text(input.substr(ti, candidate - ti), normalized) : float_text(input.substr(ti, candidate - ti), normalized))) {
            slots.push_back(normalized);
            if (match_parts(parts, pi + 1, input, candidate, slots)) return true;
            slots.pop_back();
        }
    }
    return false;
}

ExecutionEngine::Impl* context(lua_State* lua) {
    lua_getglobal(lua, "__pilotmonitoring_context");
    auto* value = static_cast<ExecutionEngine::Impl*>(lua_touserdata(lua, -1));
    lua_pop(lua, 1); return value;
}

void log_lua_call(ExecutionEngine::Impl* c, std::string message) {
    if (c->logger) c->logger(message);
}

int slot_function(lua_State* lua) {
    luaL_checkstring(lua, 1); luaL_checkstring(lua, 2);
    lua_createtable(lua, 0, 3);
    lua_pushboolean(lua, 1); lua_setfield(lua, -2, "__slot");
    lua_pushvalue(lua, 1); lua_setfield(lua, -2, "name");
    lua_pushvalue(lua, 2); lua_setfield(lua, -2, "type");
    return 1;
}

int trigger_command(lua_State* lua) {
    auto* c = context(lua);
    const char* command = luaL_checkstring(lua, 1);
    log_lua_call(c, "trigger_command(\"" + std::string(command) + "\")");
    c->actions.emplace_back(TriggerCommandAction{command});
    return 0;
}
int set_integer(lua_State* lua) {
    auto* c = context(lua);
    const char* dataref = luaL_checkstring(lua, 1);
    const auto value = static_cast<int>(luaL_checkinteger(lua, 2));
    log_lua_call(c, "set_dataref_integer(\"" + std::string(dataref) + "\", " + std::to_string(value) + ")");
    c->actions.emplace_back(SetIntegerDatarefAction{dataref, value});
    return 0;
}
int set_float(lua_State* lua) {
    auto* c = context(lua);
    const char* dataref = luaL_checkstring(lua, 1);
    const auto value = static_cast<float>(luaL_checknumber(lua, 2));
    log_lua_call(c, "set_dataref_float(\"" + std::string(dataref) + "\", " + std::to_string(value) + ")");
    c->actions.emplace_back(SetFloatDatarefAction{dataref, value});
    return 0;
}
int set_boolean(lua_State* lua) {
    auto* c = context(lua);
    const char* dataref = luaL_checkstring(lua, 1);
    const bool value = lua_toboolean(lua, 2) != 0;
    log_lua_call(c, "set_dataref_boolean(\"" + std::string(dataref) + "\", " + (value ? "true" : "false") + ")");
    c->actions.emplace_back(SetBooleanDatarefAction{dataref, value});
    return 0;
}

int get_dataref(lua_State* lua, int kind) {
    auto* c = context(lua); const char* name = luaL_checkstring(lua, 1); std::string error;
    const char* function_name = kind == 0 ? "get_dataref_integer" : kind == 1 ? "get_dataref_float" : "get_dataref_boolean";
    log_lua_call(c, std::string(function_name) + "(\"" + name + "\")");
    if (!c->owner->dataref_host()) { lua_pushnil(lua); lua_pushliteral(lua, "no dataref host"); return 2; }
    if (kind == 0) { auto v = c->owner->dataref_host()->get_integer(name, error); if (v) lua_pushinteger(lua, *v); else lua_pushnil(lua); }
    else if (kind == 1) { auto v = c->owner->dataref_host()->get_float(name, error); if (v) lua_pushnumber(lua, *v); else lua_pushnil(lua); }
    else { auto v = c->owner->dataref_host()->get_boolean(name, error); if (v) lua_pushboolean(lua, *v); else lua_pushnil(lua); }
    lua_pushstring(lua, error.c_str()); return 2;
}
int get_integer(lua_State* l) { return get_dataref(l, 0); }
int get_float(lua_State* l) { return get_dataref(l, 1); }
int get_boolean(lua_State* l) { return get_dataref(l, 2); }
int not_implemented(lua_State* lua) { return luaL_error(lua, "this Lua host function is not implemented yet"); }

int register_handler(lua_State* lua) {
    auto* c = context(lua); luaL_checktype(lua, 1, LUA_TTABLE);
    lua_getfield(lua, 1, "id"); const char* id = luaL_checkstring(lua, -1); Handler handler; handler.id = id; lua_pop(lua, 1);
    for (const auto& h : c->handlers) if (h.id == handler.id) return luaL_error(lua, "duplicate handler id: %s", handler.id.c_str());
    lua_getfield(lua, 1, "phrases"); luaL_checktype(lua, -1, LUA_TTABLE); const lua_Integer phrase_count = luaL_len(lua, -1);
    if (phrase_count == 0) return luaL_error(lua, "handler must have phrases");
    for (lua_Integer i = 1; i <= phrase_count; ++i) {
        lua_geti(lua, -1, i); luaL_checktype(lua, -1, LUA_TTABLE); std::vector<Part> phrase; const lua_Integer count = luaL_len(lua, -1);
        for (lua_Integer j = 1; j <= count; ++j) {
        lua_geti(lua, -1, j);
            if (j > 1) phrase.push_back({Part::Type::Literal, " ", {}});
            if (lua_type(lua, -1) == LUA_TSTRING) phrase.push_back({Part::Type::Literal, literal(lua_tostring(lua, -1)), {}});
            else { lua_getfield(lua, -1, "__slot"); const bool is_slot = lua_toboolean(lua, -1); lua_pop(lua, 1); if (!is_slot) return luaL_error(lua, "phrase part must be a string or slot"); lua_getfield(lua, -1, "name"); const std::string name = luaL_checkstring(lua, -1); lua_pop(lua, 1); lua_getfield(lua, -1, "type"); const std::string type = luaL_checkstring(lua, -1); lua_pop(lua, 1); if (type != "integer" && type != "float") return luaL_error(lua, "unsupported slot type: %s", type.c_str()); phrase.push_back({type == "integer" ? Part::Type::Integer : Part::Type::Float, {}, name}); }
            lua_pop(lua, 1);
        }
        handler.phrases.push_back(std::move(phrase)); lua_pop(lua, 1);
    }
    lua_pop(lua, 1); lua_getfield(lua, 1, "handler"); if (!lua_isfunction(lua, -1)) return luaL_error(lua, "handler must be a function"); handler.function_ref = luaL_ref(lua, LUA_REGISTRYINDEX); c->handlers.push_back(std::move(handler)); return 0;
}

void install_api(lua_State* lua) {
    luaL_openlibs(lua);
    for (const char* name : {"io", "os", "debug", "package", "require", "dofile", "loadfile"}) { lua_pushnil(lua); lua_setglobal(lua, name); }
    lua_pushcfunction(lua, register_handler); lua_setglobal(lua, "register_handler");
    lua_pushcfunction(lua, slot_function); lua_setglobal(lua, "slot");
    lua_pushcfunction(lua, trigger_command); lua_setglobal(lua, "trigger_command");
    lua_pushcfunction(lua, set_integer); lua_setglobal(lua, "set_dataref_integer");
    lua_pushcfunction(lua, set_float); lua_setglobal(lua, "set_dataref_float");
    lua_pushcfunction(lua, set_boolean); lua_setglobal(lua, "set_dataref_boolean");
    lua_pushcfunction(lua, get_integer); lua_setglobal(lua, "get_dataref_integer");
    lua_pushcfunction(lua, get_float); lua_setglobal(lua, "get_dataref_float");
    lua_pushcfunction(lua, get_boolean); lua_setglobal(lua, "get_dataref_boolean");
    lua_pushcfunction(lua, not_implemented); lua_setglobal(lua, "speak");
    lua_pushcfunction(lua, not_implemented); lua_setglobal(lua, "sleep");
    lua_pushcfunction(lua, not_implemented); lua_setglobal(lua, "wait_until");
    lua_pushcfunction(lua, not_implemented); lua_setglobal(lua, "display_message");
    lua_pushnil(lua); lua_setglobal(lua, "__pilotmonitoring_context");
}

} // namespace


Event Event::transcript_event(std::string text) { return {Type::Transcript, std::move(text), 0.0F}; }
Event Event::tick_event(float delta) { return {Type::Tick, {}, delta}; }

ExecutionEngine::ExecutionEngine(DatarefHost* host, LogCallback logger) : impl_(std::make_unique<Impl>()), dataref_host_(host) {
    impl_->logger = std::move(logger);
 
    impl_->owner = this; impl_->lua = luaL_newstate(); install_api(impl_->lua); lua_pushlightuserdata(impl_->lua, impl_.get()); lua_setglobal(impl_->lua, "__pilotmonitoring_context");
}
ExecutionEngine::~ExecutionEngine() { if (impl_->lua) { for (auto& h : impl_->handlers) luaL_unref(impl_->lua, LUA_REGISTRYINDEX, h.function_ref); lua_close(impl_->lua); } }

bool ExecutionEngine::load_from_file(const std::filesystem::path& path, std::string& error) { std::ifstream input(path); if (!input) { error = "could not open Lua config: " + path.string(); return false; } std::ostringstream text; text << input.rdbuf(); return load_from_lua_text(text.str(), error); }

bool ExecutionEngine::load_from_lua_text(std::string_view source, std::string& error) {
    failed_ = false; impl_->handlers.clear(); grammar_text_.clear(); grammar_ = {};
    if (luaL_loadbuffer(impl_->lua, source.data(), source.size(), "commands.lua") != LUA_OK || lua_pcall(impl_->lua, 0, 0, 0) != LUA_OK) { error = lua_tostring(impl_->lua, -1); lua_pop(impl_->lua, 1); failed_ = true; return false; }
    grammar_text_ = grammar_for(impl_->handlers); grammar_ = grammar_parser::parse(grammar_text_.c_str());
    if (grammar_.rules.empty()) { error = "Lua config registered no valid grammar"; failed_ = true; return false; }
    return true;
}

std::vector<Action> ExecutionEngine::handle_event(const Event& event, std::string& error) {
    impl_->actions.clear(); if (failed_) { error = "execution engine is failed"; return {}; }
    if (event.type == Event::Type::Tick) return {};
    const std::string input = lower(event.transcript);
    std::size_t command_start = 0;
    while (command_start <= input.size()) {
        const std::size_t comma = input.find(',', command_start);
        const std::size_t command_length = comma == std::string::npos
            ? input.size() - command_start
            : comma - command_start;
        std::string_view command(input.data() + command_start, command_length);
        while (!command.empty() && command.front() == ' ') command.remove_prefix(1);
        while (!command.empty() && command.back() == ' ') command.remove_suffix(1);
        bool matched = false;

        for (auto& h : impl_->handlers) {
            for (const auto& phrase : h.phrases) {
                std::vector<std::string> values;
                if (!match_parts(phrase, 0, command, 0, values)) continue;
                matched = true;
                if (impl_->logger) {
                    std::ostringstream log;
                    log << "matched command id \"" << h.id
                        << "\"; starting handler with slots = {";
                    std::size_t slot_index = 0;
                    bool first_slot = true;
                    for (const auto& part : phrase) {
                        if (part.type == Part::Type::Literal) continue;
                        if (!first_slot) log << ", ";
                        first_slot = false;
                        log << part.name << " = " << values[slot_index++];
                    }
                    log << "}";
                    impl_->logger(log.str());
                }
                lua_State* thread = lua_newthread(impl_->lua);
                const int thread_ref = luaL_ref(impl_->lua, LUA_REGISTRYINDEX);
                lua_rawgeti(thread, LUA_REGISTRYINDEX, h.function_ref);
                lua_createtable(thread, 0, static_cast<int>(values.size()));
                std::size_t value_index = 0;
                for (const auto& part : phrase) {
                    if (part.type == Part::Type::Literal) continue;
                    const std::string& value = values[value_index++];
                    if (part.type == Part::Type::Integer) lua_pushinteger(thread, std::stoi(value));
                    else lua_pushnumber(thread, std::stod(value));
                    lua_setfield(thread, -2, part.name.c_str());
                }
                int results = 0;
                const int status = lua_resume(thread, impl_->lua, 1, &results);
                if (status != LUA_OK) {
                    error = lua_tostring(thread, -1);
                    luaL_unref(impl_->lua, LUA_REGISTRYINDEX, thread_ref);
                    failed_ = true;
                    return {};
                }
                luaL_unref(impl_->lua, LUA_REGISTRYINDEX, thread_ref);
                break;
            }
            if (matched) break;
        }

        if (comma == std::string::npos) break;
        command_start = comma + 1;
    }
    return impl_->actions;
}
