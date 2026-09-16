#pragma once

#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include "execution_engine.h"

struct FakeHost final : DatarefHost {
    std::map<std::string, float> floats;
    std::map<std::string, bool> booleans;

    std::optional<int> get_integer(std::string_view, std::string& error) override {
        error = "not an integer";
        return std::nullopt;
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
