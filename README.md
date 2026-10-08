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

Derive your extension from `sw::extension` and use `SW_DEFINE_EXTENSION` in one
`.cpp` file. See [the C++ example](examples/cpp/main.cpp).

Implement `name()`, `version()`, and `on_load()`. All lifecycle callbacks take no
arguments; access the APIs through the inherited protected `const sw::core& core_`,
such as `core_.memory.alloc(1024)`. It refers to storage owned by the extension and
can be passed to functions or objects accepting `const sw::core&`. Extensions
cannot be copied or moved. The SDK binds the interfaces after construction,
before metadata getters and callbacks. The APIs remain valid through unload and
derived member destruction; they are unavailable during construction.

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
