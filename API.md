# API

## Contents

- [C](#c)
- [C++](#c-1)
  - [Core interfaces](#core-interfaces)
  - [Error handling](#error-handling)
  - [Lifecycle](#lifecycle)
  - [Naming](#naming)

## C

Include the C ABI header:

```c
#include <swiftlys2/sw_extension.h>
```

Export `sw_extension_init` to obtain the core interfaces and register your extension.
See [the C example](examples/c/main.c).

## C++

Include the C++ SDK:

```cpp
#include <swiftlys2/sdk.hpp>
```

Derive your extension from `sw::Extension` and use `SW_DEFINE_EXTENSION` in one
`.cpp` file. See [the C++ example](examples/cpp/main.cpp).

### Core interfaces

Implement `Name()`, `Version()`, and `OnLoad(sw::Core core)`. The SDK passes a
copyable host API view by value to `OnLoad`; access its interfaces through
`Memory()`, `Schema()`, `Shared()`, and `Misc()`, such as
`core.Schema().GetOffset("CBaseEntity", "m_iHealth")` or `core.Memory().Alloc(1024)`.
`Misc().IsHotReloaded()` returns the hot reload state cached during initialization.

### Error handling

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

### Lifecycle

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

### Naming

C++ types and public functions use PascalCase. Parameters and local variables
use snake_case; internal members use snake_case with a trailing underscore.
`sw::Error` uses the accessors `code()` and `msg()`.
The C ABI keeps its existing names.
