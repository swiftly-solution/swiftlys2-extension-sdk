#pragma once

#include "result.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace sw {

class Memory;

namespace detail {

/**
 * @brief Move-only owner of a live function or virtual function hook.
 * @tparam Fn Function pointer with the target signature and calling convention.
 * @tparam Vtable Select the matching virtual function unhook operation.
 * @note The original trampoline is valid only until reset or destruction.
 * Release the hook while the underlying memory interface is still alive.
 */
template<class Fn, bool Vtable>
class BasicHook {
    static_assert(std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>,
                  "Hook signatures must be function pointer types.");
public:
    /** @brief Construct an empty hook with no handle or original trampoline. */
    BasicHook() noexcept = default;
    /** @brief Disable and destroy a live hook using its matching unhook callback. */
    ~BasicHook() { reset_noexcept(); }
    BasicHook(const BasicHook&) = delete;
    BasicHook& operator=(const BasicHook&) = delete;

    /** @brief Transfer ownership and leave the source empty. */
    BasicHook(BasicHook&& other) noexcept
        : api_(std::exchange(other.api_, nullptr)),
          handle_(std::exchange(other.handle_, nullptr)),
          original_(std::exchange(other.original_, nullptr)) {}

    /** @brief Release the current hook, then transfer ownership from the source. */
    BasicHook& operator=(BasicHook&& other) noexcept
    {
        if (this != &other) {
            reset_noexcept();
            api_ = std::exchange(other.api_, nullptr);
            handle_ = std::exchange(other.handle_, nullptr);
            original_ = std::exchange(other.original_, nullptr);
        }
        return *this;
    }

    /** @brief Whether this object owns a live hook handle. */
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
    /**
     * @brief Get the trampoline for calling the original function.
     * @return The original function, or nullptr for an empty hook.
     * @note The function is invalid after the hook is removed.
     */
    [[nodiscard]] Fn Original() const noexcept { return original_; }

    /**
     * @brief Disable and destroy the hook; do nothing if it is already empty.
     * @return Success, or a detailed error from the matching unhook callback.
     * @note Success invalidates the handle and original trampoline. On failure,
     * this object retains its handle so cleanup can be retried.
     * @see sw_memory::unhook_address
     * @see sw_memory::unhook_vtable
     */
    [[nodiscard]] Result<void> Reset()
    {
        if (!handle_)
            return {};
        constexpr auto operation = Vtable ? "memory.unhook_vtable" : "memory.unhook_address";
        try {
            sw_status status;
            if constexpr (Vtable)
                status = api_->unhook_vtable(api_, handle_);
            else
                status = api_->unhook_address(api_, handle_);
            if (status != SW_OK)
                return fail(status, operation, "memory interface or hook handle is null");
            handle_ = nullptr;
            original_ = nullptr;
            return {};
        } catch (const std::exception& exception) {
            return fail(SW_E_FAILED, std::string(operation) + ": host callback threw: " + exception.what());
        } catch (...) {
            return fail(SW_E_FAILED, std::string(operation) + ": host callback threw an unknown exception");
        }
    }

private:
    // Formatting string errors can allocate; destruction and moves must not throw.
    void reset_noexcept() noexcept
    {
        try { (void)Reset(); } catch (...) {}
    }

    friend class ::sw::Memory;
    BasicHook(const sw_memory* api, sw_hook_handle handle, Fn original) noexcept
        : api_(api), handle_(handle), original_(original) {}
    const sw_memory* api_ = nullptr;
    sw_hook_handle handle_ = nullptr;
    Fn original_ = nullptr;
};

} // namespace detail

/** @brief Owner of a function address hook, released with unhook_address. */
template<class Fn>
using AddressHook = detail::BasicHook<Fn, false>;
/** @brief Owner of a virtual function hook, released with unhook_vtable. */
template<class Fn>
using VTableHook = detail::BasicHook<Fn, true>;

/**
 * @brief Memory allocation, hooks, and address lookup APIs.
 * @note The underlying interface belongs to the host and must not be freed.
 * String arguments are copied to terminated strings for the host.
 * @see sw_memory
 */
class Memory {
public:
    /** @param api Borrowed host memory interface, valid until extension unload. */
    explicit Memory(const sw_memory* api) noexcept : api_(api) {}

    /**
     * @brief Allocate memory through the game's allocator.
     * @param size Number of bytes to allocate.
     * @return A non-null allocation, or an error if the allocator returns null
     * or the required memory interface or callback is unavailable.
     * @note Memory is not initialized. The caller owns the bytes and must release
     * them with Free(), or resize them with Resize(). Zero-size behavior follows
     * the game's allocator.
     * @see sw_memory::alloc
     */
    [[nodiscard]] Result<void*> Alloc(std::size_t size) const
    {
        if (!api_ || !api_->alloc)
            return detail::fail(SW_E_FAILED, "memory.alloc: memory interface or alloc callback is unavailable");
        void* pointer = nullptr;
        const auto status = api_->alloc(api_, size, &pointer);
        if (status != SW_OK || !pointer)
            return detail::fail(status == SW_OK ? SW_E_FAILED : status, "memory.alloc",
                "memory interface or output storage is null",
                "game allocator returned null for " + std::to_string(size) + " bytes");
        return pointer;
    }

    /**
     * @brief Release memory allocated through the game's allocator.
     * @param pointer Live allocation returned by Alloc() or Resize(), or nullptr.
     * @return Success, or an error if the required interface or callback is unavailable.
     * @see sw_memory::free
     */
    [[nodiscard]] Result<void> Free(void* pointer) const
    {
        if (!api_ || !api_->free)
            return detail::fail(SW_E_FAILED, "memory.free: memory interface or free callback is unavailable");
        return detail::check(api_->free(api_, pointer), "memory.free", "memory interface is null");
    }

    /**
     * @brief Resize an allocation through the game's allocator.
     * @param pointer Live allocation returned by Alloc() or Resize(), or nullptr
     * to allocate a new block.
     * @param size New size in bytes.
     * @return The allocator's result, or an error if a nonzero allocation fails
     * or the required interface or callback is unavailable.
     * @note Replace the original pointer only on success. A nonzero resize that
     * returns null leaves the original allocation valid. Successful resize may
     * move the allocation and invalidate the original pointer. Zero-size behavior
     * follows the game's allocator; a successful result may contain nullptr.
     * @see sw_memory::resize
     */
    [[nodiscard]] Result<void*> Resize(void* pointer, std::size_t size) const
    {
        if (!api_ || !api_->resize)
            return detail::fail(SW_E_FAILED, "memory.resize: memory interface or resize callback is unavailable");
        void* resized = nullptr;
        const auto status = api_->resize(api_, pointer, size, &resized);
        if (status != SW_OK)
            return detail::fail(status, "memory.resize", "memory interface or output storage is null");
        if (size != 0 && !resized)
            return detail::fail(SW_E_FAILED, "memory.resize: game allocator returned null for " +
                std::to_string(size) + " bytes; the original allocation remains valid");
        return resized;
    }

    /**
     * @brief Create and enable a hook at a function address.
     * @tparam Fn Function pointer matching the target signature and calling convention.
     * @param address Function address to hook, must not be nullptr.
     * @param replacement Replacement function, must not be nullptr.
     * @return An owning hook with its original trampoline, or a detailed error.
     * @note A null original trampoline means the hook was not enabled. The
     * original function must not be used after reset or destruction of the hook.
     * @see sw_memory::hook_address
     */
    template<class Fn>
    [[nodiscard]] Result<AddressHook<Fn>> HookAddress(void* address, Fn replacement) const
    {
        if (!api_ || !api_->hook_address || !api_->unhook_address)
            return detail::fail(SW_E_FAILED, "memory.hook_address: memory interface, hook_address or unhook_address callback is unavailable");
        if (!address)
            return detail::fail(SW_E_INVALID_ARG, "memory.hook_address: function address is null");
        if (!replacement)
            return detail::fail(SW_E_INVALID_ARG, "memory.hook_address: replacement function is null");
        sw_hook_handle handle = nullptr;
        void* original = nullptr;
        const auto status = api_->hook_address(api_, address,
            reinterpret_cast<void*>(replacement), &handle, &original);
        if (status != SW_OK)
            return detail::fail(status, "memory.hook_address", "a required pointer is null",
                "original trampoline is null; the hook was not enabled");
        if (!handle || !original) {
            if (handle)
                (void)api_->unhook_address(api_, handle);
            return detail::fail(SW_E_FAILED, "memory.hook_address: host returned success with a null hook handle or original trampoline");
        }
        return AddressHook<Fn>(api_, handle, reinterpret_cast<Fn>(original));
    }

    /**
     * @brief Create and enable a hook for a virtual function table entry.
     * @tparam Fn Target signature and calling convention, including the instance argument.
     * @param vtable Virtual function table address, must not be nullptr. Pass
     * the table itself, rather than an object instance.
     * @param index Zero-based entry index, not a byte offset. Must identify a
     * valid entry and must not exceed INT_MAX.
     * @param replacement Replacement function, must not be nullptr.
     * @return An owning hook with its original trampoline, or a detailed error.
     * @note A null original trampoline means the hook was not enabled. The
     * original function must not be used after reset or destruction of the hook.
     * @see sw_memory::hook_vtable
     */
    template<class Fn>
    [[nodiscard]] Result<VTableHook<Fn>> HookVTable(
        void* vtable, std::uint32_t index, Fn replacement) const
    {
        if (!api_ || !api_->hook_vtable || !api_->unhook_vtable)
            return detail::fail(SW_E_FAILED, "memory.hook_vtable: memory interface, hook_vtable or unhook_vtable callback is unavailable");
        if (!vtable)
            return detail::fail(SW_E_INVALID_ARG, "memory.hook_vtable: vtable address is null");
        if (!replacement)
            return detail::fail(SW_E_INVALID_ARG, "memory.hook_vtable: replacement function is null");
        if (index > static_cast<std::uint32_t>(INT_MAX))
            return detail::fail(SW_E_INVALID_ARG, "memory.hook_vtable: entry index " + std::to_string(index) + " exceeds INT_MAX");
        sw_hook_handle handle = nullptr;
        void* original = nullptr;
        const auto status = api_->hook_vtable(api_, vtable, index,
            reinterpret_cast<void*>(replacement), &handle, &original);
        if (status != SW_OK)
            return detail::fail(status, "memory.hook_vtable", "a required pointer is null or the entry index exceeds INT_MAX",
                "original trampoline is null; the hook was not enabled");
        if (!handle || !original) {
            if (handle)
                (void)api_->unhook_vtable(api_, handle);
            return detail::fail(SW_E_FAILED, "memory.hook_vtable: host returned success with a null hook handle or original trampoline");
        }
        return VTableHook<Fn>(api_, handle, reinterpret_cast<Fn>(original));
    }

    /**
     * @brief Look up a signature address from the host's loaded gamedata.
     * @param name Gamedata signature name.
     * @return A non-null stored address, or an error if the name does not exist,
     * its stored address is null, or a required interface or callback is unavailable.
     * @note The address is borrowed; do not release it with the game allocator.
     * @see sw_memory::gamedata_resolve_signature
     */
    [[nodiscard]] Result<void*> GameDataResolveSignature(std::string_view name) const
    {
        if (!api_ || !api_->gamedata_resolve_signature)
            return detail::fail(SW_E_FAILED, "memory.gamedata_resolve_signature: memory interface or gamedata_resolve_signature callback is unavailable");
        auto text = detail::c_string(name, "memory.gamedata_resolve_signature: name");
        if (!text)
            return std::unexpected(text.error());
        void* address = nullptr;
        const auto status = api_->gamedata_resolve_signature(api_, text->c_str(), &address);
        if (status == SW_OK && address)
            return address;
        return detail::fail(status == SW_OK ? SW_E_FAILED : status,
            "memory.gamedata_resolve_signature('" + *text + "')",
            "a required pointer is null or the signature name does not exist", "stored signature address is null");
    }

    /**
     * @brief Look up an offset from the host's loaded gamedata.
     * @param name Gamedata offset name.
     * @return The stored offset, including zero, or an error if the name does
     * not exist or a required interface or callback is unavailable.
     * @see sw_memory::gamedata_get_offset
     */
    [[nodiscard]] Result<std::uint32_t> GameDataGetOffset(std::string_view name) const
    {
        if (!api_ || !api_->gamedata_get_offset)
            return detail::fail(SW_E_FAILED, "memory.gamedata_get_offset: memory interface or gamedata_get_offset callback is unavailable");
        auto text = detail::c_string(name, "memory.gamedata_get_offset: name");
        if (!text)
            return std::unexpected(text.error());
        std::uint32_t offset = 0;
        const auto status = api_->gamedata_get_offset(api_, text->c_str(), &offset);
        if (status != SW_OK)
            return detail::fail(status, "memory.gamedata_get_offset('" + *text + "')",
                "a required pointer is null or the offset name does not exist");
        return offset;
    }

    /**
     * @brief Scan a library for a signature and resolve its runtime address.
     * @param library Binary name, such as "server" or "engine2".
     * @param pattern Pattern accepted by S2BinLib.
     * @return A non-null matched address, or an error if the scan fails, no
     * address is resolved, or a required interface or callback is unavailable.
     * @note The address is borrowed.
     * @see sw_memory::resolve_signature
     */
    [[nodiscard]] Result<void*> ResolveSignature(
        std::string_view library, std::string_view pattern) const
    {
        if (!api_ || !api_->resolve_signature)
            return detail::fail(SW_E_FAILED, "memory.resolve_signature: memory interface or resolve_signature callback is unavailable");
        auto lib = detail::c_string(library, "memory.resolve_signature: library");
        auto signature = detail::c_string(pattern, "memory.resolve_signature: pattern");
        if (!lib) return std::unexpected(lib.error());
        if (!signature) return std::unexpected(signature.error());
        void* address = nullptr;
        const auto status = api_->resolve_signature(api_, lib->c_str(), signature->c_str(), &address);
        if (status == SW_OK && address)
            return address;
        const auto operation = "memory.resolve_signature('" + *lib + "', '" + *signature + "')";
        if (status != SW_OK)
            return detail::fail(status, operation, "a required pointer is null", "signature scan failed or no address was resolved");
        return detail::fail(SW_E_FAILED, operation, {}, "signature scan returned a null address");
    }

    /**
     * @brief Find a class's virtual function table in a library.
     * @param library Binary name, such as "server" or "engine2".
     * @param class_name Class name used to identify the virtual function table.
     * @return A non-null table address, or an error if lookup fails, no address
     * is resolved, or a required interface or callback is unavailable.
     * @note The table belongs to the library and must not be freed.
     * @see sw_memory::find_vtable
     * @see HookVTable
     */
    [[nodiscard]] Result<void*> FindVTable(
        std::string_view library, std::string_view class_name) const
    {
        if (!api_ || !api_->find_vtable)
            return detail::fail(SW_E_FAILED, "memory.find_vtable: memory interface or find_vtable callback is unavailable");
        auto lib = detail::c_string(library, "memory.find_vtable: library");
        auto name = detail::c_string(class_name, "memory.find_vtable: class_name");
        if (!lib) return std::unexpected(lib.error());
        if (!name) return std::unexpected(name.error());
        void* address = nullptr;
        const auto status = api_->find_vtable(api_, lib->c_str(), name->c_str(), &address);
        if (status == SW_OK && address)
            return address;
        const auto operation = "memory.find_vtable('" + *lib + "', '" + *name + "')";
        if (status != SW_OK)
            return detail::fail(status, operation, "a required pointer is null", "vtable lookup failed or no address was resolved");
        return detail::fail(SW_E_FAILED, operation, {}, "vtable lookup returned a null address");
    }

    /**
     * @brief Look up a Valve interface.
     * @param version Interface version name.
     * @return A non-null interface, or an error if lookup fails or a required
     * interface or callback is unavailable.
     * @note The interface belongs to the game and must not be freed.
     * @see sw_memory::get_valve_interface
     */
    [[nodiscard]] Result<void*> GetValveInterface(std::string_view version) const
    {
        if (!api_ || !api_->get_valve_interface)
            return detail::fail(SW_E_FAILED, "memory.get_valve_interface: memory interface or get_valve_interface callback is unavailable");
        auto name = detail::c_string(version, "memory.get_valve_interface: version");
        if (!name) return std::unexpected(name.error());
        void* pointer = nullptr;
        const auto status = api_->get_valve_interface(api_, name->c_str(), &pointer);
        if (status == SW_OK && pointer)
            return pointer;
        const auto operation = "memory.get_valve_interface('" + *name + "')";
        if (status != SW_OK)
            return detail::fail(status, operation, "a required pointer is null", "Valve interface lookup failed");
        return detail::fail(SW_E_FAILED, operation, {}, "Valve interface lookup returned null");
    }

private:
    const sw_memory* api_;
};

} // namespace sw
