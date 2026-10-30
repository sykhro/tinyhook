import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import urllib.request


def main():
    request = urllib.request.Request(
        "https://api.github.com/repos/llvm/llvm-project/releases/latest",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
            "User-Agent": "tinyhook-ci",
        },
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        release = json.load(response)
    version = release["tag_name"].removeprefix("llvmorg-")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise RuntimeError(f"Unexpected LLVM release: {release['tag_name']}")

    system = platform.system()
    architecture = {"x86_64": "X64", "amd64": "X64", "arm64": "ARM64", "aarch64": "ARM64"}[
        platform.machine().lower()
    ]
    if system == "Windows":
        triple = {"X64": "x86_64", "ARM64": "aarch64"}[architecture]
        name = f"clang+llvm-{version}-{triple}-pc-windows-msvc.tar.xz"
    else:
        name = f"LLVM-{version}-{'macOS' if system == 'Darwin' else system}-{architecture}.tar.xz"
    asset = next(asset for asset in release["assets"] if asset["name"] == name)

    directory = pathlib.Path(os.environ["RUNNER_TEMP"]) / "llvm"
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory.parent / "llvm.tar.xz"
    print(f"Installing LLVM {version}: {name}", flush=True)
    with urllib.request.urlopen(asset["browser_download_url"], timeout=120) as response:
        with archive.open("wb") as output:
            shutil.copyfileobj(response, output)
    subprocess.run(["tar", "-xf", str(archive), "-C", str(directory)], check=True)

    compiler_name = "clang-cl.exe" if system == "Windows" else "clang"
    compiler = next(path for path in directory.rglob(compiler_name) if path.parent.name == "bin")
    output = subprocess.check_output([str(compiler), "--version"], text=True)
    print(output, end="", flush=True)
    if f"clang version {version}" not in output:
        raise RuntimeError("Installed Clang does not match the latest LLVM release")
    with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
        output.write(f"{compiler.parent}\n")
    if system != "Windows":
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
            output.write(f"CC={compiler}\nCXX={compiler.parent / 'clang++'}\n")


if __name__ == "__main__":
    main()
