#include <swiftly/sdk.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
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
    bool fail_hook = false;
    bool broken_hook = false;
    sw_status query_status = SW_OK;
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
    int address_unhooks = 0;
    int vtable_unhooks = 0;
    int state_changes = 0;
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
        CHECK(size > 0);
        auto* pointer = std::malloc(static_cast<std::size_t>(size));
        *static_cast<void**>(out) = pointer;
        if (!pointer) return SW_E_FAILED;
        active->allocations.emplace(pointer, size);
        return SW_OK;
    }
    static sw_status free(const sw_memory* self, void* pointer)
    {
        CHECK(self == &active->memory);
        CHECK(active->allocations.erase(pointer) == 1);
        std::free(pointer);
        ++active->frees;
        return SW_OK;
    }
    static sw_status resize(const sw_memory* self, void* pointer, std::uint64_t size, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        CHECK(size > 0); // SDK must implement resize(0) using free instead.
        *static_cast<void**>(out) = nullptr;
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
        if (std::strcmp(name, "sig") != 0) return SW_E_INVALID_ARG;
        *static_cast<void**>(out) = reinterpret_cast<void*>(&original);
        return SW_OK;
    }
    static sw_status offset(const sw_memory* self, const char* name, std::uint32_t* out)
    {
        CHECK(self == &active->memory);
        if (std::strcmp(name, "offset") != 0) return SW_E_INVALID_ARG;
        *out = 0;
        return SW_OK;
    }
    static sw_status scan(const sw_memory* self, const char* lib, const char* pattern, sw_ptr_out out)
    {
        CHECK(std::strcmp(lib, "server") == 0);
        CHECK(std::strcmp(pattern, "sig") == 0);
        return signature(self, pattern, out);
    }
    static sw_status vtable(const sw_memory* self, const char* lib, const char*, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
        CHECK(std::strcmp(lib, "server") == 0);
        *static_cast<void**>(out) = &active->object;
        return SW_OK;
    }
    static sw_status valve(const sw_memory* self, const char*, sw_ptr_out out)
    {
        CHECK(self == &active->memory);
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

void reset_counts(behavior value = behavior::success)
{
    mode = value;
    loads = notifications = unloads = destroyed = 0;
}

class test_extension : public sw::extension {
public:
    ~test_extension() override { ++destroyed; }
    std::string_view name() const noexcept override
    {
        if (mode == behavior::bad_name) return {"bad\0name", 8};
        return {"Example-not-part-of-name", 7}; // Deliberately not NUL-terminated at size().
    }
    std::string_view version() const noexcept override { return "1.0.0"; }
    sw::result<void> on_load(sw::core& core) override
    {
        ++loads;
        CHECK(core.hot_reloaded == active->hot_reloaded);
        CHECK(active->name == "Example");
        auto allocation = core.memory.allocate(32);
        CHECK(allocation);
        buffer_ = std::move(*allocation);
        auto hook = core.memory.hook_address(reinterpret_cast<void*>(&original), &replacement);
        CHECK(hook);
        hook_ = std::move(*hook);
        if (mode == behavior::error) return sw::detail::fail(-73);
        if (mode == behavior::load_exception) throw std::runtime_error("expected load exception");
        return {};
    }
    void on_all_extensions_loaded(sw::core& core) override
    {
        ++notifications;
        CHECK(!active->initializing);
        CHECK(core.shared.has("absent") && !*core.shared.has("absent"));
        if (mode == behavior::all_exception) throw std::runtime_error("expected notification exception");
    }
    void on_unload(sw::core& core) override
    {
        ++unloads;
        CHECK(!active->initializing);
        CHECK(core.schema.offset("CBaseEntity", "m_iHealth"));
        if (mode == behavior::unload_exception) throw std::runtime_error("expected unload exception");
    }
private:
    sw::game_buffer buffer_;
    sw::address_hook<function> hook_;
};

void resource_contracts()
{
    host host;
    sw::memory memory(&host.memory);
    {
        auto allocation = memory.allocate(16);
        CHECK(allocation);
        std::ranges::fill(allocation->bytes(), std::byte{42});
        auto* previous = allocation->data();
        host.fail_resize = true;
        auto failed = allocation->resize(128);
        CHECK(!failed && failed.error().code == SW_E_FAILED);
        CHECK(allocation->data() == previous && allocation->size() == 16);
        CHECK(allocation->bytes()[0] == std::byte{42});
        host.fail_resize = false;
        CHECK(allocation->resize(64));
        CHECK(allocation->size() == 64 && allocation->bytes()[0] == std::byte{42});
        auto moved = std::move(*allocation);
        CHECK(!allocation->data() && allocation->size() == 0);
        auto target = memory.allocate(4);
        CHECK(target);
        *target = std::move(moved);
        CHECK(!moved.data() && moved.size() == 0);
        CHECK(host.frees == 1);
        CHECK(target->resize(0));
        CHECK(!target->data() && target->size() == 0);
        CHECK(host.frees == 2);
        CHECK(target->reset());
        CHECK(target->resize(8));
    }
    CHECK(host.frees == 3);
    {
        auto empty = memory.allocate(0);
        CHECK(empty && empty->bytes().empty());
        CHECK(empty->resize(5));
    }
    CHECK(host.frees == 4);
    {
        auto hook = memory.hook_address(reinterpret_cast<void*>(&original), &replacement);
        CHECK(hook && hook->original()(3) == 4);
        auto moved = std::move(*hook);
        CHECK(!*hook && !hook->original());
        auto target = memory.hook_address(reinterpret_cast<void*>(&original), &replacement);
        CHECK(target);
        *target = std::move(moved);
        CHECK(!moved && !moved.original());
        CHECK(host.address_unhooks == 1);
    }
    CHECK(host.address_unhooks == 2);
    {
        auto hook = memory.hook_vtable(&host.object, 2, &replacement);
        CHECK(hook && hook->original()(0) == 1);
        CHECK(hook->reset());
        CHECK(!*hook && !hook->original());
        CHECK(hook->reset());
    }
    CHECK(host.vtable_unhooks == 1);
    host.fail_hook = true;
    CHECK(!memory.hook_address(reinterpret_cast<void*>(&original), &replacement));
    host.fail_hook = false;
    host.broken_hook = true;
    CHECK(!memory.hook_address(reinterpret_cast<void*>(&original), &replacement));
    CHECK(host.address_hooks.empty() && host.address_unhooks == 3);
}

void lookup_contracts()
{
    host host;
    sw::memory memory(&host.memory);
    sw::schema schema(&host.schema);
    sw::shared_registry shared(&host.shared);
    CHECK(*memory.gamedata_offset("offset") == 0);
    CHECK(!memory.gamedata_offset("absent"));
    CHECK(memory.gamedata_signature("sig"));
    CHECK(memory.resolve_signature("server", "sig"));
    CHECK(*memory.find_vtable("server", "Entity") == &host.object);
    CHECK(*memory.valve_interface("Example001") == &host.object);
    CHECK(*schema.offset("CBaseEntity", "m_iHealth") == 0);
    CHECK(!schema.offset("CBaseEntity", "absent"));
    CHECK(schema.set_state_changed(&host.object, "CBaseEntity", "m_iHealth"));
    CHECK(host.state_changes == 1);
    CHECK(!schema.set_state_changed(nullptr, "CBaseEntity", "m_iHealth"));
    CHECK(shared.set("null", nullptr));
    CHECK(*shared.has("null"));
    auto registered_null = shared.get("null");
    CHECK(registered_null && *registered_null == nullptr);
    CHECK(!shared.get("absent"));
    CHECK(!*shared.has("absent"));
    CHECK(shared.set("object", &host.object));
    CHECK(*shared.get("object") == &host.object);
    CHECK(shared.remove("null"));
    CHECK(!shared.remove("null"));
    const std::string_view substring("object-suffix", 6);
    CHECK(*shared.get(substring) == &host.object);
    CHECK(!shared.set(std::string_view("bad\0key", 7), nullptr));
    CHECK(!memory.gamedata_signature(std::string_view("sig\0suffix", 10)));
    CHECK(!schema.offset("CBaseEntity", std::string_view("m_iHealth\0suffix", 16)));
    CHECK(!sw::memory(nullptr).allocate(1));
    CHECK(!sw::schema(nullptr).offset("Entity", "field"));
    CHECK(!sw::shared_registry(nullptr).get("key"));
}

void lifecycle_contracts()
{
    for (auto value : {behavior::success, behavior::error, behavior::load_exception,
                       behavior::all_exception, behavior::unload_exception, behavior::bad_name}) {
        reset_counts(value);
        host host;
        host.hot_reloaded = true;
        const auto status = sw::run_extension<test_extension>(&host.context);
        if (value == behavior::bad_name) CHECK(status == SW_E_INVALID_ARG && loads == 0);
        else if (value == behavior::error) CHECK(status == -73);
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
        CHECK(destroyed == 1);
        CHECK(unloads == (value == behavior::bad_name ? 0 : 1));
        CHECK(host.allocations.empty() && host.address_hooks.empty());
        host.unload();
        CHECK(destroyed == 1);
    }
    {
        reset_counts();
        host host;
        host.unload_registration_status = SW_E_FAILED;
        CHECK(sw::run_extension<test_extension>(&host.context) == SW_E_FAILED);
        CHECK(destroyed == 1 && loads == 0 && unloads == 0);
        CHECK(!host.unload_fn);
    }
    {
        reset_counts();
        host host;
        host.all_registration_status = SW_E_FAILED;
        CHECK(sw::run_extension<test_extension>(&host.context) == SW_E_FAILED);
        CHECK(destroyed == 0 && loads == 0);
        host.unload();
        CHECK(destroyed == 1 && unloads == 0);
    }
    {
        reset_counts();
        host host;
        host.metadata_status = -42;
        CHECK(sw::run_extension<test_extension>(&host.context) == -42);
        CHECK(loads == 0 && destroyed == 0);
        host.unload();
        CHECK(destroyed == 1 && unloads == 0);
    }
    {
        reset_counts();
        host host;
        host.query_status = SW_E_FAILED;
        CHECK(sw::run_extension<test_extension>(&host.context) == SW_E_FAILED);
        CHECK(!host.unload_fn && loads == 0 && destroyed == 0);
        CHECK(sw::run_extension<test_extension>(nullptr) == SW_E_INVALID_ARG);
        sw_init_context empty{};
        CHECK(sw::run_extension<test_extension>(&empty) == SW_E_INVALID_ARG);
    }
}

} // namespace

static_assert(!std::is_copy_constructible_v<sw::game_buffer>);
static_assert(std::is_nothrow_move_constructible_v<sw::game_buffer>);
static_assert(!std::is_copy_constructible_v<sw::address_hook<function>>);
static_assert(!std::is_same_v<sw::address_hook<function>, sw::vtable_hook<function>>);

int main()
{
    resource_contracts();
    lookup_contracts();
    lifecycle_contracts();
    std::puts("SDK ownership, lookup and lifecycle contracts passed.");
}
