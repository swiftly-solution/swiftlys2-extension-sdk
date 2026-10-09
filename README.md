# Contents

- [Requirements](#requirements)
- [Loading extensions](#loading-extensions)
- [C](#c)
  - [Xmake](#xmake)
  - [CMake](#cmake)
- [C++](#c-1)
  - [Xmake](#xmake-1)
  - [CMake](#cmake-1)
- [SteamRT3 compatibility CI](#steamrt3-compatibility-ci)

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

Clone this repository into your project as `swiftlys2-extension-sdk`, then include the C ABI header:

```c
#include <swiftlys2/sw_extension.h>
```

Export `sw_extension_init` to obtain the core interfaces and register your extension.
See [the C example](examples/c/main.c).

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

Clone this repository into your project as `swiftlys2-extension-sdk`, then include the C++ SDK:

```cpp
#include <swiftlys2/sdk.hpp>
```

Derive your extension from `sw::Extension` and use `SW_DEFINE_EXTENSION` in one
`.cpp` file. See [the C++ example](examples/cpp/main.cpp).

Implement `Name()`, `Version()`, and `OnLoad(sw::Core core)`. The SDK passes a
copyable host API view by value to `OnLoad`; access its interfaces through
`Memory()`, `Schema()`, `Shared()`, and `Misc()`, such as
`core.Schema().GetOffset("CBaseEntity", "m_iHealth")` or `core.Memory().Alloc(1024)`.
`Misc().IsHotReloaded()` returns the hot reload state cached during initialization.

Fallible operations return `sw::Result<T>` (`std::expected<T, sw::Error>`).
Use `result.error().code()` for the `sw_status` and `result.error().msg()` for
the detailed diagnostic string. Host failures preserve their original status,
including unknown codes. Local argument validation uses `SW_E_INVALID_ARG`;
other local failures, including unavailable interfaces or callbacks, use
`SW_E_FAILED`. Status meanings still depend on the operation: for example,
`SW_E_INVALID_ARG` can also mean a requested key does not exist.

Propagate errors with `return std::unexpected(result.error());`, or create one:

```cpp
return std::unexpected(sw::Error(SW_E_FAILED, "Required service is unavailable"));
```

`OnLoad` errors are logged and reported as `SW_E_FAILED` by the C entry point.

`Extension` does not store or expose a core member. If `OnAllExtensionsLoaded()`,
`OnUnload()`, or member destructors need the APIs, save your own `sw::Core` copy,
for example in a `std::optional<sw::Core>`. Do not retain a pointer or reference to
the `OnLoad` parameter. Copying the view does not copy or own the host interfaces
or extend their lifetime. The APIs remain valid during `OnUnload()` and extension
destruction, including after a failed or throwing `OnLoad`, and must not be used
after unload. Save the view before initialization work that may fail if cleanup
will need it.

`OnAllExtensionsLoaded()` and `OnUnload()` take no arguments. If initialization
fails before `OnLoad` begins, only destruction runs and no core view has been
supplied. Host APIs are unavailable in constructors and metadata getters.
Extensions cannot be copied or moved.

C++ types and public functions use PascalCase. Parameters and local variables
use snake_case; internal members use snake_case with a trailing underscore.
`sw::Error` uses the accessors `code()` and `msg()`.
The C ABI keeps its existing names.

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

# SteamRT3 compatibility CI

[SteamRT3 compatibility](.github/workflows/steamrt3.yml) builds with GCC 14 in a
pinned Valve SDK image and runs the binaries in the matching Platform image.
It checks C/C++ extension loading and cleanup through a mock C ABI host, the SDK
contract tests, and `std::expected`/`std::print`. The C++26 job additionally checks
saturation arithmetic supported by GCC 14; it does not certify all C++26 features.

SDK-default and explicit static `libstdc++` builds must pass. This SDK snapshot's
GCC 14 defaults to static C++ runtime libraries. Forced linking against the
Platform's shared `libstdc++` is an informational compatibility comparison.
Each run uploads binaries, dependency reports and logs, and displays
the results in its summary. These checks cover runtime compatibility, not CS2
game interfaces. The workflow runs on pushes, pull requests and manual dispatch.
