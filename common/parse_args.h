#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string_view>

inline bool parse_u32_arg(const char* text, uint32_t& value) {
    if (text == nullptr) {
        return false;
    }

    std::string_view view(text);
    if (view.empty()) {
        return false;
    }

    uint32_t parsed = 0;
    auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), parsed);
    if (ec != std::errc{} || ptr != view.data() + view.size()) {
        return false;
    }

    value = parsed;
    return true;
}

inline bool require_u32_arg(const char* option, const char* text, uint32_t& value) {
    if (parse_u32_arg(text, value)) {
        return true;
    }

    std::fprintf(stderr, "Invalid value for %s: %s\n", option, text ? text : "<null>");
    return false;
}
