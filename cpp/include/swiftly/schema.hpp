#pragma once

#include "result.hpp"

#include <cstdint>

namespace sw {

class schema {
public:
    explicit schema(const sw_schema* api) noexcept : api_(api) {}

    [[nodiscard]] result<std::uint32_t> offset(
        std::string_view class_name, std::string_view field_name) const
    {
        if (!api_ || !api_->get_offset)
            return detail::fail(SW_E_INVALID_ARG);
        auto class_text = detail::c_string(class_name);
        auto field_text = detail::c_string(field_name);
        if (!class_text) return std::unexpected(class_text.error());
        if (!field_text) return std::unexpected(field_text.error());
        std::uint32_t value = 0;
        const auto status = api_->get_offset(api_, class_text->c_str(), field_text->c_str(), &value);
        if (status != SW_OK) return detail::fail(status);
        return value;
    }

    // Call on the game thread, after modifying a live object's field.
    [[nodiscard]] result<void> set_state_changed(
        void* object, std::string_view class_name, std::string_view field_name) const
    {
        if (!api_ || !api_->set_state_changed || !object)
            return detail::fail(SW_E_INVALID_ARG);
        auto class_text = detail::c_string(class_name);
        auto field_text = detail::c_string(field_name);
        if (!class_text) return std::unexpected(class_text.error());
        if (!field_text) return std::unexpected(field_text.error());
        return detail::check(api_->set_state_changed(
            api_, object, class_text->c_str(), field_text->c_str()));
    }

private:
    const sw_schema* api_;
};

} // namespace sw
