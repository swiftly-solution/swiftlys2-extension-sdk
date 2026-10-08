#pragma once

#include "memory.hpp"
#include "misc.hpp"
#include "schema.hpp"
#include "shared.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <utility>

namespace sw {

/**
 * @brief Extension APIs bound by the SDK before metadata getters and callbacks.
 * @note The underlying interfaces belong to the host and remain valid until
 * the extension is unloaded. Do not free them or use them after unload.
 * @see sw_core
 */
struct core {
    /** @brief Memory allocation, hooks, and address lookup APIs. */
    sw::memory memory{nullptr};
    /** @brief Schema field lookup and network state change APIs. */
    sw::schema schema{nullptr};
    /** @brief Shared pointer registry API. */
    sw::shared shared{nullptr};
    /** @brief Miscellaneous extension state captured during initialization. */
    sw::misc misc{false};
};

/**
 * @brief Base class for an SDK-owned extension and its lifecycle callbacks.
 * @note Implement name(), version(), and on_load(), then export the entry point
 * with SW_DEFINE_EXTENSION in exactly one source file. Destructors must not throw.
 */
class extension {
private:
    // Own the APIs before the view so they outlive it and derived resources.
    sw::core core_storage_{};

public:
    /** @brief Construct an extension whose APIs will be bound by the SDK. */
    extension() noexcept = default;
    /** @brief Keep each extension and its core view at a stable identity. */
    extension(const extension&) = delete;
    extension& operator=(const extension&) = delete;
    extension(extension&&) = delete;
    extension& operator=(extension&&) = delete;

    /** @brief Destroy the extension after its unload callback, while core is still valid. */
    virtual ~extension() noexcept = default;

    /**
     * @brief Extension name registered with sw_init::set_info.
     * @return A nonempty string literal or view into this object's members.
     * @note Metadata is copied during initialization. Do not return temporary strings.
     */
    virtual std::string_view name() const noexcept = 0;
    /** @brief Extension version; follows the same string rules as name(). */
    virtual std::string_view version() const noexcept = 0;
    /** @brief Optional extension author; an empty view registers an absent author. */
    virtual std::string_view author() const noexcept { return {}; }
    /** @brief Optional description; an empty view registers an absent description. */
    virtual std::string_view description() const noexcept { return {}; }

    /**
     * @brief Initialize the extension after its metadata and callbacks are registered.
     * @return Success, or a detailed error printed to stderr and returned as
     * SW_E_FAILED from the C entry point.
     * @note Access the host APIs through the inherited core_ member.
     * @note on_unload also runs if this callback returns an error or throws.
     */
    virtual result<void> on_load() = 0;
    /**
     * @brief Notify the extension when the set of loaded extensions changes.
     * @note The inherited core_ member remains valid until extension unload.
     * @note Called once after the initial loading pass, then after each successful
     * extension load or unload. Failed loads and unloads do not trigger it.
     * Loading or unloading extensions during a lifecycle callback fails.
     * @see sw_init::set_on_all_extensions_loaded_callback
     */
    virtual void on_all_extensions_loaded() {}
    /**
     * @brief Clean up before the extension and its member resources are destroyed.
     * @note The inherited core_ member remains valid during this callback
     * and member destruction.
     * @note Also called after a failed or throwing on_load. Handle partially
     * initialized state and explicitly release raw allocations and shared keys.
     * If initialization fails before on_load starts, only destruction runs.
     * @see sw_init::set_unload_callback
     */
    virtual void on_unload() {}

protected:
    /**
     * @brief Read-only view of this extension's API wrappers.
     * @note Interfaces are unavailable during construction. The SDK binds them
     * before metadata getters and lifecycle callbacks. Once bound, they remain
     * valid through unload and derived member destruction, including after
     * failed initialization.
     */
    const sw::core& core_ = core_storage_;

private:
    template<class T>
    friend sw_status run_extension(const sw_init_context* ctx) noexcept;
};

namespace detail {

inline void report_exception(const char* phase, const char* message) noexcept
{
    std::fprintf(stderr, "SwiftlyS2 Extension SDK: %s: %s\n", phase, message);
}

inline sw_status report_init_error(
    sw_status status, std::string_view operation, std::string_view invalid_arg,
    std::string_view failed = "operation failed")
{
    if (status == SW_E_INVALID_OPERATION) {
        const auto message = std::string(operation) + ": initialization has finished (SW_E_INVALID_OPERATION)";
        report_exception("initialization", message.c_str());
        return status;
    }
    const auto error = fail(status, operation, invalid_arg, failed);
    report_exception("initialization", error.error().c_str());
    return status;
}

struct extension_state {
    // The extension owns its API storage, which outlives derived resources.
    std::unique_ptr<extension> instance;
    bool load_started = false;

    explicit extension_state(std::unique_ptr<extension> instance) noexcept
        : instance(std::move(instance)) {}
};

inline void unload_callback(void* user_data) noexcept
{
    std::unique_ptr<extension_state> state(static_cast<extension_state*>(user_data));
    if (!state->load_started)
        return;
    try {
        state->instance->on_unload();
    } catch (const std::exception& exception) {
        report_exception("on_unload", exception.what());
    } catch (...) {
        report_exception("on_unload", "unknown exception");
    }
    // The owning pointer destroys member resources even if on_unload threw.
}

inline void all_loaded_callback(void* user_data) noexcept
{
    auto& state = *static_cast<extension_state*>(user_data);
    try {
        state.instance->on_all_extensions_loaded();
    } catch (const std::exception& exception) {
        report_exception("on_all_extensions_loaded", exception.what());
    } catch (...) {
        report_exception("on_all_extensions_loaded", "unknown exception");
    }
}

} // namespace detail

/**
 * @brief Initialize an SDK extension through the host's C ABI.
 * @tparam T A publicly derived, concrete, default-constructible extension type.
 * @param ctx Host initialization context; valid only during this call.
 * @return SW_OK on success, or a nonzero host status on failure.
 * @note Extension errors and exceptions are reported and become SW_E_FAILED.
 */
template<class T>
sw_status run_extension(const sw_init_context* ctx) noexcept
{
    try {
        if (!ctx || !ctx->core)
            return detail::report_init_error(SW_E_INVALID_ARG, "context.core",
                "initialization context or core callback is null");
        sw_core* raw = nullptr;
        auto status = ctx->core(ctx, SW_IFACE_CORE, &raw);
        if (status != SW_OK)
            return detail::report_init_error(status, "context.core", "context, interface name or output storage is null",
                "requested core interface was not found");
        if (!raw || !raw->init || !raw->memory || !raw->schema || !raw->shared)
            return detail::report_init_error(SW_E_INVALID_ARG, "context.core",
                "host returned a null core, init, memory, schema or shared interface");

        const auto* init = raw->init;
        if (!init->set_info || !init->is_hotreloaded || !init->set_unload_callback ||
            !init->set_on_all_extensions_loaded_callback)
            return detail::report_init_error(SW_E_INVALID_ARG, "core.init", "a required initialization callback is unavailable");
        std::int32_t hot_reloaded = 0;
        status = init->is_hotreloaded(init, &hot_reloaded);
        if (status != SW_OK)
            return detail::report_init_error(status, "init.is_hotreloaded", "initialization interface or output storage is null");

        // This conversion requires public inheritance, a concrete type and default construction.
        std::unique_ptr<extension> instance = std::make_unique<T>();
        instance->core_storage_ = sw::core{
            sw::memory(raw->memory), sw::schema(raw->schema),
            sw::shared(raw->shared), sw::misc(hot_reloaded != 0)};
        auto state = std::make_unique<detail::extension_state>(std::move(instance));

        status = init->set_unload_callback(init, detail::unload_callback, state.get());
        if (status != SW_OK)
            return detail::report_init_error(status, "init.set_unload_callback", "initialization interface is null");

        // Once registered, the host calls our unload trampoline on BOTH success and failure.
        // The trampoline now owns state. Never delete it again on an initialization error.
        auto* registered = state.release();
        status = init->set_on_all_extensions_loaded_callback(
            init, detail::all_loaded_callback, registered);
        if (status != SW_OK)
            return detail::report_init_error(status, "init.set_on_all_extensions_loaded_callback", "initialization interface is null");

        // Snapshot each getter immediately, then pass terminated strings for the host to copy.
        auto name = detail::c_string(registered->instance->name(), "extension.name");
        auto version = detail::c_string(registered->instance->version(), "extension.version");
        auto author = detail::c_string(registered->instance->author(), "extension.author");
        auto description = detail::c_string(registered->instance->description(), "extension.description");
        for (const auto* metadata : {&name, &version, &author, &description}) {
            if (!*metadata) {
                detail::report_exception("initialization", metadata->error().c_str());
                return SW_E_INVALID_ARG;
            }
        }
        if (name->empty() || version->empty())
            return detail::report_init_error(SW_E_INVALID_ARG, "extension metadata", "name and version must not be empty");
        status = init->set_info(init, name->c_str(), version->c_str(),
            author->empty() ? nullptr : author->c_str(),
            description->empty() ? nullptr : description->c_str());
        if (status != SW_OK)
            return detail::report_init_error(status, "init.set_info", "initialization interface, name or version is null");

        registered->load_started = true;
        auto loaded = registered->instance->on_load();
        if (!loaded) {
            detail::report_exception("on_load", loaded.error().c_str());
            return SW_E_FAILED;
        }
        return SW_OK;
    } catch (const std::exception& exception) {
        detail::report_exception("initialization", exception.what());
        return SW_E_FAILED;
    } catch (...) {
        detail::report_exception("initialization", "unknown exception");
        return SW_E_FAILED;
    }
}

} // namespace sw

/**
 * @brief Define and export sw_extension_init for the specified extension type.
 * @note Expand exactly once, in one .cpp file. The exported C entry is not inline.
 */
#define SW_DEFINE_EXTENSION(...)                                   \
    SW_API sw_status sw_extension_init(const sw_init_context* ctx)  \
    {                                                             \
        return ::sw::run_extension<__VA_ARGS__>(ctx);               \
    }
