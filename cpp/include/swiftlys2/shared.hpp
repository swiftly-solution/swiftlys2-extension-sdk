#pragma once

#include "result.hpp"

#include <cstdint>

namespace sw {

/**
 * @brief Shared pointer registry APIs.
 * @note The registry does not transfer ownership of stored objects. Inserting,
 * replacing, or removing a key never copies or frees the pointed-to object.
 * String keys are copied to terminated strings for the host.
 * @see sw_shared
 */
class shared {
public:
    /** @param api Borrowed host shared pointer interface, valid until extension unload. */
    explicit shared(const sw_shared* api) noexcept : api_(api) {}

    /**
     * @brief Get a pointer from the host's shared pointer registry.
     * @param key Key to look up.
     * @return The stored value, which may be nullptr, or an error if the key
     * does not exist or a required interface or callback is unavailable.
     * @note The returned pointer is borrowed; ownership is not transferred.
     * @see sw_shared::get_shared_pointer
     */
    [[nodiscard]] result<void*> get_shared_pointer(std::string_view key) const
    {
        if (!api_ || !api_->get_shared_pointer)
            return detail::fail("shared.get_shared_pointer: shared interface or get_shared_pointer callback is unavailable");
        auto text = detail::c_string(key, "shared.get_shared_pointer: key");
        if (!text) return std::unexpected(text.error());
        void* pointer = nullptr;
        const auto status = api_->get_shared_pointer(api_, text->c_str(), &pointer);
        if (status != SW_OK)
            return detail::fail(status, "shared.get_shared_pointer('" + *text + "')", "a required pointer is null or the key does not exist");
        return pointer; // nullptr is a valid registered value.
    }

    /**
     * @brief Insert or replace a pointer in the host's shared pointer registry.
     * @param key Key to store; copied by the host.
     * @param pointer Value to store, may be nullptr.
     * @return Success, or an error if a required interface or callback is unavailable.
     * @note The object is neither copied nor owned by the registry. Replacing
     * an entry does not free its previous value. Remove published keys before
     * destroying their values.
     * @see sw_shared::set_shared_pointer
     */
    [[nodiscard]] result<void> set_shared_pointer(std::string_view key, void* pointer) const
    {
        if (!api_ || !api_->set_shared_pointer)
            return detail::fail("shared.set_shared_pointer: shared interface or set_shared_pointer callback is unavailable");
        auto text = detail::c_string(key, "shared.set_shared_pointer: key");
        if (!text) return std::unexpected(text.error());
        const auto status = api_->set_shared_pointer(api_, text->c_str(), pointer);
        if (status != SW_OK)
            return detail::fail(status, "shared.set_shared_pointer('" + *text + "')", "shared interface or key is null");
        return {};
    }

    /**
     * @brief Check whether a key exists in the shared pointer registry.
     * @param key Key to check.
     * @return True if the key exists, false otherwise, or an error if a required
     * interface or callback is unavailable.
     * @note A key whose value is nullptr still exists.
     * @see sw_shared::has_shared_pointer
     */
    [[nodiscard]] result<bool> has_shared_pointer(std::string_view key) const
    {
        if (!api_ || !api_->has_shared_pointer)
            return detail::fail("shared.has_shared_pointer: shared interface or has_shared_pointer callback is unavailable");
        auto text = detail::c_string(key, "shared.has_shared_pointer: key");
        if (!text) return std::unexpected(text.error());
        std::int32_t found = 0;
        const auto status = api_->has_shared_pointer(api_, text->c_str(), &found);
        if (status != SW_OK)
            return detail::fail(status, "shared.has_shared_pointer('" + *text + "')", "shared interface, key or output storage is null");
        return found != 0;
    }

    /**
     * @brief Remove a key from the shared pointer registry.
     * @param key Key to remove.
     * @return Success, or an error if the key does not exist or a required
     * interface or callback is unavailable.
     * @note Removing an entry does not free the pointed-to object.
     * @see sw_shared::remove_shared_pointer
     */
    [[nodiscard]] result<void> remove_shared_pointer(std::string_view key) const
    {
        if (!api_ || !api_->remove_shared_pointer)
            return detail::fail("shared.remove_shared_pointer: shared interface or remove_shared_pointer callback is unavailable");
        auto text = detail::c_string(key, "shared.remove_shared_pointer: key");
        if (!text) return std::unexpected(text.error());
        const auto status = api_->remove_shared_pointer(api_, text->c_str());
        if (status != SW_OK)
            return detail::fail(status, "shared.remove_shared_pointer('" + *text + "')", "shared interface or key is null, or the key does not exist");
        return {};
    }

private:
    const sw_shared* api_;
};

} // namespace sw
