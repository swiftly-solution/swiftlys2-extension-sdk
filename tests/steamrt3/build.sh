#!/usr/bin/env bash
set -euo pipefail

standard=${1:?Usage: build.sh c++23|c++2c output-directory}
out=${2:?Missing output directory}
case "$standard" in
    c++23) feature_flags=() ;;
    c++2c) feature_flags=(-DTEST_CPP26) ;;
    *) echo "Unsupported standard: $standard" >&2; exit 1 ;;
esac
mkdir -p "$out"

gcc-10 --version
g++-14 --version
cat /etc/os-release
dpkg-query -W libc6:amd64 libstdc++6:amd64

cflags=(-std=c11 -O2 -Wall -Wextra -Werror -I c/include)
cxxflags=(-std="$standard" -O2 -Wall -Wextra -Werror -I c/include -I cpp/include)
module_flags=(-fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden
    -Wl,--version-script=tests/steamrt3/exports.map)

gcc-10 "${cflags[@]}" tests/steamrt3/loader.c -ldl -o "$out/loader"
gcc-10 "${cflags[@]}" -fPIC -shared examples/c/main.c -o "$out/example-c.so"
g++-14 "${cxxflags[@]}" "${module_flags[@]}" -Wl,-z,defs examples/cpp/main.cpp \
    -o "$out/example-cpp-default.so"
# Valve's GCC 14 defaults to static C++ runtime libraries. Use the C linker
# driver and GCC 10's shared libstdc++ explicitly for a genuine dynamic control.
g++-14 "${cxxflags[@]}" -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
    -c examples/cpp/main.cpp -o "$out/example-cpp.o"
gcc-10 -shared -shared-libgcc -Wl,--version-script=tests/steamrt3/exports.map \
    "$out/example-cpp.o" -lstdc++ -lm -o "$out/example-cpp-dynamic.so"
g++-14 "${cxxflags[@]}" "${module_flags[@]}" -static-libstdc++ \
    -Wl,--exclude-libs,ALL -Wl,-z,defs examples/cpp/main.cpp \
    -o "$out/example-cpp-static.so"
g++-14 "${cxxflags[@]}" -static-libstdc++ tests/sdk_tests.cpp -o "$out/sdk-tests"
g++-14 "${cxxflags[@]}" "${feature_flags[@]}" -static-libstdc++ \
    tests/steamrt3/features.cpp -o "$out/features"

# Save the actual dependencies, versioned symbols and exports for CI artifacts.
for file in loader example-c.so example-cpp-default.so example-cpp-dynamic.so example-cpp-static.so sdk-tests features; do
    {
        readelf -d "$out/$file"
        readelf --version-info "$out/$file"
        readelf --dyn-syms --wide "$out/$file"
    } > "$out/$file.elf.txt"
done
if ! readelf -d "$out/example-cpp-dynamic.so" | grep -Fq 'Shared library: [libstdc++.so.6]'; then
    echo 'Dynamic comparison did not actually link shared libstdc++' >&2
    exit 1
fi
for file in example-cpp-static.so sdk-tests features; do
    if readelf -d "$out/$file" | grep -Fq 'Shared library: [libstdc++.so.6]'; then
        echo "Unexpected dynamic libstdc++ dependency: $file" >&2
        exit 1
    fi
done
