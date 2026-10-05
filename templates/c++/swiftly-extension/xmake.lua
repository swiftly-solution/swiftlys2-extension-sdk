set_project("${TARGET_NAME}")
set_xmakever("3.1.0")
set_languages("cxx23")
set_warnings("all", "error")
add_rules("mode.debug", "mode.release")

option("sdk")
    set_showmenu(true)
    set_description("Path to a swiftlys2-sdk checkout")
    set_default(os.getenv("SWIFTLY_SDK_DIR"))
option_end()

target("${TARGET_NAME}")
    set_kind("shared")
    add_files("src/*.cpp")
    on_load(function (target)
        local sdk = get_config("sdk")
        assert(sdk, "Set the SDK path: xmake f --sdk=/path/to/swiftlys2-sdk")
        sdk = path.absolute(sdk, os.projectdir())
        assert(os.isfile(path.join(sdk, "c", "include", "swiftly", "sw_extension.h")),
               "SDK C header not found: " .. sdk)
        assert(os.isfile(path.join(sdk, "cpp", "include", "swiftly", "sdk.hpp")),
               "SDK C++ header not found: " .. sdk)
        target:add("includedirs", path.join(sdk, "c", "include"), path.join(sdk, "cpp", "include"))
        -- The host expects extensions/<name>/<name>.dll or <name>.so.
        target:set("filename", target:name() .. (is_plat("windows") and ".dll" or ".so"))
    end)
