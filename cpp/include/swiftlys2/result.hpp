#pragma once

#include <swiftlys2/sw_extension.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "SwiftlyS2 C++ SDK requires C++23 and a standard library with std::expected."
#endif

namespace sw {

/**
 * @brief SDK operation result with a detailed std::string error on failure.
 * @tparam T Success value, or void for operations without a return value.
 * @note Errors describe the operation and its documented failure causes. The
 * underlying C API still returns sw_status. Formatting errors may allocate.
 */
template<class T>
using result = std::expected<T, std::string>;

namespace detail {

/** @brief Construct a failed result from an already formatted diagnostic. */
inline std::unexpected<std::string> fail(std::string message)
{
    return std::unexpected(std::move(message));
}

/**
 * @brief Format a host status using the failure causes documented for its operation.
 * @note Status meanings depend on the operation; callers supply the relevant
 * SW_E_INVALID_ARG and SW_E_FAILED causes from sw_extension.h.
 */
inline std::unexpected<std::string> fail(
    sw_status code, std::string_view operation, std::string_view invalid_arg,
    std::string_view failed = "operation failed")
{
    std::string message(operation);
    message += ": ";
    switch (code) {
    case SW_E_INVALID_ARG:
        message += invalid_arg;
        message += " (SW_E_INVALID_ARG)";
        break;
    case SW_E_FAILED:
        message += failed;
        message += " (SW_E_FAILED)";
        break;
    case SW_E_BUFFER_TOO_SMALL:
        message += "the supplied output buffer is too small (SW_E_BUFFER_TOO_SMALL)";
        break;
    case SW_E_INVALID_OPERATION:
        message += "the operation is not allowed in the current state (SW_E_INVALID_OPERATION)";
        break;
    default:
        message += "host returned unexpected status " + std::to_string(code);
        break;
    }
    return fail(std::move(message));
}

/** @brief Convert a host status into success or an operation-specific string error. */
inline result<void> check(
    sw_status code, std::string_view operation, std::string_view invalid_arg,
    std::string_view failed = "operation failed")
{
    if (code != SW_OK)
        return fail(code, operation, invalid_arg, failed);
    return {};
}

/**
 * @brief Copy a string view into a NUL-terminated string accepted by the C API.
 * @param value Input view, which need not already be terminated.
 * @param parameter Operation and parameter name used in invalid-input errors.
 * @return An owned terminated string, or a descriptive error for invalid input.
 */
inline result<std::string> c_string(std::string_view value, std::string_view parameter)
{
    if (value.find('\0') != std::string_view::npos)
        return fail(std::string(parameter) + ": embedded NUL is not allowed");
    return std::string(value);
}

} // namespace detail
} // namespace sw
