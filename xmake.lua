local sdk_is_top_level = os.scriptdir() == os.projectdir()

-- Project defaults belong to standalone SDK builds, not consuming projects.
if sdk_is_top_level then
    set_project("swiftlys2-extension-sdk")
    set_version("0.1.0")
    set_xmakever("3.1.0")
    set_warnings("all", "error")
    add_rules("mode.debug", "mode.release")
end

target("swiftlys2-c")
    set_kind("headeronly")
    set_languages("c11", {public = true})
    add_includedirs("c/include", {public = true})
    add_headerfiles("c/include/(swiftlys2/*.h)")

target("swiftlys2-cpp")
    set_kind("headeronly")
    -- Consumers inherit the standard along with the header search paths.
    set_languages("cxx23", {public = true})
    add_deps("swiftlys2-c", {public = true})
    add_includedirs("cpp/include", {public = true})
    add_headerfiles("cpp/include/(swiftlys2/*.hpp)")
target_end()

-- includes("swiftlys2-extension-sdk") exposes only the two header interfaces.
if sdk_is_top_level then
    target("example-c")
        set_kind("shared")
        set_filename("example-c" .. (is_plat("windows") and ".dll" or ".so"))
        add_deps("swiftlys2-c")
        add_files("examples/c/main.c")

    target("example-cpp")
        set_kind("shared")
        set_filename("example-cpp" .. (is_plat("windows") and ".dll" or ".so"))
        add_deps("swiftlys2-cpp")
        add_files("examples/cpp/main.cpp")

    target("sdk-tests")
        set_kind("binary")
        add_deps("swiftlys2-cpp")
        add_files("tests/sdk_tests.cpp")
        add_tests("contracts")
    target_end()
end
