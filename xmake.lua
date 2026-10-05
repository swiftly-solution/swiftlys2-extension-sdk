set_project("swiftlys2-sdk")
set_version("0.1.0")
set_xmakever("3.1.0")
set_languages("cxx23")
set_warnings("all", "error")
add_rules("mode.debug", "mode.release")

target("swiftly-c")
    set_kind("headeronly")
    add_includedirs("c/include", {public = true})
    add_headerfiles("c/include/(swiftly/*.h)")

target("swiftly-cpp")
    set_kind("headeronly")
    add_deps("swiftly-c", {public = true})
    add_includedirs("cpp/include", {public = true})
    add_headerfiles("cpp/include/(swiftly/*.hpp)")

target("example-c")
    set_kind("shared")
    set_filename("example-c" .. (is_plat("windows") and ".dll" or ".so"))
    set_languages("c11")
    add_deps("swiftly-c")
    add_files("examples/c/main.c")

target("example-cpp")
    set_kind("shared")
    set_filename("example-cpp" .. (is_plat("windows") and ".dll" or ".so"))
    add_deps("swiftly-cpp")
    add_files("examples/cpp/main.cpp")

target("sdk-tests")
    set_kind("binary")
    add_deps("swiftly-cpp")
    add_files("tests/sdk_tests.cpp")
    add_tests("contracts")

task("install-template")
    set_menu {
        usage = "xmake install-template",
        description = "Install the SwiftlyS2 C++ project template for xmake create."
    }
    on_run(function ()
        import("core.base.global")
        local source = path.join(os.projectdir(), "templates", "c++", "swiftly-extension")
        local destination = path.join(global.directory(), "templates", "c++", "swiftly-extension")
        os.mkdir(path.directory(destination))
        os.cp(source, path.directory(destination))
        print("Installed template: %s", destination)
        print("Create a project: xmake create -l c++ -t swiftly-extension my_extension")
    end)
