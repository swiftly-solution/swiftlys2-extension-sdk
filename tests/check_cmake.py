"""Build consumers from an SDK subdirectory and a relocated CMake installation."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    args = parser.parse_args()
    if not args.cmake:
        raise SystemExit("CMake 3.20+ is required.")
    root = Path(__file__).resolve().parents[1]

    def run(*arguments):
        subprocess.run([args.cmake, *map(str, arguments)], check=True)

    with tempfile.TemporaryDirectory(prefix="swiftlys2-extension-sdk-cmake-") as temporary:
        work = Path(temporary)

        def build_consumer(name, language, prefix=None):
            project = work / name
            project.mkdir()
            target = "consumer-" + language.lower()
            source = project / ("main.c" if language == "C" else "main.cpp")
            shutil.copy2(root / ("examples/c/main.c" if language == "C" else "examples/cpp/main.cpp"), source)
            interface = "c" if language == "C" else "cpp"
            if prefix:
                dependency = "find_package(SwiftlyS2ExtensionSDK 0.1 CONFIG REQUIRED)"
            else:
                sdk = project / "swiftlys2-extension-sdk"
                sdk.mkdir()
                for directory in ("c", "cpp", "cmake"):
                    shutil.copytree(root / directory, sdk / directory)
                shutil.copy2(root / "CMakeLists.txt", sdk / "CMakeLists.txt")
                dependency = "add_subdirectory(swiftlys2-extension-sdk)"
            (project / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\n"
                f"project(Consumer LANGUAGES {language})\n"
                f"{dependency}\n"
                # Adding the SDK to an extension must not enable its examples or tests.
                "if(TARGET example-c OR TARGET example-cpp OR TARGET sdk-tests)\n"
                '    message(FATAL_ERROR "SDK examples or tests were enabled in a consumer")\n'
                "endif()\n"
                f'add_library({target} MODULE "{source.as_posix()}")\n'
                f"target_link_libraries({target} PRIVATE swiftlys2::{interface})\n"
                f'set_target_properties({target} PROPERTIES PREFIX "" C_EXTENSIONS OFF CXX_EXTENSIONS OFF)\n'
            )
            build = project / "build"
            options = [f"-DCMAKE_PREFIX_PATH={prefix}"] if prefix else []
            run("-S", project, "-B", build, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", *options)
            run("--build", build, "--config", "Debug", "--parallel", "2")
            suffix = ".dll" if os.name == "nt" else ".so"
            if not list(build.rglob(target + suffix)):
                raise AssertionError("Expected host-compatible extension filename was not generated.")
            database = build / "compile_commands.json"
            if database.exists():
                commands = json.loads(database.read_text())
                arguments = " ".join(entry.get("command") or " ".join(entry.get("arguments", []))
                                     for entry in commands)
                if prefix and str(root / "cpp/include") in arguments:
                    raise AssertionError("Installed package still refers to checkout headers.")

        for language in ("C", "CXX"):
            build_consumer("checkout-" + language.lower(), language)

        package_build = work / "package-build"
        prefix = work / "install"
        run("-S", root, "-B", package_build, f"-DCMAKE_INSTALL_PREFIX={prefix}",
            "-DSWIFTLYS2_EXTENSION_SDK_BUILD_EXAMPLES=OFF", "-DSWIFTLYS2_EXTENSION_SDK_BUILD_TESTS=OFF")
        run("--install", package_build)
        # Moving the prefix verifies that exported header paths are relocatable.
        relocated = work / "relocated"
        prefix.rename(relocated)
        for language in ("C", "CXX"):
            build_consumer("installed-" + language.lower(), language, relocated)

    print("CMake subdirectory integration, relocated installation, C/C++ consumers and C++23 passed.")


if __name__ == "__main__":
    main()
