#!/usr/bin/env bash
set -euo pipefail

out=${1:?Usage: run.sh artifact-directory}
rm -f "$out/result.md"
unset LD_LIBRARY_PATH LD_PRELOAD
cat /etc/os-release
dpkg-query -W libc6:amd64 libstdc++6:amd64

# These are required checks in the Platform image, with no SDK libraries mounted.
"$out/loader" "$out/example-c.so" c
"$out/loader" "$out/example-cpp-default.so" cpp
"$out/loader" "$out/example-cpp-static.so" cpp
"$out/sdk-tests"
"$out/features"

# The dynamically linked variant is a comparison, not the supported release mode.
# Keep its exact result so changes in Valve's toolchain/runtime remain visible.
if "$out/loader" "$out/example-cpp-dynamic.so" cpp > "$out/dynamic.log" 2>&1; then
    comparison='PASS'
else
    status=$?
    if test "$status" -ne 2 || ! grep -Eq 'undefined symbol:|version .* not found' "$out/dynamic.log"; then
        cat "$out/dynamic.log"
        exit "$status"
    fi
    comparison='INCOMPATIBLE (missing runtime symbol/version; see dynamic.log)'
fi
cat "$out/dynamic.log"
cat > "$out/result.md" <<EOF
| Check | Result |
| --- | --- |
| C extension lifecycle | PASS |
| C++ extension with SDK default linking | PASS |
| C++ extension with static libstdc++: lifecycle, reload, failed-load cleanup | PASS |
| SDK contract tests with static libstdc++ | PASS |
| Standard-library feature probes with static libstdc++ | PASS |
| C++ extension with dynamic libstdc++ (informational comparison) | $comparison |

Tested with a mock C ABI host in the SteamRT3 Platform image, including a
preloaded Platform libstdc++. This does not run CS2 or validate game interfaces.
EOF
cat "$out/result.md"
