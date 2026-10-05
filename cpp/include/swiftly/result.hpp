#pragma once

#include <swiftly/sw_extension.h>

#include <expected>
#include <string>
#include <string_view>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "SwiftlyS2 C++ SDK requires C++23 and a standard library with std::expected."
#endif

namespace sw {

struct error {
    sw_status code;
};

template<class T>
using result = std::expected<T, error>;

namespace detail {

inline std::unexpected<error> fail(sw_status code)
{
    return std::unexpected(error{code});
}

inline result<void> check(sw_status code)
{
    if (code != SW_OK)
        return fail(code);
    return {};
}

// The C API consumes NUL-terminated strings. A string_view need not be terminated.
inline result<std::string> c_string(std::string_view value)
{
    if (value.find('\0') != std::string_view::npos)
        return fail(SW_E_INVALID_ARG);
    return std::string(value);
}

} // namespace detail
} // namespace sw
