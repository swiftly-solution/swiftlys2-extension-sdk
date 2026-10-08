"""Build C and C++ extensions with the SDK in a project subdirectory."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xmake", default=shutil.which("xmake"))
    args = parser.parse_args()
    if not args.xmake:
        raise SystemExit("Xmake 3.1+ is required.")
    root = Path(__file__).resolve().parents[1]

    with tempfile.TemporaryDirectory(prefix="swiftlys2-extension-sdk-xmake-") as temporary:
        project = Path(temporary)
        sdk = project / "swiftlys2-extension-sdk"
        sdk.mkdir()
        for directory in ("c", "cpp"):
            shutil.copytree(root / directory, sdk / directory)
        shutil.copy2(root / "xmake.lua", sdk / "xmake.lua")
        shutil.copy2(root / "examples/c/main.c", project / "main.c")
        shutil.copy2(root / "examples/cpp/main.cpp", project / "main.cpp")
        (project / "unrelated.cpp").write_text(
            "#ifdef _MSVC_LANG\n"
            'static_assert(_MSVC_LANG == 201703L, "Consumer standard was changed");\n'
            "#else\n"
            'static_assert(__cplusplus == 201703L, "Consumer standard was changed");\n'
            "#endif\n"
            "int main() {}\n"
        )
        (project / "xmake.lua").write_text(
            'set_project("consumer")\n'
            'set_languages("cxx17")\n'
            'includes("swiftlys2-extension-sdk")\n'
            'target("unrelated")\n'
            '    set_kind("binary")\n'
            '    add_files("unrelated.cpp")\n'
            'target("consumer-c")\n'
            '    set_kind("shared")\n'
            '    set_filename("consumer-c" .. (is_plat("windows") and ".dll" or ".so"))\n'
            '    add_files("main.c")\n'
            '    add_deps("swiftlys2-c")\n'
            'target("consumer-cpp")\n'
            '    set_kind("shared")\n'
            '    set_filename("consumer-cpp" .. (is_plat("windows") and ".dll" or ".so"))\n'
            '    add_files("main.cpp")\n'
            '    add_deps("swiftlys2-cpp")\n'
        )
        env = os.environ.copy()
        env["XMAKE_GLOBALDIR"] = str(project / "global")

        def run(*arguments, capture=False):
            return subprocess.run([args.xmake, *arguments], cwd=project, env=env,
                                  check=True, text=True, capture_output=capture)

        run("f", "-y")
        targets = run("show", "-l", "targets", capture=True).stdout
        if any(name in targets for name in ("example-c", "example-cpp", "sdk-tests")):
            raise AssertionError("SDK examples or tests were enabled in a consumer.")
        run("-y")
        run("project", "-k", "compile_commands", "--lsp=cpptools")
        commands = json.loads((project / "compile_commands.json").read_text())
        if not commands:
            raise AssertionError("Compilation database is empty.")
        for entry in commands:
            arguments = entry.get("command") or " ".join(entry.get("arguments", []))
            if Path(entry["file"]).name == "main.cpp":
                if not any(flag in arguments for flag in
                           ("-std=c++23", "-std=gnu++23", "/std:c++latest",
                            "/std:c++23", "/std:c++23preview")):
                    raise AssertionError("The C++ SDK did not propagate C++23.")
            if Path(entry["file"]).name == "unrelated.cpp":
                if (str(sdk / "c/include") in arguments or str(sdk / "cpp/include") in arguments
                        or "-Werror" in arguments or "/WX" in arguments):
                    raise AssertionError("SDK settings leaked into an unrelated target.")
        suffix = ".dll" if os.name == "nt" else ".so"
        for target in ("consumer-c", "consumer-cpp"):
            if not list((project / "build").rglob(target + suffix)):
                raise AssertionError(f"Expected {target} extension library was not generated.")

    print("Xmake subdirectory integration, C/C++ consumers, C++23 and target isolation passed.")


if __name__ == "__main__":
    main()
