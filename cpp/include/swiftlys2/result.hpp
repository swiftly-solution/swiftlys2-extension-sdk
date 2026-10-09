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
 * @brief An error status and an owned diagnostic message.
 * @note Host errors preserve their original status, including unknown codes.
 * Local argument validation uses SW_E_INVALID_ARG; other local failures use
 * SW_E_FAILED. Status meanings depend on the operation.
 */
class Error {
public:
    /** @brief Construct an error with a nonzero status and a diagnostic. */
    Error(sw_status code, std::string message)
        : code_(code), message_(std::move(message)) {}

    /** @brief Original host status or the status assigned by SDK validation. */
    [[nodiscard]] sw_status code() const noexcept { return code_; }
    /** @brief Detailed diagnostic, owned by this error. */
    [[nodiscard]] const std::string& msg() const noexcept { return message_; }

private:
    sw_status code_;
    std::string message_;
};

/**
 * @brief SDK operation result with an Error on failure.
 * @tparam T Success value, or void for operations without a return value.
 * @note Errors describe the operation and its documented failure causes.
 * Formatting errors may allocate.
 */
template<class T>
using Result = std::expected<T, Error>;

namespace detail {

/** @brief Construct a failed result from a status and formatted diagnostic. */
inline std::unexpected<Error> fail(sw_status code, std::string message)
{
    return std::unexpected(Error(code, std::move(message)));
}

/**
 * @brief Format a host status using the failure causes documented for its operation.
 * @note Status meanings depend on the operation; callers supply the relevant
 * SW_E_INVALID_ARG and SW_E_FAILED causes from sw_extension.h.
 */
inline std::unexpected<Error> fail(
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
    return fail(code, std::move(message));
}

/** @brief Convert a host status into success or an operation-specific error. */
inline Result<void> check(
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
inline Result<std::string> c_string(std::string_view value, std::string_view parameter)
{
    if (value.find('\0') != std::string_view::npos)
        return fail(SW_E_INVALID_ARG, std::string(parameter) + ": embedded NUL is not allowed");
    return std::string(value);
}

} // namespace detail
} // namespace sw
