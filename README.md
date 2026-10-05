# SwiftlyS2 SDK

A standalone SDK for SwiftlyS2 native extensions. It contains the C ABI header,
a header-only C++23 SDK, examples, and an Xmake project template. Extensions need
no SDK library to link, and do not need the game's SDK to use these interfaces.

## Layout

```text
c/include/swiftly/sw_extension.h   C ABI; shared foundation for language SDKs
cpp/include/swiftly/               Header-only C++ SDK
examples/c/                       Minimal C extension
examples/cpp/                     C++ extension with owned resources
templates/c++/swiftly-extension/   Xmake project template
tests/                            Contract and template integration tests
xmake.lua                         Build examples/tests and install the template
```

A future Rust SDK can live in `rust/`, beside `c/` and `cpp/`, and bind to the
same C header. No placeholder crate or second ABI header is needed now.

## Requirements

- C11 for the C example.
- C++23 with `std::expected` for the C++ SDK (for example GCC 13+, with its
  matching standard library). Development was verified with GCC 14.
- Xmake 3.1+ for the included build and template installation.
- Python 3 only for the template integration test.

## Create an extension

From this repository, install the template:

```sh
xmake install-template
```

This copies the template to Xmake's global templates directory. The installation
can be repeated after updating this checkout. It does not bundle a copy of the
SDK into each generated project.

Then, from the directory where you want your project:

```sh
xmake create -l c++ -t swiftly-extension my_extension
cd my_extension
xmake f --sdk=/path/to/swiftlys2-sdk
xmake
```

Alternatively set `SWIFTLY_SDK_DIR` before configuring. Use ordinary project
names such as `my_extension` or `my-extension`. The generated project builds
`my_extension.dll` on Windows or `my_extension.so` on Linux. Copy it to your
SwiftlyS2 server's `extensions/my_extension/` directory.

## C++ API

Include `<swiftly/sdk.hpp>` and add both `c/include` and `cpp/include` to your
include paths. The generated project configures these paths for you.

```cpp
#include <swiftly/sdk.hpp>
#include <utility>

class MyExtension final : public sw::extension {
public:
    std::string_view name() const noexcept override { return "My extension"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    std::string_view author() const noexcept override { return "Your name"; }

    sw::result<void> on_load(sw::core& core) override
    {
        auto allocation = core.memory.allocate(1024);
        if (!allocation)
            return std::unexpected(allocation.error());
        buffer_ = std::move(*allocation);
        return {};
    }

private:
    sw::game_buffer buffer_;
};

SW_DEFINE_EXTENSION(MyExtension)
```

Expand `SW_DEFINE_EXTENSION` in exactly one `.cpp` file. The SDK creates and owns
the extension instance. Public inheritance, default construction, and required
overrides are checked by the compiler; a custom constructor is not necessary.

`name()`, `version()`, and `on_load()` are required. `author()` and `description()`
default to absent metadata. Return string literals or views into object members,
never views into temporary strings. The SDK snapshots and validates the metadata,
then calls the host's `set_info` during initialization.

Optional callbacks:

- `on_all_extensions_loaded(sw::core&)`: invoked initially, then after successful
  loads or unloads of extensions; it can run more than once.
- `on_unload(sw::core&)`: invoked before resources are destroyed. It also runs
  after `on_load` failed or threw, so it must handle partially initialized state.
  If initialization failed before entering `on_load`, only destruction runs.

Initialization exceptions become `SW_E_FAILED`. Lifecycle exceptions are reported
to stderr and caught at the C boundary. An exception in `on_unload` still permits
member resource destruction. Destructors must not throw. Stop and join workers,
and stop callbacks using your state, before unload returns.

Fixed modules are members, and operations return `sw::result<T>`:

| Module | Operations |
| --- | --- |
| `core.memory` | `allocate`, `hook_address`, `hook_vtable`, `gamedata_signature`, `gamedata_offset`, `resolve_signature`, `find_vtable`, `valve_interface` |
| `core.schema` | `offset`, `set_state_changed` |
| `core.shared` | `get`, `set`, `has`, `remove` |
| `core.hot_reloaded` | Cached initialization flag |

Error codes are preserved in `result.error().code`. String inputs accept
`std::string_view`, reject embedded NUL, and are copied to terminated strings.
String copies can throw allocation exceptions inside the extension; the entry
and lifecycle trampolines catch them.

### Resource and pointer rules

- `game_buffer` owns uninitialized bytes, is move-only, and calls the game's
  `free`. `bytes()` provides a `std::span`; it does not construct typed objects.
- `buffer.resize(nonzero)` preserves the existing allocation on failure,
  including a host response of `SW_OK` with a null pointer. `allocate(0)` creates
  an empty buffer; `resize(0)` frees, avoiding allocator-specific zero-size rules.
- `address_hook<Fn>` and `vtable_hook<Fn>` are move-only and use their matching
  unhook operation. `original()` is invalid after unhook. The target signature
  and calling convention must match `Fn`; function-pointer conversions follow
  the supported Windows/Linux platform ABI.
- `reset()` returns a result for explicit cleanup. Destructors also reset; valid
  resource handles must be released while the host interface is still alive.
- Interfaces, signature addresses, Valve interfaces and registry values are
  borrowed. Do not free them or retain them beyond their owner's lifetime.
- A schema offset of zero is valid. A successful registry lookup can return
  null; a missing key is an error. Registry operations do not transfer ownership.
- Use the game thread for `set_state_changed`. The SDK adds no thread-safety
  guarantees to the host. Remove keys you publish before destroying their values.

See `examples/cpp/main.cpp` for a complete extension and `examples/c/main.c` for
direct C API usage. C consumers only need `c/include` and `<swiftly/sw_extension.h>`.

## Build and verify

```sh
xmake f -m debug
xmake
xmake test
python3 tests/check_template.py
```

The tests use a fake host to exercise allocator ownership, resize failure,
hook cleanup, lookup semantics, metadata registration and initialization/unload
failures. The integration test installs the template into a temporary Xmake
global directory, creates an extension, and builds its shared library.

## C ABI provenance and updates

The C header is copied verbatim from `src/api/extension/sw_extension.h` in the
local SwiftlyS2 working tree based on commit
`860e207aa0c49a20bc192d25629bb9d1b25ac22c`, including the working-tree rename to
`sw_schema::get_offset`. Its SHA-256 is
`84605eee530e2055ad4a3512ac8f489605b2b516ef50226e00dfe22000b67780`.

This snapshot requires the `SwCore00001` layout containing `core->shared` and the
initialization lifecycle callbacks. Earlier layouts carrying the same interface
name are incompatible; the ABI has no table size with which to detect them.
Use a host built with the matching layout. This repository does not modify the
host or introduce a different ABI.

When updating, replace this one C header from the intended host version, update
the provenance above, review the C++ bindings, and run the tests. Future language
bindings should derive from this same header.

License files retain the [SwiftlyS2 upstream](https://github.com/swiftly-solution/swiftlys2)
terms, including its existing exception. See `LICENSE` and `LICENSE.MIT`.
