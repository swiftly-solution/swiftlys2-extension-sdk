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
 * @brief Copyable view of the host APIs passed by value to Extension::OnLoad.
 * @note The underlying interfaces belong to the host and remain valid until
 * the extension is unloaded, including OnUnload and member destruction.
 * Copy this view when later callbacks need it. Copies do not own the interfaces
 * or extend their lifetime. Do not retain a pointer or reference to the OnLoad
 * parameter, free the host interfaces, or use them after unload.
 * @see sw_core
 */
class Core {
public:
    /** @brief Construct a view of the host interfaces and cached initialization state. */
    Core(sw::Memory memory, sw::Schema schema, sw::Shared shared, sw::Misc misc) noexcept
        : memory_(memory), schema_(schema), shared_(shared), misc_(misc) {}

    /** @brief Memory allocation, hooks, and address lookup APIs. */
    [[nodiscard]] const sw::Memory& Memory() const noexcept { return memory_; }
    /** @brief Schema field lookup and network state change APIs. */
    [[nodiscard]] const sw::Schema& Schema() const noexcept { return schema_; }
    /** @brief Shared pointer registry API. */
    [[nodiscard]] const sw::Shared& Shared() const noexcept { return shared_; }
    /** @brief Miscellaneous extension state captured during initialization. */
    [[nodiscard]] const sw::Misc& Misc() const noexcept { return misc_; }

private:
    sw::Memory memory_;
    sw::Schema schema_;
    sw::Shared shared_;
    sw::Misc misc_;
};

/**
 * @brief Base class for an SDK-owned extension and its lifecycle callbacks.
 * @note Implement Name(), Version(), and OnLoad(), then export the entry point
 * with SW_DEFINE_EXTENSION in exactly one source file. Destructors must not throw.
 */
class Extension {
public:
    /** @brief Construct an extension; host APIs are supplied only to OnLoad. */
    Extension() noexcept = default;
    /** @brief Keep each extension at a stable identity. */
    Extension(const Extension&) = delete;
    Extension& operator=(const Extension&) = delete;
    Extension(Extension&&) = delete;
    Extension& operator=(Extension&&) = delete;

    /** @brief Destroy the extension after its unload callback, while saved API views are valid. */
    virtual ~Extension() noexcept = default;

    /**
     * @brief Extension name registered with sw_init::set_info.
     * @return A nonempty string literal or view into this object's members.
     * @note Metadata is copied during initialization. Do not return temporary strings.
     */
    virtual std::string_view Name() const noexcept = 0;
    /** @brief Extension version; follows the same string rules as Name(). */
    virtual std::string_view Version() const noexcept = 0;
    /** @brief Optional extension author; an empty view registers an absent author. */
    virtual std::string_view Author() const noexcept { return {}; }
    /** @brief Optional description; an empty view registers an absent description. */
    virtual std::string_view Description() const noexcept { return {}; }

    /**
     * @brief Initialize the extension after its metadata and callbacks are registered.
     * @param core Copyable host API view. Store a copy if later callbacks need it.
     * @return Success, or a detailed error printed to stderr and returned as
     * SW_E_FAILED from the C entry point.
     * @note Host APIs are unavailable during construction and metadata getters.
     * Never retain a pointer or reference to this parameter.
     * @note OnUnload also runs if this callback returns an error or throws.
     */
    virtual Result<void> OnLoad(sw::Core core) = 0;
    /**
     * @brief Notify the extension when the set of loaded extensions changes.
     * @note Use an API view saved during OnLoad if host access is needed.
     * @note Called once after the initial loading pass, then after each successful
     * extension load or unload. Failed loads and unloads do not trigger it.
     * Loading or unloading extensions during a lifecycle callback fails.
     * @see sw_init::set_on_all_extensions_loaded_callback
     */
    virtual void OnAllExtensionsLoaded() {}
    /**
     * @brief Clean up before the extension and its member resources are destroyed.
     * @note API views saved during OnLoad remain valid during this callback
     * and member destruction, including after failed initialization.
     * @note Also called after a failed or throwing OnLoad. Handle partially
     * initialized state and explicitly release raw allocations and shared keys.
     * If initialization fails before OnLoad starts, only destruction runs.
     * @see sw_init::set_unload_callback
     */
    virtual void OnUnload() {}
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

struct ExtensionState {
    std::unique_ptr<Extension> instance;
    bool load_started = false;

    explicit ExtensionState(std::unique_ptr<Extension> instance) noexcept
        : instance(std::move(instance)) {}
};

inline void unload_callback(void* user_data) noexcept
{
    std::unique_ptr<ExtensionState> state(static_cast<ExtensionState*>(user_data));
    if (!state->load_started)
        return;
    try {
        state->instance->OnUnload();
    } catch (const std::exception& exception) {
        report_exception("on_unload", exception.what());
    } catch (...) {
        report_exception("on_unload", "unknown exception");
    }
    // The owning pointer destroys member resources even if OnUnload threw.
}

inline void all_loaded_callback(void* user_data) noexcept
{
    auto& state = *static_cast<ExtensionState*>(user_data);
    try {
        state.instance->OnAllExtensionsLoaded();
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
sw_status RunExtension(const sw_init_context* ctx) noexcept
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
        std::unique_ptr<Extension> instance = std::make_unique<T>();
        auto state = std::make_unique<detail::ExtensionState>(std::move(instance));

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
        auto name = detail::c_string(registered->instance->Name(), "extension.name");
        auto version = detail::c_string(registered->instance->Version(), "extension.version");
        auto author = detail::c_string(registered->instance->Author(), "extension.author");
        auto description = detail::c_string(registered->instance->Description(), "extension.description");
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
        auto loaded = registered->instance->OnLoad(sw::Core{
            sw::Memory(raw->memory), sw::Schema(raw->schema),
            sw::Shared(raw->shared), sw::Misc(hot_reloaded != 0)});
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
        return ::sw::RunExtension<__VA_ARGS__>(ctx);               \
    }
