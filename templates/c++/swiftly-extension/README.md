This extension uses the SwiftlyS2 C++23 SDK.

Configure and build (replace the SDK path):

```sh
xmake f --sdk=/path/to/swiftlys2-sdk
xmake
```

Alternatively set `SWIFTLY_SDK_DIR` before configuring.

Edit `src/main.cpp` to implement metadata and lifecycle callbacks. The entry macro
must appear in exactly one source file. Copy the built library into
`extensions/<project-name>/<project-name>.dll` or `<project-name>.so` on your server.
