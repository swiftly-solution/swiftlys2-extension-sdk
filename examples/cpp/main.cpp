#include <swiftlys2/sdk.hpp>

#include <cstdio>
#include <optional>
#include <print>

class ExampleExtension final : public sw::Extension {
public:
    std::string_view Name() const noexcept override
    { return "Example"; }

    std::string_view Version() const noexcept override
    { return "1.0.0"; }

    std::string_view Author() const noexcept override
    { return "Example author"; }

    std::string_view Description() const noexcept override
    { return "C++ SDK example"; }

    sw::Result<void> OnLoad(sw::Core core) override
    {
        // Keep our own copy for later callbacks; never keep the parameter's address.
        core_ = core;
        auto offset = core.Schema().GetOffset("CBaseEntity", "m_iHealth");
        if (!offset)
            return std::unexpected(offset.error());
        std::print("Offset {}\n", *offset);

        auto allocation = core.Memory().Alloc(1024);
        if (!allocation)
            return std::unexpected(allocation.error());
        buffer_ = *allocation;
        return {};
    }

    void OnAllExtensionsLoaded() override
    {
        auto available = core_->Shared().HasSharedPointer("example.service.v1");
        if (available)
            std::printf("Example service available: %d\n", static_cast<int>(*available));
    }

    void OnUnload() override
    {
        auto released = core_->Memory().Free(buffer_);
        if (!released)
            std::fprintf(stderr, "Error %d: %s\n",
                static_cast<int>(released.error().code()), released.error().msg().c_str());
        else
            buffer_ = nullptr;
    }

private:
    std::optional<sw::Core> core_;
    // Released in OnUnload, including after failed initialization.
    void* buffer_ = nullptr;
};

SW_DEFINE_EXTENSION(ExampleExtension)
