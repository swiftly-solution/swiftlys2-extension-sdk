#pragma once

#include "result.hpp"

#include <cstdint>

namespace sw {

/**
 * @brief Schema field lookup and network state change APIs.
 * @note The interface belongs to the host. Class and field names are case-sensitive.
 * Use the class that declares inherited fields.
 * @see sw_schema
 */
class Schema {
public:
    /** @param api Borrowed host schema interface, valid until extension unload. */
    explicit Schema(const sw_schema* api) noexcept : api_(api) {}

    /**
     * @brief Look up a schema field's byte offset within its declaring class.
     * @param class_name Case-sensitive schema class name.
     * @param field_name Case-sensitive schema field name.
     * @return The field's byte offset, including zero, or an error if the class
     * or field does not exist or a required interface or callback is unavailable.
     * @note For inherited fields, use the class that declares the field.
     * @see sw_schema::get_offset
     */
    [[nodiscard]] Result<std::uint32_t> GetOffset(
        std::string_view class_name, std::string_view field_name) const
    {
        if (!api_ || !api_->get_offset)
            return detail::fail("schema.get_offset: schema interface or get_offset callback is unavailable");
        auto class_text = detail::c_string(class_name, "schema.get_offset: class_name");
        auto field_text = detail::c_string(field_name, "schema.get_offset: field_name");
        if (!class_text) return std::unexpected(class_text.error());
        if (!field_text) return std::unexpected(field_text.error());
        std::uint32_t value = 0;
        const auto status = api_->get_offset(api_, class_text->c_str(), field_text->c_str(), &value);
        if (status != SW_OK)
            return detail::fail(status, "schema.get_offset('" + *class_text + "', '" + *field_text + "')",
                "a required pointer is null or the class or field does not exist; names are case-sensitive and must identify the declaring class");
        return value;
    }

    /**
     * @brief Notify the game that a schema field's value has changed.
     * @param object Live instance of the declaring class, must not be nullptr.
     * @param class_name Case-sensitive name of the class declaring the field.
     * @param field_name Case-sensitive schema field name.
     * @return Success, or an error if the object is null, the class or field
     * does not exist, or a required interface or callback is unavailable.
     * @note Call after modifying the field, on the game thread. For inherited
     * fields, use the class that declares the field.
     * @see sw_schema::set_state_changed
     */
    [[nodiscard]] Result<void> SetStateChanged(
        void* object, std::string_view class_name, std::string_view field_name) const
    {
        if (!api_ || !api_->set_state_changed)
            return detail::fail("schema.set_state_changed: schema interface or set_state_changed callback is unavailable");
        if (!object)
            return detail::fail("schema.set_state_changed: object is null; a live instance of the declaring class is required");
        auto class_text = detail::c_string(class_name, "schema.set_state_changed: class_name");
        auto field_text = detail::c_string(field_name, "schema.set_state_changed: field_name");
        if (!class_text) return std::unexpected(class_text.error());
        if (!field_text) return std::unexpected(field_text.error());
        const auto status = api_->set_state_changed(api_, object, class_text->c_str(), field_text->c_str());
        if (status != SW_OK)
            return detail::fail(status, "schema.set_state_changed('" + *class_text + "', '" + *field_text + "')",
                "a required pointer is null or the class or field does not exist; names are case-sensitive and must identify the declaring class");
        return {};
    }

private:
    const sw_schema* api_;
};

} // namespace sw
