#include <swiftly/sdk.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

class ExampleExtension final : public sw::extension {
public:
    std::string_view name() const noexcept override { return "Example"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    std::string_view author() const noexcept override { return "Example author"; }
    std::string_view description() const noexcept override { return "C++ SDK example"; }

    sw::result<void> on_load(sw::core& core) override
    {
        auto allocation = core.memory.allocate(1024);
        if (!allocation)
            return std::unexpected(allocation.error());
        buffer_ = std::move(*allocation);
        std::ranges::fill(buffer_.bytes(), std::byte{0});

        std::printf("Example loaded (hot reload: %d)\n", static_cast<int>(core.hot_reloaded));
        return {};
    }

    void on_all_extensions_loaded(sw::core& core) override
    {
        auto available = core.shared.has("example.service.v1");
        if (available)
            std::printf("Example service available: %d\n", static_cast<int>(*available));
    }

private:
    // Released automatically on unload, including failed initialization.
    sw::game_buffer buffer_;
};

SW_DEFINE_EXTENSION(ExampleExtension)
