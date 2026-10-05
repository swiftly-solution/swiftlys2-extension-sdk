#pragma once

#include "memory.hpp"
#include "schema.hpp"
#include "shared.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <utility>

namespace sw {

// Constructed by the SDK. All interfaces are borrowed until extension unload.
struct core {
    const sw::memory memory;
    const sw::schema schema;
    const sw::shared_registry shared;
    const bool hot_reloaded;
};

class extension {
public:
    virtual ~extension() noexcept = default;

    // Return string literals or views into this object's members.
    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view version() const noexcept = 0;
    virtual std::string_view author() const noexcept { return {}; }
    virtual std::string_view description() const noexcept { return {}; }

    virtual result<void> on_load(core&) = 0;
    // Called initially, and again after successful extension loads/unloads.
    virtual void on_all_extensions_loaded(core&) {}
    // Also called after failed/throwing on_load, before member resources are destroyed.
    virtual void on_unload(core&) {}
};

namespace detail {

inline void report_exception(const char* phase, const char* message) noexcept
{
    std::fprintf(stderr, "SwiftlyS2 SDK: %s: %s\n", phase, message);
}

struct extension_state {
    // Destroy the extension (and its resources) before destroying core.
    sw::core api;
    std::unique_ptr<extension> instance;
    bool load_started = false;

    extension_state(sw::core api, std::unique_ptr<extension> instance) noexcept
        : api(api), instance(std::move(instance)) {}
};

inline void unload_callback(void* user_data) noexcept
{
    std::unique_ptr<extension_state> state(static_cast<extension_state*>(user_data));
    if (!state->load_started)
        return;
    try {
        state->instance->on_unload(state->api);
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
        state.instance->on_all_extensions_loaded(state.api);
    } catch (const std::exception& exception) {
        report_exception("on_all_extensions_loaded", exception.what());
    } catch (...) {
        report_exception("on_all_extensions_loaded", "unknown exception");
    }
}

} // namespace detail

template<class T>
sw_status run_extension(const sw_init_context* ctx) noexcept
{
    try {
        if (!ctx || !ctx->core)
            return SW_E_INVALID_ARG;
        sw_core* raw = nullptr;
        auto status = ctx->core(ctx, SW_IFACE_CORE, &raw);
        if (status != SW_OK)
            return status;
        if (!raw || !raw->init || !raw->memory || !raw->schema || !raw->shared)
            return SW_E_INVALID_ARG;

        const auto* init = raw->init;
        if (!init->set_info || !init->is_hotreloaded || !init->set_unload_callback ||
            !init->set_on_all_extensions_loaded_callback)
            return SW_E_INVALID_ARG;
        std::int32_t hot_reloaded = 0;
        status = init->is_hotreloaded(init, &hot_reloaded);
        if (status != SW_OK)
            return status;

        // This conversion requires public inheritance, a concrete type and default construction.
        std::unique_ptr<extension> instance = std::make_unique<T>();
        auto state = std::make_unique<detail::extension_state>(
            sw::core{sw::memory(raw->memory), sw::schema(raw->schema),
                     sw::shared_registry(raw->shared), hot_reloaded != 0},
            std::move(instance));

        status = init->set_unload_callback(init, detail::unload_callback, state.get());
        if (status != SW_OK)
            return status;

        // Once registered, the host calls our unload trampoline on BOTH success and failure.
        // The trampoline now owns state. Never delete it again on an initialization error.
        auto* registered = state.release();
        status = init->set_on_all_extensions_loaded_callback(
            init, detail::all_loaded_callback, registered);
        if (status != SW_OK)
            return status;

        // Snapshot each getter immediately, then pass terminated strings for the host to copy.
        auto name = detail::c_string(registered->instance->name());
        auto version = detail::c_string(registered->instance->version());
        auto author = detail::c_string(registered->instance->author());
        auto description = detail::c_string(registered->instance->description());
        if (!name || !version || !author || !description || name->empty() || version->empty())
            return SW_E_INVALID_ARG;
        status = init->set_info(init, name->c_str(), version->c_str(),
            author->empty() ? nullptr : author->c_str(),
            description->empty() ? nullptr : description->c_str());
        if (status != SW_OK)
            return status;

        registered->load_started = true;
        auto loaded = registered->instance->on_load(registered->api);
        if (!loaded)
            return loaded.error().code == SW_OK ? SW_E_FAILED : loaded.error().code;
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

// Expand exactly once, in one .cpp file. The exported C entry must not be inline.
#define SW_DEFINE_EXTENSION(...)                                   \
    SW_API sw_status sw_extension_init(const sw_init_context* ctx)  \
    {                                                             \
        return ::sw::run_extension<__VA_ARGS__>(ctx);               \
    }
