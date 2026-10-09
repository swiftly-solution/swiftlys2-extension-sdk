// A C host deliberately avoids linking against the SDK's newer C++ runtime.
#define SW_HOST
#include <swiftlys2/sw_extension.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
} } while (0)

static struct {
    sw_extension_callback_fn unload, all;
    void *unload_data, *all_data, *allocation;
    int info_calls, alloc_calls, free_calls, shared_calls, fail_alloc, hot;
} state;

static sw_status info(const sw_init* self, const char* name, const char* version,
                      const char* author, const char* description)
{
    (void)self; (void)author; (void)description;
    CHECK(name && *name && version && *version);
    ++state.info_calls;
    return SW_OK;
}
static sw_status hot(const sw_init* self, int32_t* out)
{
    (void)self;
    *out = state.hot;
    return SW_OK;
}
static sw_status set_unload(const sw_init* self, sw_extension_callback_fn fn, void* data)
{
    (void)self;
    state.unload = fn;
    state.unload_data = data;
    return SW_OK;
}
static sw_status set_all(const sw_init* self, sw_extension_callback_fn fn, void* data)
{
    (void)self;
    state.all = fn;
    state.all_data = data;
    return SW_OK;
}
static sw_status allocate(const sw_memory* self, uint64_t size, sw_ptr_out out)
{
    (void)self;
    ++state.alloc_calls;
    CHECK(!state.allocation && size == 1024);
    state.allocation = state.fail_alloc ? NULL : malloc((size_t)size);
    *(void**)out = state.allocation;
    return state.allocation ? SW_OK : SW_E_FAILED;
}
static sw_status release(const sw_memory* self, void* pointer)
{
    (void)self;
    CHECK(pointer == state.allocation);
    if (pointer) ++state.free_calls;
    free(pointer);
    state.allocation = NULL;
    return SW_OK;
}
static sw_status offset(const sw_schema* self, const char* name, const char* field, uint32_t* out)
{
    (void)self;
    CHECK(strcmp(name, "CBaseEntity") == 0 && strcmp(field, "m_iHealth") == 0);
    *out = 64;
    return SW_OK;
}
static sw_status has(const sw_shared* self, const char* key, int32_t* out)
{
    (void)self;
    CHECK(strcmp(key, "example.service.v1") == 0);
    ++state.shared_calls;
    *out = 0;
    return SW_OK;
}
static sw_init init = {info, hot, set_unload, set_all};
static sw_memory memory = {.alloc = allocate, .free = release};
static sw_schema schema = {.get_offset = offset};
static sw_shared shared = {.has_shared_pointer = has};
static sw_core core = {&init, &memory, &schema, &shared};
static sw_status query(const sw_init_context* self, const char* iface, sw_core** out)
{
    (void)self;
    CHECK(strcmp(iface, SW_IFACE_CORE) == 0);
    *out = &core;
    return SW_OK;
}

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    const int cpp = strcmp(argv[2], "cpp") == 0;
    CHECK(cpp || strcmp(argv[2], "c") == 0);
    // Reproduce a process that already has the Platform's old libstdc++ loaded.
    void* runtime = dlopen("libstdc++.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!runtime) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    for (int pass = 0; pass < (cpp ? 3 : 2); ++pass) {
        memset(&state, 0, sizeof(state));
        state.hot = pass != 0;
        state.fail_alloc = pass == 2;
        void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!module) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
        sw_extension_init_fn entry = (sw_extension_init_fn)dlsym(module, "sw_extension_init");
        CHECK(entry);
        sw_init_context context = {query};
        CHECK(entry(&context) == (state.fail_alloc ? SW_E_FAILED : SW_OK));
        CHECK(state.info_calls == 1 && state.unload);
        if (cpp) {
            CHECK(state.all && state.alloc_calls == 1);
            if (!state.fail_alloc) {
                state.all(state.all_data);
                state.all(state.all_data);
                CHECK(state.shared_calls == 2);
            }
        }
        state.unload(state.unload_data);
        CHECK(!state.allocation);
        CHECK(state.free_calls == (cpp && !state.fail_alloc ? 1 : 0));
        CHECK(dlclose(module) == 0);
    }
    CHECK(dlclose(runtime) == 0);
    printf("Load, initialize, callbacks, cleanup and reload passed: %s\n", argv[1]);
    return 0;
}
