#pragma once

#include "result.hpp"

#include <cstdint>

namespace sw {

// Registry values are borrowed. Registering/removing a key never owns/frees its object.
class shared_registry {
public:
    explicit shared_registry(const sw_shared* api) noexcept : api_(api) {}

    [[nodiscard]] result<void*> get(std::string_view key) const
    {
        if (!api_ || !api_->get_shared_pointer)
            return detail::fail(SW_E_INVALID_ARG);
        auto text = detail::c_string(key);
        if (!text) return std::unexpected(text.error());
        void* pointer = nullptr;
        const auto status = api_->get_shared_pointer(api_, text->c_str(), &pointer);
        if (status != SW_OK) return detail::fail(status);
        return pointer; // nullptr is a valid registered value.
    }

    [[nodiscard]] result<void> set(std::string_view key, void* pointer) const
    {
        if (!api_ || !api_->set_shared_pointer)
            return detail::fail(SW_E_INVALID_ARG);
        auto text = detail::c_string(key);
        if (!text) return std::unexpected(text.error());
        return detail::check(api_->set_shared_pointer(api_, text->c_str(), pointer));
    }

    [[nodiscard]] result<bool> has(std::string_view key) const
    {
        if (!api_ || !api_->has_shared_pointer)
            return detail::fail(SW_E_INVALID_ARG);
        auto text = detail::c_string(key);
        if (!text) return std::unexpected(text.error());
        std::int32_t found = 0;
        const auto status = api_->has_shared_pointer(api_, text->c_str(), &found);
        if (status != SW_OK) return detail::fail(status);
        return found != 0;
    }

    [[nodiscard]] result<void> remove(std::string_view key) const
    {
        if (!api_ || !api_->remove_shared_pointer)
            return detail::fail(SW_E_INVALID_ARG);
        auto text = detail::c_string(key);
        if (!text) return std::unexpected(text.error());
        return detail::check(api_->remove_shared_pointer(api_, text->c_str()));
    }

private:
    const sw_shared* api_;
};

} // namespace sw
