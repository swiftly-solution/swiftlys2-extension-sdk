#include <swiftlys2/sdk.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
    std::abort(); \
} } while (false)

namespace {

struct host;
host* active = nullptr;
int original(int value) { return value + 1; }
int replacement(int value) { return value + 2; }
using function = int (*)(int);

struct host {
    bool initializing = true;
    bool hot_reloaded = false;
    bool fail_resize = false;
    bool fail_alloc = false;
    bool broken_alloc = false;
    bool fail_hook = false;
    bool broken_hook = false;
    sw_status query_status = SW_OK;
    sw_status offset_status = SW_OK;
    sw_status metadata_status = SW_OK;
    sw_status unload_registration_status = SW_OK;
    sw_status all_registration_status = SW_OK;
    sw_extension_callback_fn unload_fn = nullptr;
    sw_extension_callback_fn all_fn = nullptr;
    void* unload_data = nullptr;
    void* all_data = nullptr;
    std::string name;
    std::string version;
    bool author_absent = false;
    bool description_absent = false;
    std::map<void*, std::size_t> allocations;
    std::unordered_set<void*> address_hooks;
    std::unordered_set<void*> vtable_hooks;
    std::map<std::string, void*> registry;
    int frees = 0;
    int alloc_calls = 0;
    int resize_calls = 0;
    std::uint64_t last_alloc_size = 0;
    std::uint64_t last_resize_size = 0;
    int address_unhooks = 0;
    int vtable_unhooks = 0;
    int state_changes = 0;
    int hot_queries = 0;
    int object = 0;

    static sw_status query(const sw_init_context* self, const char* version, sw_core** out)
    {
        CHECK(self == &active->context);
        CHECK(active->initializing);
        CHECK(std::strcmp(version, SW_IFACE_CORE) == 0);
        *out = active->query_status == SW_OK ? &active->core : nullptr;
        return active->query_status;
    }
    static sw_status info(const sw_init* self, const char* name, const char* version,
                          const char* author, const char* description)
    {
        CHECK(self == &active->init);
        CHECK(active->initializing);
        active->name = name;
        active->version = version;
        active->author_absent = author == nullptr;
        active->description_absent = description == nullptr;
        return active->metadata_status;
    }
    static sw_status hot(const sw_init* self, std::int32_t* out)
    {
        CHECK(self == &active->init);
        CHECK(active->initializing);
        ++active->hot_queries;
        *out = active->hot_reloaded;
        return SW_OK;
    }
    static sw_status set_unload(const sw_init* self, sw_extension_callback_fn fn, void* data)
    {
        CHECK(self == &active->init);
        CHECK(active->initializing);
        if (active->unload_registration_status != SW_OK)
            return active->unload_registration_status;
        active->unload_fn = fn;
        active->unload_data = data;
        return SW_OK;
    }
    static sw_status set_all(const sw_init* self, sw_extension_callback_fn fn, void* data)
    {
        CHECK(self == &active->init);
        CHECK(active->initializing);
        if (active->all_registration_status != SW_OK)
            return active->all_registration_status;
        active->all_fn = fn;
        active->all_data = data;
        return SW_OK;
    }
    static sw_status alloc(const sw_memory* self, std::uint64_t size, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        ++active->alloc_calls;
        active->last_alloc_size = size;
        *static_cast<void**>(out) = nullptr;
        if (active->fail_alloc) return SW_E_FAILED;
        if (active->broken_alloc) return SW_OK;
        auto* pointer = std::malloc(std::max<std::size_t>(static_cast<std::size_t>(size), 1));
        *static_cast<void**>(out) = pointer;
        if (!pointer) return SW_E_FAILED;
        active->allocations.emplace(pointer, size);
        return SW_OK;
    }
    static sw_status free(const sw_memory* self, void* pointer)
    {
        CHECK(self == &active->memory);
        if (!pointer) return SW_OK;
        CHECK(active->allocations.erase(pointer) == 1);
        std::free(pointer);
        ++active->frees;
        return SW_OK;
    }
    static sw_status resize(const sw_memory* self, void* pointer, std::uint64_t size, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        ++active->resize_calls;
        active->last_resize_size = size;
        *static_cast<void**>(out) = nullptr;
        // This fake allocator frees on resize(0); the SDK must forward that request.
        if (size == 0) return free(self, pointer);
        if (active->fail_resize) return SW_OK;
        auto* resized = std::malloc(static_cast<std::size_t>(size));
        if (!resized) return SW_OK;
        if (pointer) {
            const auto old_size = active->allocations.at(pointer);
            std::memcpy(resized, pointer, std::min(old_size, static_cast<std::size_t>(size)));
            active->allocations.erase(pointer);
            std::free(pointer);
        }
        active->allocations.emplace(resized, size);
        *static_cast<void**>(out) = resized;
        return SW_OK;
    }
    static sw_status hook(const sw_memory* self, void*, void*, sw_hook_handle* handle,
                          sw_ptr_out out, bool vtable)
    {
        CHECK(self == &active->memory);
        *handle = nullptr;
        *static_cast<void**>(out) = nullptr;
        if (active->fail_hook) return SW_E_FAILED;
        *handle = new int(1);
        (vtable ? active->vtable_hooks : active->address_hooks).insert(*handle);
        if (!active->broken_hook)
            *static_cast<void**>(out) = reinterpret_cast<void*>(&original);
        return SW_OK;
    }
    static sw_status hook_address(const sw_memory* self, void* address, void* callback,
                                  sw_hook_handle* handle, sw_ptr_out out)
    {
        return hook(self, address, callback, handle, out, false);
    }
    static sw_status hook_vtable(const sw_memory* self, void* address, std::uint32_t index,
                                 void* callback, sw_hook_handle* handle, sw_ptr_out out)
    {
        CHECK(index == 2);
        return hook(self, address, callback, handle, out, true);
    }
    static sw_status unhook_address(const sw_memory* self, sw_hook_handle handle)
    {
        CHECK(self == &active->memory);
        CHECK(active->address_hooks.erase(handle) == 1);
        delete static_cast<int*>(handle);
        ++active->address_unhooks;
        return SW_OK;
    }
    static sw_status unhook_vtable(const sw_memory* self, sw_hook_handle handle)
    {
        CHECK(self == &active->memory);
        CHECK(active->vtable_hooks.erase(handle) == 1);
        delete static_cast<int*>(handle);
        ++active->vtable_unhooks;
        return SW_OK;
    }
    static sw_status signature(const sw_memory* self, const char* name, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        if (std::strcmp(name, "null") == 0) {
            *static_cast<void**>(out) = nullptr;
            return SW_E_FAILED;
        }
        if (std::strcmp(name, "broken") == 0) {
            *static_cast<void**>(out) = nullptr;
            return SW_OK;
        }
        if (std::strcmp(name, "sig") != 0) return SW_E_INVALID_ARG;
        *static_cast<void**>(out) = reinterpret_cast<void*>(&original);
        return SW_OK;
    }
    static sw_status offset(const sw_memory* self, const char* name, std::uint32_t* out)
    {
        CHECK(self == &active->memory);
        if (active->offset_status != SW_OK) return active->offset_status;
        if (std::strcmp(name, "offset") != 0) return SW_E_INVALID_ARG;
        *out = 0;
        return SW_OK;
    }
    static sw_status scan(const sw_memory* self, const char* lib, const char* pattern, sw_ptr_out out)
    {
        if (std::strcmp(lib, "server") || std::strcmp(pattern, "sig"))
            return SW_E_FAILED;
        return signature(self, pattern, out);
    }
    static sw_status vtable(const sw_memory* self, const char* lib, const char* class_name, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        if (std::strcmp(lib, "server") || std::strcmp(class_name, "Entity"))
            return SW_E_FAILED;
        *static_cast<void**>(out) = &active->object;
        return SW_OK;
    }
    static sw_status valve(const sw_memory* self, const char* version, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        if (std::strcmp(version, "Example001")) return SW_E_FAILED;
        *static_cast<void**>(out) = &active->object;
        return SW_OK;
    }
    static sw_status schema_offset(const sw_schema* self, const char* class_name,
                                   const char* field, std::uint32_t* out)
    {
        CHECK(self == &active->schema);
        if (std::strcmp(class_name, "CBaseEntity") || std::strcmp(field, "m_iHealth"))
            return SW_E_INVALID_ARG;
        *out = 0;
        return SW_OK;
    }
    static sw_status changed(const sw_schema* self, void* object, const char* class_name, const char* field)
    {
        CHECK(object == &active->object);
        std::uint32_t out;
        const auto status = schema_offset(self, class_name, field, &out);
        if (status == SW_OK) ++active->state_changes;
        return status;
    }
    static sw_status get(const sw_shared* self, const char* key, sw_ptr_out out)
    {
        CHECK(self == &active->shared);
        auto found = active->registry.find(key);
        if (found == active->registry.end()) return SW_E_INVALID_ARG;
        *static_cast<void**>(out) = found->second;
        return SW_OK;
    }
    static sw_status set(const sw_shared* self, const char* key, void* pointer)
    {
        CHECK(self == &active->shared);
        active->registry.insert_or_assign(key, pointer);
        return SW_OK;
    }
    static sw_status has(const sw_shared* self, const char* key, std::int32_t* out)
    {
        CHECK(self == &active->shared);
        *out = active->registry.contains(key);
        return SW_OK;
    }
    static sw_status remove(const sw_shared* self, const char* key)
    {
        CHECK(self == &active->shared);
        return active->registry.erase(key) ? SW_OK : SW_E_INVALID_ARG;
    }

    sw_init init{info, hot, set_unload, set_all};
    sw_memory memory{alloc, free, resize, hook_address, unhook_address, hook_vtable,
                     unhook_vtable, signature, offset, scan, vtable, valve};
    sw_schema schema{schema_offset, changed};
    sw_shared shared{get, set, has, remove};
    sw_core core{&init, &memory, &schema, &shared};
    sw_init_context context{query};

    host() { CHECK(!active); active = this; }
    ~host()
    {
        unload();
        CHECK(allocations.empty());
        CHECK(address_hooks.empty());
        CHECK(vtable_hooks.empty());
        active = nullptr;
    }
    void notify()
    {
        initializing = false;
        if (all_fn) all_fn(all_data);
    }
    void unload()
    {
        initializing = false;
        auto fn = std::exchange(unload_fn, nullptr);
        auto data = std::exchange(unload_data, nullptr);
        all_fn = nullptr;
        all_data = nullptr;
        if (fn) fn(data);
    }
};

enum class behavior { success, error, load_exception, all_exception, unload_exception, bad_name };
behavior mode;
int loads = 0;
int notifications = 0;
int unloads = 0;
int destroyed = 0;
int bound_destructions = 0;
int member_bound_destructions = 0;

void reset_counts(behavior value = behavior::success)
{
    mode = value;
    loads = notifications = unloads = destroyed = bound_destructions = member_bound_destructions = 0;
}

struct core_probe {
    sw::Core api;

    explicit core_probe(sw::Core core) noexcept : api(core) {}

    ~core_probe()
    {
        // An independently saved copy remains valid during member destruction.
        if (api.Schema().GetOffset("CBaseEntity", "m_iHealth")) {
            CHECK(api.Misc().IsHotReloaded() == active->hot_reloaded);
            ++member_bound_destructions;
        }
    }
};

class test_extension : public sw::Extension {
public:
    test_extension()
    {
        CHECK(!core_ && !core_probe_);
    }
    ~test_extension() override
    {
        if (core_) {
            CHECK(core_->Schema().GetOffset("CBaseEntity", "m_iHealth"));
            CHECK(core_->Misc().IsHotReloaded() == active->hot_reloaded);
            ++bound_destructions;
        }
        ++destroyed;
    }
    std::string_view Name() const noexcept override
    {
        CHECK(!core_ && !core_probe_); // Host APIs arrive only in OnLoad.
        if (mode == behavior::bad_name) return {"bad\0name", 8};
        return {"Example-not-part-of-name", 7}; // Deliberately not NUL-terminated at size().
    }
    std::string_view Version() const noexcept override { return "1.0.0"; }
    sw::Result<void> OnLoad(sw::Core core) override
    {
        ++loads;
        core_ = core;
        core_probe_.emplace(core);
        CHECK(core.Misc().IsHotReloaded() == active->hot_reloaded);
        CHECK(active->name == "Example");
        auto allocation = core.Memory().Alloc(32);
        CHECK(allocation);
        buffer_ = *allocation;
        auto hook = core.Memory().HookAddress(reinterpret_cast<void*>(&original), &replacement);
        CHECK(hook);
        hook_ = std::move(*hook);
        if (mode == behavior::error) return std::unexpected(sw::Error(SW_E_FAILED, "expected detailed load error"));
        if (mode == behavior::load_exception) throw std::runtime_error("expected load exception");
        return {};
    }
    void OnAllExtensionsLoaded() override
    {
        ++notifications;
        CHECK(!active->initializing);
        CHECK(core_);
        CHECK(core_->Misc().IsHotReloaded() == active->hot_reloaded);
        CHECK(core_->Shared().HasSharedPointer("absent") && !*core_->Shared().HasSharedPointer("absent"));
        if (mode == behavior::all_exception) throw std::runtime_error("expected notification exception");
    }
    void OnUnload() override
    {
        ++unloads;
        CHECK(!active->initializing);
        CHECK(core_);
        CHECK(core_->Misc().IsHotReloaded() == active->hot_reloaded);
        CHECK(core_->Schema().GetOffset("CBaseEntity", "m_iHealth"));
        CHECK(core_->Memory().Free(buffer_));
        buffer_ = nullptr;
        if (mode == behavior::unload_exception) throw std::runtime_error("expected unload exception");
    }
private:
    std::optional<sw::Core> core_;
    std::optional<core_probe> core_probe_;
    void* buffer_ = nullptr;
    sw::AddressHook<function> hook_;
};

// Verify stable identity on an extension with no move-only resources of its own.
class simple_extension final : public sw::Extension {
public:
    std::string_view Name() const noexcept override { return "Simple"; }
    std::string_view Version() const noexcept override { return "1.0.0"; }
    sw::Result<void> OnLoad(sw::Core core) override
    {
        auto offset = core.Schema().GetOffset("CBaseEntity", "m_iHealth");
        if (!offset)
            return std::unexpected(offset.error());
        return {};
    }
};

void check_error(const auto& result, sw_status code,
                 std::initializer_list<std::string_view> fragments)
{
    CHECK(!result);
    CHECK(result.error().code() == code);
    for (const auto fragment : fragments)
        CHECK(result.error().msg().find(fragment) != std::string::npos);
}

void resource_contracts()
{
    host host;
    sw::Memory memory(&host.memory);
    {
        auto allocation = memory.Alloc(16);
        CHECK(allocation);
        void* pointer = *allocation;
        std::memset(pointer, 42, 16);
        auto* previous = pointer;
        host.fail_resize = true;
        auto failed = memory.Resize(pointer, 128);
        check_error(failed, SW_E_FAILED, {"memory.resize", "null", "128", "original allocation remains valid"});
        CHECK(pointer == previous && host.allocations.at(pointer) == 16);
        CHECK(static_cast<unsigned char*>(pointer)[0] == 42);
        host.fail_resize = false;
        auto resized = memory.Resize(pointer, 64);
        CHECK(resized && *resized != previous); // This fake allocator always moves.
        pointer = *resized;
        CHECK(host.allocations.at(pointer) == 64 && static_cast<unsigned char*>(pointer)[0] == 42);
        CHECK(memory.Free(pointer));
        CHECK(host.allocations.empty() && host.frees == 1);
    }
    {
        void* pointer;
        {
            auto allocation = memory.Alloc(4);
            CHECK(allocation);
            pointer = *allocation;
        }
        CHECK(host.allocations.contains(pointer)); // The result does not own the allocation.
        CHECK(memory.Free(pointer));
    }
    {
        auto zero = memory.Alloc(0);
        CHECK(zero && *zero && host.last_alloc_size == 0 && host.alloc_calls == 3);
        CHECK(memory.Free(*zero));
        auto fresh = memory.Resize(nullptr, 8);
        CHECK(fresh && *fresh);
        const auto previous_calls = host.resize_calls;
        auto empty = memory.Resize(*fresh, 0);
        CHECK(empty && *empty == nullptr);
        CHECK(host.resize_calls == previous_calls + 1 && host.last_resize_size == 0);
        CHECK(host.allocations.empty());
        CHECK(memory.Free(nullptr));
    }
    {
        auto hook = memory.HookAddress(reinterpret_cast<void*>(&original), &replacement);
        CHECK(hook && hook->Original()(3) == 4);
        auto moved = std::move(*hook);
        CHECK(!*hook && !hook->Original());
        auto target = memory.HookAddress(reinterpret_cast<void*>(&original), &replacement);
        CHECK(target);
        *target = std::move(moved);
        CHECK(!moved && !moved.Original());
        CHECK(host.address_unhooks == 1);
    }
    CHECK(host.address_unhooks == 2);
    {
        auto hook = memory.HookVTable(&host.object, 2, &replacement);
        CHECK(hook && hook->Original()(0) == 1);
        CHECK(hook->Reset());
        CHECK(!*hook && !hook->Original());
        CHECK(hook->Reset());
    }
    CHECK(host.vtable_unhooks == 1);
    host.fail_hook = true;
    CHECK(!memory.HookAddress(reinterpret_cast<void*>(&original), &replacement));
    host.fail_hook = false;
    host.broken_hook = true;
    CHECK(!memory.HookAddress(reinterpret_cast<void*>(&original), &replacement));
    CHECK(host.address_hooks.empty() && host.address_unhooks == 3);
}

void lookup_contracts()
{
    host host;
    sw::Memory memory(&host.memory);
    sw::Schema schema(&host.schema);
    sw::Shared shared(&host.shared);
    CHECK(*memory.GameDataGetOffset("offset") == 0);
    CHECK(!memory.GameDataGetOffset("absent"));
    CHECK(memory.GameDataResolveSignature("sig"));
    CHECK(memory.ResolveSignature("server", "sig"));
    CHECK(*memory.FindVTable("server", "Entity") == &host.object);
    CHECK(*memory.GetValveInterface("Example001") == &host.object);
    CHECK(*schema.GetOffset("CBaseEntity", "m_iHealth") == 0);
    CHECK(!schema.GetOffset("CBaseEntity", "absent"));
    CHECK(schema.SetStateChanged(&host.object, "CBaseEntity", "m_iHealth"));
    CHECK(host.state_changes == 1);
    CHECK(!schema.SetStateChanged(nullptr, "CBaseEntity", "m_iHealth"));
    CHECK(shared.SetSharedPointer("null", nullptr));
    CHECK(*shared.HasSharedPointer("null"));
    auto registered_null = shared.GetSharedPointer("null");
    CHECK(registered_null && *registered_null == nullptr);
    CHECK(!shared.GetSharedPointer("absent"));
    CHECK(!*shared.HasSharedPointer("absent"));
    CHECK(shared.SetSharedPointer("object", &host.object));
    CHECK(*shared.GetSharedPointer("object") == &host.object);
    CHECK(shared.RemoveSharedPointer("null"));
    CHECK(!shared.RemoveSharedPointer("null"));
    const std::string_view substring("object-suffix", 6);
    CHECK(*shared.GetSharedPointer(substring) == &host.object);
    CHECK(!shared.SetSharedPointer(std::string_view("bad\0key", 7), nullptr));
    CHECK(!memory.GameDataResolveSignature(std::string_view("sig\0suffix", 10)));
    CHECK(!schema.GetOffset("CBaseEntity", std::string_view("m_iHealth\0suffix", 16)));
    CHECK(!sw::Memory(nullptr).Alloc(1));
    CHECK(!sw::Schema(nullptr).GetOffset("Entity", "field"));
    CHECK(!sw::Shared(nullptr).GetSharedPointer("key"));
}

void result_contracts()
{
    std::string message = "custom extension failure";
    sw::Result<int> result = std::unexpected(sw::Error(-123, message));
    message.clear();
    check_error(result, -123, {"custom extension failure"});
    sw::Result<void> propagated = std::unexpected(result.error());
    check_error(propagated, -123, {"custom extension failure"});
    const sw::Error& error = propagated.error();
    CHECK(error.msg() == "custom extension failure");
}

void error_contracts()
{
    host host;
    sw::Memory memory(&host.memory);
    sw::Schema schema(&host.schema);
    sw::Shared shared(&host.shared);
    host.fail_alloc = true;
    check_error(memory.Alloc(1024), SW_E_FAILED, {"memory.alloc", "allocator returned null", "1024"});
    host.fail_alloc = false;
    host.broken_alloc = true;
    check_error(memory.Alloc(32), SW_E_FAILED, {"memory.alloc", "null", "32"});
    host.broken_alloc = false;

    // Raw allocation needs only the allocator callback; cleanup is explicit.
    const auto saved_free = std::exchange(host.memory.free, nullptr);
    auto allocated = memory.Alloc(1);
    CHECK(allocated && *allocated);
    check_error(memory.Free(*allocated), SW_E_FAILED, {"memory.free", "free callback is unavailable"});
    host.memory.free = saved_free;
    CHECK(memory.Free(*allocated));

    check_error(memory.GameDataResolveSignature("absent"), SW_E_INVALID_ARG, {"gamedata_resolve_signature", "absent", "name does not exist"});
    check_error(memory.GameDataResolveSignature("null"), SW_E_FAILED, {"gamedata_resolve_signature", "null", "stored signature address is null"});
    check_error(memory.GameDataResolveSignature("broken"), SW_E_FAILED, {"gamedata_resolve_signature", "broken", "null"});
    check_error(memory.GameDataGetOffset("absent"), SW_E_INVALID_ARG, {"gamedata_get_offset", "absent", "offset name does not exist"});
    check_error(memory.ResolveSignature("missing", "pattern"), SW_E_FAILED, {"resolve_signature", "missing", "pattern", "scan failed"});
    check_error(memory.FindVTable("server", "MissingClass"), SW_E_FAILED, {"find_vtable", "MissingClass", "lookup failed"});
    check_error(memory.GetValveInterface("Missing001"), SW_E_FAILED, {"get_valve_interface", "Missing001", "lookup failed"});
    check_error(schema.GetOffset("MissingClass", "field"), SW_E_INVALID_ARG, {"schema.get_offset", "MissingClass", "field", "class or field does not exist"});
    check_error(schema.SetStateChanged(&host.object, "CBaseEntity", "missing"), SW_E_INVALID_ARG, {"set_state_changed", "CBaseEntity", "missing", "class or field does not exist"});
    check_error(schema.SetStateChanged(nullptr, "CBaseEntity", "m_iHealth"), SW_E_INVALID_ARG, {"set_state_changed", "object is null"});
    check_error(shared.GetSharedPointer("absent"), SW_E_INVALID_ARG, {"shared.get_shared_pointer", "absent", "key does not exist"});
    check_error(shared.RemoveSharedPointer("absent"), SW_E_INVALID_ARG, {"shared.remove_shared_pointer", "absent", "key does not exist"});

    const std::string_view invalid("bad\0text", 8);
    check_error(memory.GameDataResolveSignature(invalid), SW_E_INVALID_ARG, {"gamedata_resolve_signature", "name", "embedded NUL"});
    check_error(memory.GameDataGetOffset(invalid), SW_E_INVALID_ARG, {"gamedata_get_offset", "name", "embedded NUL"});
    check_error(memory.ResolveSignature(invalid, "sig"), SW_E_INVALID_ARG, {"resolve_signature", "library", "embedded NUL"});
    check_error(memory.ResolveSignature("server", invalid), SW_E_INVALID_ARG, {"resolve_signature", "pattern", "embedded NUL"});
    check_error(memory.FindVTable(invalid, "Entity"), SW_E_INVALID_ARG, {"find_vtable", "library", "embedded NUL"});
    check_error(memory.FindVTable("server", invalid), SW_E_INVALID_ARG, {"find_vtable", "class_name", "embedded NUL"});
    check_error(memory.GetValveInterface(invalid), SW_E_INVALID_ARG, {"get_valve_interface", "version", "embedded NUL"});
    check_error(schema.GetOffset(invalid, "field"), SW_E_INVALID_ARG, {"schema.get_offset", "class_name", "embedded NUL"});
    check_error(schema.GetOffset("Entity", invalid), SW_E_INVALID_ARG, {"schema.get_offset", "field_name", "embedded NUL"});
    check_error(schema.SetStateChanged(&host.object, invalid, "field"), SW_E_INVALID_ARG, {"set_state_changed", "class_name", "embedded NUL"});
    check_error(schema.SetStateChanged(&host.object, "Entity", invalid), SW_E_INVALID_ARG, {"set_state_changed", "field_name", "embedded NUL"});
    check_error(shared.GetSharedPointer(invalid), SW_E_INVALID_ARG, {"shared.get_shared_pointer", "key", "embedded NUL"});
    check_error(shared.SetSharedPointer(invalid, nullptr), SW_E_INVALID_ARG, {"shared.set_shared_pointer", "key", "embedded NUL"});
    check_error(shared.HasSharedPointer(invalid), SW_E_INVALID_ARG, {"shared.has_shared_pointer", "key", "embedded NUL"});
    check_error(shared.RemoveSharedPointer(invalid), SW_E_INVALID_ARG, {"shared.remove_shared_pointer", "key", "embedded NUL"});

    check_error(memory.HookAddress(nullptr, &replacement), SW_E_INVALID_ARG, {"hook_address", "address is null"});
    check_error(memory.HookAddress(reinterpret_cast<void*>(&original), static_cast<function>(nullptr)), SW_E_INVALID_ARG, {"hook_address", "replacement function is null"});
    check_error(memory.HookVTable(nullptr, 2, &replacement), SW_E_INVALID_ARG, {"hook_vtable", "vtable address is null"});
    check_error(memory.HookVTable(&host.object, 2, static_cast<function>(nullptr)), SW_E_INVALID_ARG, {"hook_vtable", "replacement function is null"});
    check_error(memory.HookVTable(&host.object, UINT32_MAX, &replacement), SW_E_INVALID_ARG, {"hook_vtable", "index", "exceeds INT_MAX"});
    host.fail_hook = true;
    check_error(memory.HookAddress(reinterpret_cast<void*>(&original), &replacement), SW_E_FAILED, {"hook_address", "trampoline is null", "not enabled"});
    check_error(memory.HookVTable(&host.object, 2, &replacement), SW_E_FAILED, {"hook_vtable", "trampoline is null", "not enabled"});
    host.fail_hook = false;
    host.broken_hook = true;
    check_error(memory.HookVTable(&host.object, 2, &replacement), SW_E_FAILED, {"hook_vtable", "success", "null"});
    CHECK(host.vtable_hooks.empty() && host.vtable_unhooks == 1);
    host.broken_hook = false;

    auto hook = memory.HookAddress(reinterpret_cast<void*>(&original), &replacement);
    CHECK(hook);
    const auto saved_unhook = std::exchange(host.memory.unhook_address,
        +[](const sw_memory*, sw_hook_handle) -> sw_status { return SW_E_INVALID_ARG; });
    check_error(hook->Reset(), SW_E_INVALID_ARG, {"unhook_address", "interface or hook handle is null"});
    CHECK(*hook && hook->Original());
    host.memory.unhook_address = +[](const sw_memory*, sw_hook_handle) -> sw_status {
        throw std::runtime_error("unhook exception details");
    };
    check_error(hook->Reset(), SW_E_FAILED, {"unhook_address", "unhook exception details"});
    host.memory.unhook_address = saved_unhook;
    CHECK(hook->Reset());

    sw_memory absent_memory{};
    sw_schema absent_schema{};
    sw_shared absent_shared{};
    for (const auto* api : {static_cast<const sw_memory*>(nullptr), static_cast<const sw_memory*>(&absent_memory)}) {
        const sw::Memory absent(api);
        check_error(absent.Alloc(1), SW_E_FAILED, {"memory.alloc", "unavailable"});
        check_error(absent.Free(nullptr), SW_E_FAILED, {"memory.free", "unavailable"});
        check_error(absent.Resize(nullptr, 1), SW_E_FAILED, {"memory.resize", "unavailable"});
        check_error(absent.GameDataResolveSignature("sig"), SW_E_FAILED, {"gamedata_resolve_signature", "unavailable"});
        check_error(absent.GameDataGetOffset("offset"), SW_E_FAILED, {"gamedata_get_offset", "unavailable"});
        check_error(absent.ResolveSignature("server", "sig"), SW_E_FAILED, {"resolve_signature", "unavailable"});
        check_error(absent.FindVTable("server", "Entity"), SW_E_FAILED, {"find_vtable", "unavailable"});
        check_error(absent.GetValveInterface("Example001"), SW_E_FAILED, {"get_valve_interface", "unavailable"});
        check_error(absent.HookAddress(reinterpret_cast<void*>(&original), &replacement), SW_E_FAILED, {"hook_address", "unavailable"});
        check_error(absent.HookVTable(&host.object, 2, &replacement), SW_E_FAILED, {"hook_vtable", "unavailable"});
    }
    for (const auto* api : {static_cast<const sw_schema*>(nullptr), static_cast<const sw_schema*>(&absent_schema)}) {
        check_error(sw::Schema(api).GetOffset("Entity", "field"), SW_E_FAILED, {"schema.get_offset", "unavailable"});
        check_error(sw::Schema(api).SetStateChanged(&host.object, "Entity", "field"), SW_E_FAILED, {"set_state_changed", "unavailable"});
    }
    for (const auto* api : {static_cast<const sw_shared*>(nullptr), static_cast<const sw_shared*>(&absent_shared)}) {
        const sw::Shared absent(api);
        check_error(absent.GetSharedPointer("key"), SW_E_FAILED, {"shared.get_shared_pointer", "unavailable"});
        check_error(absent.SetSharedPointer("key", nullptr), SW_E_FAILED, {"shared.set_shared_pointer", "unavailable"});
        check_error(absent.HasSharedPointer("key"), SW_E_FAILED, {"shared.has_shared_pointer", "unavailable"});
        check_error(absent.RemoveSharedPointer("key"), SW_E_FAILED, {"shared.remove_shared_pointer", "unavailable"});
    }

    host.offset_status = SW_E_BUFFER_TOO_SMALL;
    check_error(memory.GameDataGetOffset("offset"), SW_E_BUFFER_TOO_SMALL, {"gamedata_get_offset", "buffer is too small"});
    host.offset_status = SW_E_INVALID_OPERATION;
    check_error(memory.GameDataGetOffset("offset"), SW_E_INVALID_OPERATION, {"gamedata_get_offset", "not allowed in the current state"});
    host.offset_status = -91;
    check_error(memory.GameDataGetOffset("offset"), -91, {"gamedata_get_offset", "unexpected status -91"});
}

void lifecycle_contracts()
{
    for (const bool hot_reloaded : {false, true})
    for (auto value : {behavior::success, behavior::error, behavior::load_exception,
                       behavior::all_exception, behavior::unload_exception, behavior::bad_name}) {
        reset_counts(value);
        host host;
        host.hot_reloaded = hot_reloaded;
        const auto status = sw::RunExtension<test_extension>(&host.context);
        if (value == behavior::bad_name) CHECK(status == SW_E_INVALID_ARG && loads == 0);
        else if (value == behavior::error) CHECK(status == SW_E_FAILED);
        else if (value == behavior::load_exception) CHECK(status == SW_E_FAILED);
        else CHECK(status == SW_OK);
        CHECK(destroyed == 0); // Ownership already transferred to the host's unload trampoline.
        if (status == SW_OK) {
            CHECK(host.version == "1.0.0" && host.author_absent && host.description_absent);
            host.notify();
            host.notify();
            CHECK(notifications == 2);
        }
        host.unload();
        const int saved_views = value == behavior::bad_name ? 0 : 1;
        CHECK(destroyed == 1 && bound_destructions == saved_views && member_bound_destructions == saved_views);
        CHECK(unloads == (value == behavior::bad_name ? 0 : 1));
        CHECK(host.allocations.empty() && host.address_hooks.empty());
        CHECK(host.hot_queries == 1); // Later callbacks read the cached state.
        host.unload();
        CHECK(destroyed == 1);
    }
    {
        reset_counts();
        host host;
        host.unload_registration_status = SW_E_FAILED;
        CHECK(sw::RunExtension<test_extension>(&host.context) == SW_E_FAILED);
        CHECK(destroyed == 1 && bound_destructions == 0 && loads == 0 && unloads == 0);
        CHECK(member_bound_destructions == 0);
        CHECK(!host.unload_fn);
    }
    {
        reset_counts();
        host host;
        host.all_registration_status = SW_E_FAILED;
        CHECK(sw::RunExtension<test_extension>(&host.context) == SW_E_FAILED);
        CHECK(destroyed == 0 && loads == 0);
        host.unload();
        CHECK(destroyed == 1 && bound_destructions == 0 && unloads == 0);
        CHECK(member_bound_destructions == 0);
    }
    {
        reset_counts();
        host host;
        host.metadata_status = -42;
        CHECK(sw::RunExtension<test_extension>(&host.context) == -42);
        CHECK(loads == 0 && destroyed == 0);
        host.unload();
        CHECK(destroyed == 1 && bound_destructions == 0 && unloads == 0);
        CHECK(member_bound_destructions == 0);
    }
    {
        reset_counts();
        host host;
        host.query_status = SW_E_FAILED;
        CHECK(sw::RunExtension<test_extension>(&host.context) == SW_E_FAILED);
        CHECK(!host.unload_fn && loads == 0 && destroyed == 0);
        CHECK(sw::RunExtension<test_extension>(nullptr) == SW_E_INVALID_ARG);
        sw_init_context empty{};
        CHECK(sw::RunExtension<test_extension>(&empty) == SW_E_INVALID_ARG);
    }
    {
        // Saving a core view is optional for an extension that needs it only in OnLoad.
        host host;
        CHECK(sw::RunExtension<simple_extension>(&host.context) == SW_OK);
        host.notify();
        host.unload();
        CHECK(host.hot_queries == 1 && !host.unload_fn && !host.all_fn);
    }
}

} // namespace

static_assert(std::is_same_v<sw::Result<void>::error_type, sw::Error>);
static_assert(std::is_same_v<decltype(std::declval<const sw::Error&>().code()), sw_status>);
static_assert(std::is_same_v<decltype(std::declval<const sw::Error&>().msg()), const std::string&>);
static_assert(noexcept(std::declval<const sw::Error&>().code()));
static_assert(noexcept(std::declval<const sw::Error&>().msg()));
static_assert(std::is_same_v<decltype(std::declval<sw::Memory>().Alloc(1)), sw::Result<void*>>);
static_assert(std::is_same_v<decltype(std::declval<const sw::Core&>().Memory()), const sw::Memory&>);
static_assert(std::is_same_v<decltype(std::declval<const sw::Core&>().Schema()), const sw::Schema&>);
static_assert(std::is_same_v<decltype(std::declval<const sw::Core&>().Shared()), const sw::Shared&>);
static_assert(std::is_same_v<decltype(std::declval<const sw::Core&>().Misc()), const sw::Misc&>);
static_assert(std::is_same_v<decltype(&sw::Extension::OnLoad), sw::Result<void> (sw::Extension::*)(sw::Core)>);
static_assert(std::is_nothrow_copy_constructible_v<sw::Core>);
static_assert(std::is_nothrow_copy_assignable_v<sw::Core>);
static_assert(!std::is_copy_constructible_v<simple_extension>);
static_assert(!std::is_copy_assignable_v<simple_extension>);
static_assert(!std::is_move_constructible_v<simple_extension>);
static_assert(!std::is_move_assignable_v<simple_extension>);
static_assert(!std::is_copy_constructible_v<sw::AddressHook<function>>);
static_assert(!std::is_same_v<sw::AddressHook<function>, sw::VTableHook<function>>);

int main()
{
    resource_contracts();
    lookup_contracts();
    result_contracts();
    error_contracts();
    lifecycle_contracts();
    std::puts("SDK ownership, lookup and lifecycle contracts passed.");
}
