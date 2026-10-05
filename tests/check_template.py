"""Create and build a real extension using an isolated Xmake template installation."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    xmake = shutil.which("xmake")
    if xmake is None:
        raise SystemExit("Xmake 3.1+ is required.")

    with tempfile.TemporaryDirectory(prefix="swiftlys2-sdk-template-") as temporary:
        work = Path(temporary)
        env = os.environ.copy()
        env["XMAKE_GLOBALDIR"] = str(work / "global")
        env.pop("SWIFTLY_SDK_DIR", None)

        def run(*arguments, cwd=root):
            subprocess.run([xmake, *arguments], cwd=cwd, env=env, check=True)

        run("install-template")
        run("install-template")  # Reinstallation must not nest the template directory.
        installed = work / "global/.xmake/templates/c++/swiftly-extension"
        if (installed / "swiftly-extension").exists():
            raise AssertionError("Template installation created a nested directory.")

        project = work / "demo-extension"
        run("create", "-l", "c++", "-t", "swiftly-extension",
            "-P", str(project), "demo-extension")
        source = (project / "src/main.cpp").read_text()
        if '${TARGET_NAME}' in source or 'return "demo-extension"' not in source:
            raise AssertionError("Project name was not substituted.")
        run("f", "-y", "--sdk=" + str(root), cwd=project)
        run("-y", cwd=project)

        extension = ".dll" if os.name == "nt" else ".so"
        if not list((project / "build").rglob("demo-extension" + extension)):
            raise AssertionError("Expected host-compatible library filename was not generated.")

    print("Template installation, project creation and extension build passed.")


if __name__ == "__main__":
    main()
