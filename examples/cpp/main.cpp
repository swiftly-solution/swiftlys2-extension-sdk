#include <swiftlys2/sdk.hpp>

#include <cstdio>
#include <cstring>
#include <print>

class ExampleExtension final : public sw::extension {
public:
    std::string_view name() const noexcept override
    { return "Example"; }

    std::string_view version() const noexcept override
    { return "1.0.0"; }

    std::string_view author() const noexcept override
    { return "Example author"; }

    std::string_view description() const noexcept override
    { return "C++ SDK example"; }

    sw::result<void> on_load() override
    {

        auto offset = core_.schema.get_offset("CBaseEntity", "m_iHealth");
        std::print("Offset {}\n", *offset);

        return {};
    }

    void on_all_extensions_loaded() override
    {
        auto available = core_.shared.has_shared_pointer("example.service.v1");
        if (available)
            std::printf("Example service available: %d\n", static_cast<int>(*available));
    }

    void on_unload() override
    {
        auto released = core_.memory.free(buffer_);
        if (!released)
            std::fprintf(stderr, "%s\n", released.error().c_str());
        else
            buffer_ = nullptr;
    }

private:
    // Released in on_unload, including after failed initialization.
    void* buffer_ = nullptr;
};

SW_DEFINE_EXTENSION(ExampleExtension)
