"""CI helper: downloads an SDK zip, extracts it and finds the SDK root.

    python tools/ci_fetch_sdk.py <url> <dest-dir> <marker-file> <levels-up> <env-var>

Zips made by Windows PowerShell's Compress-Archive store paths with
backslashes; they are turned into folders here on every platform. The SDK root
is <levels-up> folders above the shallowest <marker-file> (nesting depth does
not matter) and is written to $GITHUB_ENV as <env-var>.
"""
import os
import sys
import urllib.request
import zipfile


def main():
    url, dest, marker, levels, var = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5]
    os.makedirs(dest, exist_ok=True)
    archive = dest.rstrip("/\\") + ".zip"
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request) as response, open(archive, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    print(f"downloaded {os.path.getsize(archive)} bytes")
    if not zipfile.is_zipfile(archive):
        sys.exit(f"{var}: the link did not return a zip file (a Dropbox link must end in dl=1)")

    root_abs = os.path.abspath(dest)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            name = info.filename.replace("\\", "/")
            target = os.path.abspath(os.path.join(dest, name))
            if not target.startswith(root_abs + os.sep):
                continue  # never write outside dest
            if name.endswith("/"):
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as src, open(target, "wb") as out:
                out.write(src.read())

    found = []
    for folder, _, files in os.walk(dest):
        if marker in files:
            found.append(os.path.join(folder, marker))
    if not found:
        print(f"{marker} not found. Top of the archive:")
        for folder, dirs, _ in os.walk(dest):
            depth = os.path.relpath(folder, dest).count(os.sep)
            if depth < 3:
                print("  " + os.path.relpath(folder, dest))
        sys.exit(1)
    root = min(found, key=lambda p: p.count(os.sep))
    for _ in range(levels):
        root = os.path.dirname(root)
    root = os.path.abspath(root).replace("\\", "/")  # CMake-friendly on Windows too
    print(f"{var}={root}")
    env = os.environ.get("GITHUB_ENV")
    if env:
        with open(env, "a", encoding="utf-8") as f:
            f.write(f"{var}={root}\n")


if __name__ == "__main__":
    main()
