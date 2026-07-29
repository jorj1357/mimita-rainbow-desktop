import os, sys, subprocess, time, json, hashlib
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
BUILD = os.path.join(ROOT, "build")
OBJ = os.path.join(BUILD, "obj")
CACHE = os.path.join(BUILD, "build_cache.json")

VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
LIBS = "user32.lib gdi32.lib kernel32.lib d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib windowsapp.lib comctl32.lib"

# SDK version
SDK_VER = "10.0.26100.0"
SDK_BASE = r"C:\Program Files (x86)\Windows Kits\10"
CPPWINRT_INC = f'{SDK_BASE}\\Include\\{SDK_VER}\\cppwinrt'


def setup_env():
    proc = subprocess.run(
        f'call "{VCVARS}" >nul && set',
        shell=True, capture_output=True, text=True
    )
    for line in proc.stdout.strip().split("\n"):
        if "=" in line:
            k, v = line.split("=", 1)
            os.environ[k] = v


def hash_file(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def obj_path(src):
    rel = os.path.relpath(src, SRC).replace("\\", "_").replace("/", "_")
    return os.path.join(OBJ, rel + ".obj")


def get_deps(src):
    deps = [src]
    h_path = os.path.splitext(src)[0] + ".h"
    if os.path.exists(h_path):
        deps.append(h_path)
    return deps


def needs_rebuild(src, cache):
    obj = obj_path(src)
    if not os.path.exists(obj):
        return True

    obj_mtime = os.path.getmtime(obj)
    for dep in get_deps(src):
        if not os.path.exists(dep) or os.path.getmtime(dep) >= obj_mtime:
            return True

    src_hash = hash_file(src)
    cached = cache.get(src)
    if cached != src_hash:
        return True

    return False


def compile_cpp(src):
    obj = obj_path(src)
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    rel = os.path.relpath(src, ROOT)

    # Add C++/WinRT include path and /await for coroutine support
    cmd = (
        f'cl /nologo /O2 /EHsc /std:c++17 '
        f'/I"third_party" '
        f'/I"{CPPWINRT_INC}" '
        f'/c "{src}" /Fo"{obj}" 2>&1'
    )
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        return ("fail", rel, result.stdout + result.stderr)
    return ("ok", rel, obj)


def main():
    t0 = time.time()
    print("=== desktop-fx build ===")
    print()

    setup_env()
    os.makedirs(OBJ, exist_ok=True)
    os.makedirs(os.path.join(BUILD, "logs"), exist_ok=True)

    cache = {}
    if os.path.exists(CACHE):
        try:
            cache = json.load(open(CACHE))
        except:
            cache = {}

    cpp_files = sorted([
        os.path.join(SRC, f) for f in os.listdir(SRC)
        if f.endswith(".cpp")
    ])

    to_compile = []
    for src in cpp_files:
        if needs_rebuild(src, cache):
            to_compile.append(src)

    if not to_compile:
        print("Nothing changed.")
    else:
        print(f"Compiling {len(to_compile)} files...")
        compiled = 0
        failed = False

        with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
            futures = {pool.submit(compile_cpp, src): src for src in to_compile}
            for future in as_completed(futures):
                status, rel, info = future.result()
                if status == "ok":
                    print(f"  OK  {rel}")
                    compiled += 1
                    cache[futures[future]] = hash_file(futures[future])
                else:
                    print(f"  FAIL {rel}")
                    print(info)
                    failed = True

        if failed:
            print("\nBuild FAILED")
            sys.exit(1)

        json.dump(cache, open(CACHE, "w"))

    print("\nLinking...")
    objs = [obj_path(s) for s in cpp_files]
    objs_str = " ".join(f'"{o}"' for o in objs)
    exe = os.path.join(BUILD, "desktop-fx.exe")

    cmd = f'link /nologo /SUBSYSTEM:WINDOWS {objs_str} /out:"{exe}" {LIBS} 2>&1'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)

    if result.returncode != 0:
        print("Link FAILED:")
        print(result.stdout + result.stderr)
        sys.exit(1)

    t = time.time() - t0
    print(f"Build OK: {exe} ({t:.1f}s)")
    print(f"Launching {exe}...")
    subprocess.Popen([exe], shell=False, creationflags=subprocess.DETACHED_PROCESS)


if __name__ == "__main__":
    main()
