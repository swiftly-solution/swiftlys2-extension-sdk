#pragma once

#include "result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace sw {

class memory;

// Owns bytes allocated by the game. It does not construct C++ objects.
class game_buffer {
public:
    game_buffer() noexcept = default;
    ~game_buffer() { (void)reset(); }

    game_buffer(const game_buffer&) = delete;
    game_buffer& operator=(const game_buffer&) = delete;

    game_buffer(game_buffer&& other) noexcept
        : api_(std::exchange(other.api_, nullptr)),
          data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)) {}

    game_buffer& operator=(game_buffer&& other) noexcept
    {
        if (this != &other) {
            (void)reset();
            api_ = std::exchange(other.api_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    [[nodiscard]] void* data() noexcept { return data_; }
    [[nodiscard]] const void* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::span<std::byte> bytes() noexcept
    {
        return {static_cast<std::byte*>(data_), size_};
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return {static_cast<const std::byte*>(data_), size_};
    }

    [[nodiscard]] result<void> reset() noexcept
    {
        if (!data_)
            return {};
        try {
            const auto status = api_->free(api_, data_);
            if (status != SW_OK)
                return detail::fail(status);
            data_ = nullptr;
            size_ = 0;
            return {};
        } catch (...) {
            return detail::fail(SW_E_FAILED);
        }
    }

    [[nodiscard]] result<void> resize(std::size_t size)
    {
        // Deliberately avoid the allocator's unspecified zero-size resize behavior.
        if (size == 0)
            return reset();
        if (!api_ || !api_->resize)
            return detail::fail(SW_E_INVALID_ARG);
        void* resized = nullptr;
        const auto status = api_->resize(api_, data_, size, &resized);
        if (status != SW_OK)
            return detail::fail(status);
        if (!resized)
            return detail::fail(SW_E_FAILED);
        data_ = resized;
        size_ = size;
        return {};
    }

private:
    friend class memory;
    game_buffer(const sw_memory* api, void* data, std::size_t size) noexcept
        : api_(api), data_(data), size_(size) {}

    const sw_memory* api_ = nullptr;
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

namespace detail {

template<class Fn, bool Vtable>
class basic_hook {
    static_assert(std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>,
                  "Hook signatures must be function pointer types.");
public:
    basic_hook() noexcept = default;
    ~basic_hook() { (void)reset(); }
    basic_hook(const basic_hook&) = delete;
    basic_hook& operator=(const basic_hook&) = delete;

    basic_hook(basic_hook&& other) noexcept
        : api_(std::exchange(other.api_, nullptr)),
          handle_(std::exchange(other.handle_, nullptr)),
          original_(std::exchange(other.original_, nullptr)) {}

    basic_hook& operator=(basic_hook&& other) noexcept
    {
        if (this != &other) {
            (void)reset();
            api_ = std::exchange(other.api_, nullptr);
            handle_ = std::exchange(other.handle_, nullptr);
            original_ = std::exchange(other.original_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
    // The returned function pointer is valid only while this hook remains enabled.
    [[nodiscard]] Fn original() const noexcept { return original_; }

    [[nodiscard]] result<void> reset() noexcept
    {
        if (!handle_)
            return {};
        try {
            sw_status status;
            if constexpr (Vtable)
                status = api_->unhook_vtable(api_, handle_);
            else
                status = api_->unhook_address(api_, handle_);
            if (status != SW_OK)
                return fail(status);
            handle_ = nullptr;
            original_ = nullptr;
            return {};
        } catch (...) {
            return fail(SW_E_FAILED);
        }
    }

private:
    friend class ::sw::memory;
    basic_hook(const sw_memory* api, sw_hook_handle handle, Fn original) noexcept
        : api_(api), handle_(handle), original_(original) {}
    const sw_memory* api_ = nullptr;
    sw_hook_handle handle_ = nullptr;
    Fn original_ = nullptr;
};

} // namespace detail

template<class Fn>
using address_hook = detail::basic_hook<Fn, false>;
template<class Fn>
using vtable_hook = detail::basic_hook<Fn, true>;

class memory {
public:
    explicit memory(const sw_memory* api) noexcept : api_(api) {}

    [[nodiscard]] result<game_buffer> allocate(std::size_t size) const
    {
        if (!api_ || !api_->alloc || !api_->free)
            return detail::fail(SW_E_INVALID_ARG);
        if (size == 0)
            return game_buffer(api_, nullptr, 0);
        void* pointer = nullptr;
        const auto status = api_->alloc(api_, size, &pointer);
        if (status != SW_OK)
            return detail::fail(status);
        if (!pointer)
            return detail::fail(SW_E_FAILED);
        return game_buffer(api_, pointer, size);
    }

    template<class Fn>
    [[nodiscard]] result<address_hook<Fn>> hook_address(void* address, Fn replacement) const
    {
        if (!api_ || !api_->hook_address || !api_->unhook_address || !address || !replacement)
            return detail::fail(SW_E_INVALID_ARG);
        sw_hook_handle handle = nullptr;
        void* original = nullptr;
        const auto status = api_->hook_address(api_, address,
            reinterpret_cast<void*>(replacement), &handle, &original);
        if (status != SW_OK)
            return detail::fail(status);
        if (!handle || !original) {
            if (handle)
                (void)api_->unhook_address(api_, handle);
            return detail::fail(SW_E_FAILED);
        }
        return address_hook<Fn>(api_, handle, reinterpret_cast<Fn>(original));
    }

    template<class Fn>
    [[nodiscard]] result<vtable_hook<Fn>> hook_vtable(
        void* vtable, std::uint32_t index, Fn replacement) const
    {
        if (!api_ || !api_->hook_vtable || !api_->unhook_vtable || !vtable || !replacement)
            return detail::fail(SW_E_INVALID_ARG);
        sw_hook_handle handle = nullptr;
        void* original = nullptr;
        const auto status = api_->hook_vtable(api_, vtable, index,
            reinterpret_cast<void*>(replacement), &handle, &original);
        if (status != SW_OK)
            return detail::fail(status);
        if (!handle || !original) {
            if (handle)
                (void)api_->unhook_vtable(api_, handle);
            return detail::fail(SW_E_FAILED);
        }
        return vtable_hook<Fn>(api_, handle, reinterpret_cast<Fn>(original));
    }

    [[nodiscard]] result<void*> gamedata_signature(std::string_view name) const
    {
        if (!api_ || !api_->gamedata_resolve_signature)
            return detail::fail(SW_E_INVALID_ARG);
        auto text = detail::c_string(name);
        if (!text)
            return std::unexpected(text.error());
        void* address = nullptr;
        const auto status = api_->gamedata_resolve_signature(api_, text->c_str(), &address);
        if (status != SW_OK)
            return detail::fail(status);
        if (!address)
            return detail::fail(SW_E_FAILED);
        return address;
    }

    [[nodiscard]] result<std::uint32_t> gamedata_offset(std::string_view name) const
    {
        if (!api_ || !api_->gamedata_get_offset)
            return detail::fail(SW_E_INVALID_ARG);
        auto text = detail::c_string(name);
        if (!text)
            return std::unexpected(text.error());
        std::uint32_t offset = 0;
        const auto status = api_->gamedata_get_offset(api_, text->c_str(), &offset);
        if (status != SW_OK)
            return detail::fail(status);
        return offset;
    }

    [[nodiscard]] result<void*> resolve_signature(
        std::string_view library, std::string_view pattern) const
    {
        if (!api_ || !api_->resolve_signature)
            return detail::fail(SW_E_INVALID_ARG);
        auto lib = detail::c_string(library);
        auto signature = detail::c_string(pattern);
        if (!lib) return std::unexpected(lib.error());
        if (!signature) return std::unexpected(signature.error());
        void* address = nullptr;
        const auto status = api_->resolve_signature(api_, lib->c_str(), signature->c_str(), &address);
        if (status != SW_OK) return detail::fail(status);
        if (!address) return detail::fail(SW_E_FAILED);
        return address;
    }

    [[nodiscard]] result<void*> find_vtable(
        std::string_view library, std::string_view class_name) const
    {
        if (!api_ || !api_->find_vtable)
            return detail::fail(SW_E_INVALID_ARG);
        auto lib = detail::c_string(library);
        auto name = detail::c_string(class_name);
        if (!lib) return std::unexpected(lib.error());
        if (!name) return std::unexpected(name.error());
        void* address = nullptr;
        const auto status = api_->find_vtable(api_, lib->c_str(), name->c_str(), &address);
        if (status != SW_OK) return detail::fail(status);
        if (!address) return detail::fail(SW_E_FAILED);
        return address;
    }

    [[nodiscard]] result<void*> valve_interface(std::string_view version) const
    {
        if (!api_ || !api_->get_valve_interface)
            return detail::fail(SW_E_INVALID_ARG);
        auto name = detail::c_string(version);
        if (!name) return std::unexpected(name.error());
        void* pointer = nullptr;
        const auto status = api_->get_valve_interface(api_, name->c_str(), &pointer);
        if (status != SW_OK) return detail::fail(status);
        if (!pointer) return detail::fail(SW_E_FAILED);
        return pointer;
    }

private:
    const sw_memory* api_;
};

} // namespace sw
