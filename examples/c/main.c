#include <swiftly/sw_extension.h>

struct extension_state {
    const sw_memory* memory;
    void* allocation;
};

static struct extension_state state;

static void on_unload(void* user_data)
{
    struct extension_state* extension = (struct extension_state*)user_data;
    if (extension->allocation) {
        extension->memory->free(extension->memory, extension->allocation);
        extension->allocation = 0;
    }
}

SW_API sw_status sw_extension_init(const sw_init_context* ctx)
{
    sw_core* core = 0;
    sw_status status;
    if (!ctx || !ctx->core)
        return SW_E_INVALID_ARG;
    status = ctx->core(ctx, SW_IFACE_CORE, &core);
    if (status != SW_OK)
        return status;
    if (!core || !core->init || !core->memory || !core->init->set_unload_callback ||
        !core->init->set_info || !core->memory->alloc || !core->memory->free)
        return SW_E_INVALID_ARG;
    state.memory = core->memory;
    status = core->init->set_unload_callback(core->init, on_unload, &state);
    if (status != SW_OK)
        return status;
    status = core->init->set_info(core->init, "C example", "1.0.0", 0, 0);
    if (status != SW_OK)
        return status;
    return core->memory->alloc(core->memory, 1024, &state.allocation);
}
