#ifndef __ZELDA_SUPPORT_H__
#define __ZELDA_SUPPORT_H__

#include <functional>
#include <filesystem>
#include <optional>

namespace zelda64 {
    std::filesystem::path get_program_path();

// Apple specific methods that usually require Objective-C. Implemented in support_apple.mm.
#ifdef __APPLE__
    void dispatch_on_ui_thread(std::function<void()> func);
    void dispatch_sync_on_ui_thread(std::function<void()> func);
    std::optional<std::filesystem::path> get_application_support_directory();
    std::filesystem::path get_bundle_resource_directory();
    std::filesystem::path get_bundle_directory();
#endif
}

#endif
