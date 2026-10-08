# Contents

- [Requirements](#requirements)
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
