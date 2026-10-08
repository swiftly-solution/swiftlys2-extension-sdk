#pragma once

namespace sw {

/** @brief Miscellaneous extension state captured during initialization. */
class Misc {
public:
    /** @param hot_reloaded State read from sw_init::is_hotreloaded during initialization. */
    explicit Misc(bool hot_reloaded) noexcept : hot_reloaded_(hot_reloaded) {}

    /**
     * @brief Whether this extension was hot reloaded manually.
     * @return True for a manual hot reload, or false otherwise.
     * @note The SDK queries sw_init::is_hotreloaded during initialization and
     * caches its result. This accessor is also valid in later lifecycle callbacks;
     * it does not call the initialization interface after initialization finishes.
     * @see sw_init::is_hotreloaded
     */
    [[nodiscard]] bool IsHotReloaded() const noexcept { return hot_reloaded_; }

private:
    bool hot_reloaded_;
};

} // namespace sw
