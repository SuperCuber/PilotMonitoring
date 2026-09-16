#pragma once

#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <set>
#include <string>

#include "execution_engine.h"

struct FakeHost final : DatarefHost {
    std::map<std::string, int> integers;
    std::map<std::string, float> floats;
    std::map<std::string, bool> booleans;
    std::map<std::string, DatarefType> types;
    std::set<std::string> read_only;

    bool validate_dataref(std::string_view name, DatarefType expected_type,
                          bool write, std::string& error) override {
        const std::string dataref{name};
        const auto found = types.find(dataref);
        const DatarefType actual_type = found == types.end() ? expected_type : found->second;
        if (actual_type != expected_type) {
            const char* expected = expected_type == DatarefType::Integer ? "integer" : "float";
            const char* actual = actual_type == DatarefType::Integer ? "integer" : "float";
            error = "dataref type mismatch for " + dataref + ": expected " + expected + ", found " + actual;
            return false;
        }
        if (write && read_only.count(dataref) != 0) {
            error = "dataref is read-only: " + dataref;
            return false;
        }
        return true;
    }

    std::optional<int> get_integer(std::string_view name, std::string& error) override {
        const auto value = integers.find(std::string(name));
        if (value == integers.end()) {
            error = "missing";
            return std::nullopt;
        }
        return value->second;
    }

    std::optional<float> get_float(std::string_view name, std::string& error) override {
        const auto value = floats.find(std::string(name));
        if (value == floats.end()) {
            error = "missing";
            return std::nullopt;
        }
        return value->second;
    }

    std::optional<bool> get_boolean(std::string_view name, std::string& error) override {
        const auto value = booleans.find(std::string(name));
        if (value == booleans.end()) {
            error = "missing";
            return std::nullopt;
        }
        return value->second;
    }
};

inline bool expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

template <typename Actual, typename Expected>
bool expect_equal(const Actual& actual, const Expected& expected, const std::string& label) {
    if (actual == expected) {
        return true;
    }

    std::ostringstream message;
    message << label << ": expected [" << expected << "], actual [" << actual << "]";
    std::cerr << message.str() << '\n';
    return false;
}
