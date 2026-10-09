# Contents

- [Requirements](#requirements)
- [Loading extensions](#loading-extensions)
- [C](#c)
  - [Xmake](#xmake)
  - [CMake](#cmake)
- [C++](#c-1)
  - [Xmake](#xmake-1)
  - [CMake](#cmake-1)

# Requirements

- C11 for C extensions.
- C++23 with `std::expected` for C++ extensions, such as GCC 13+ with its matching standard library.
- Xmake 3.1+ or CMake 3.20+.

# Loading extensions

Place the built library under the
SwiftlyS2 directory (default: `game/csgo/addons/swiftlys2`) as:

```text
extensions/
└── my_extension/
    └── my_extension.so    # Windows: my_extension.dll
```

The folder name is the extension ID and must match the library filename without
its suffix (no `lib` prefix). SwiftlyS2 automatically scans these folders at
startup; libraries placed directly in `extensions/` are ignored. If you use
`-sw_path`, place `extensions/` under that custom SwiftlyS2 directory.

From a managed plugin, use `Core.Extension.Load("my_extension")` or
`Core.Extension.LoadFromPath(absolutePath)` to load at runtime. Path-based loading
requires an absolute library path and uses its filename without the suffix as
the ID. Loading an already loaded ID or library fails; call
`Core.Extension.Unload("my_extension")` before
loading it again.

# C

Clone this repository into your project as `swiftlys2-extension-sdk`.
See the [C example](examples/c/main.c) and [API reference](API.md#c).

## Xmake

Add `includes("swiftlys2-extension-sdk")` before your target and `add_deps("swiftlys2-c")`
inside it. The dependency supplies the include path and C11.

```lua
set_project("my_extension")
add_rules("mode.debug", "mode.release")

includes("swiftlys2-extension-sdk")

target("my_extension")
    set_kind("shared")
    set_filename("my_extension" .. (is_plat("windows") and ".dll" or ".so"))
    add_files("src/*.c")
    add_deps("swiftlys2-c")
```

Build:

```sh
xmake f -m debug
xmake
```

## CMake

Add `add_subdirectory(swiftlys2-extension-sdk)` and link your target to `swiftlys2::c`.
The dependency supplies the include path and C11.

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_extension LANGUAGES C)

add_subdirectory(swiftlys2-extension-sdk)
add_library(my_extension MODULE src/main.c)
target_link_libraries(my_extension PRIVATE swiftlys2::c)
set_target_properties(my_extension PROPERTIES PREFIX "" C_EXTENSIONS OFF)
```

Build:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
```

# C++

Clone this repository into your project as `swiftlys2-extension-sdk`.
See the [C++ example](examples/cpp/main.cpp) and [API reference](API.md#c-1).

## Xmake

Add `includes("swiftlys2-extension-sdk")` before your target and `add_deps("swiftlys2-cpp")`
inside it. The dependency supplies the include paths and C++23.

```lua
set_project("my_extension")
add_rules("mode.debug", "mode.release")

includes("swiftlys2-extension-sdk")

target("my_extension")
    set_kind("shared")
    set_filename("my_extension" .. (is_plat("windows") and ".dll" or ".so"))
    add_files("src/*.cpp")
    add_deps("swiftlys2-cpp")
```

Build:

```sh
xmake f -m debug
xmake
```

## CMake

Add `add_subdirectory(swiftlys2-extension-sdk)` and link your target to `swiftlys2::cpp`.
The dependency supplies the include paths and C++23.

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_extension LANGUAGES CXX)

add_subdirectory(swiftlys2-extension-sdk)
add_library(my_extension MODULE src/main.cpp)
target_link_libraries(my_extension PRIVATE swiftlys2::cpp)
set_target_properties(my_extension PROPERTIES PREFIX "" CXX_EXTENSIONS OFF)
```

Build:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
```
