#include <swiftly/sdk.hpp>

class Extension final : public sw::extension {
public:
    std::string_view name() const noexcept override { return "${TARGET_NAME}"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    sw::result<void> on_load(sw::core& core) override
    {
        (void)core;
        return {};
    }
};

SW_DEFINE_EXTENSION(Extension)
