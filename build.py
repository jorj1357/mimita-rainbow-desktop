import os, sys, subprocess, time, json, hashlib
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
BUILD = os.path.join(ROOT, "build")
OBJ = os.path.join(BUILD, "obj")
CACHE = os.path.join(BUILD, "build_cache.json")

VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
LIBS = "user32.lib gdi32.lib kernel32.lib d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib windowsapp.lib comctl32.lib"

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


def compile_cpp(src, extra_flags=""):
    obj = obj_path(src)
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    rel = os.path.relpath(src, ROOT)

    cmd = (
        f'cl /nologo /O2 /EHsc /std:c++17 '
        f'/I"third_party" '
        f'/I"{CPPWINRT_INC}" '
        f'{extra_flags} '
        f'/c "{src}" /Fo"{obj}" 2>&1'
    )
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        return ("fail", rel, result.stdout + result.stderr)
    return ("ok", rel, obj)


def link_console_exe(objs, exe_name, extra_libs=""):
    exe_path = os.path.join(BUILD, exe_name)
    objs_str = " ".join(f'"{o}"' for o in objs)
    cmd = f'link /nologo /SUBSYSTEM:CONSOLE {objs_str} /out:"{exe_path}" kernel32.lib {extra_libs} 2>&1'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        return False, result.stdout + result.stderr
    return True, exe_path


def link_exe(objs, exe_name, extra_libs=""):
    exe_path = os.path.join(BUILD, exe_name)
    objs_str = " ".join(f'"{o}"' for o in objs)
    cmd = f'link /nologo /SUBSYSTEM:WINDOWS {objs_str} /out:"{exe_path}" {LIBS} {extra_libs} 2>&1'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        return False, result.stdout + result.stderr
    return True, exe_path


def main():
    t0 = time.time()
    print("=== desktop-fx build ===")
    print()
    setup_env()
    os.makedirs(OBJ, exist_ok=True)

    cache = {}
    if os.path.exists(CACHE):
        try: cache = json.load(open(CACHE))
        except: cache = {}

    all_cpp = sorted([os.path.join(SRC, f) for f in os.listdir(SRC) if f.endswith(".cpp")])

    # Separate into main exe files and output window exe files
    main_exe_files = [f for f in all_cpp if "output_window" not in f and "temporal_test" not in f]
    win_exe_files = [f for f in all_cpp if "output_window" in f]
    # output_sharing.cpp goes with main exe (it provides the shared memory that main uses)

    def build_exe(files, exe_name, extra_flags="", extra_libs=""):
        to_compile = [f for f in files if needs_rebuild(f, cache)]
        compiled_objs = []

        if to_compile:
            print(f"  Compiling ({exe_name})...")
            with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
                futures = {pool.submit(compile_cpp, f, extra_flags): f for f in to_compile}
                for future in as_completed(futures):
                    status, rel, info = future.result()
                    if status == "ok":
                        print(f"    OK  {rel}")
                        cache[futures[future]] = hash_file(futures[future])
                    else:
                        print(f"    FAIL {rel}")
                        print(info)
                        return False, None

        for f in files:
            compiled_objs.append(obj_path(f))

        ok, result = link_exe(compiled_objs, exe_name, extra_libs)
        if ok:
            print(f"  {exe_name} -> {result}")
        else:
            print(f"  LINK FAILED ({exe_name}):")
            print(result)
        return ok, result

    # Build main executable
    print("desktop-fx.exe:")
    ok1, _ = build_exe(main_exe_files, "desktop-fx.exe")

    # Build output window executable (needs output_sharing.cpp too)
    print("\ndesktop-fx-window.exe:")
    window_files = win_exe_files + [f for f in main_exe_files if "output_sharing" in f] \
                   + [f for f in main_exe_files if "log" in f]
    ok2, _ = build_exe(window_files, "desktop-fx-window.exe",
                        extra_flags="",
                        extra_libs="d3dcompiler.lib")

    # Build temporal test
    test_ok = True
    test_src = os.path.join(SRC, "temporal_test.cpp")
    if os.path.exists(test_src):
        print("\ntemporal_test.exe:")
        to_compile = [test_src] if needs_rebuild(test_src, cache) else []
        if to_compile:
            status, rel, info = compile_cpp(test_src)
            if status == "ok":
                print(f"    OK  {rel}")
                cache[test_src] = hash_file(test_src)
            else:
                print(f"    FAIL {rel}")
                print(info)
                test_ok = False
        if test_ok:
            test_obj = obj_path(test_src)
            ok_link, link_result = link_console_exe([test_obj], "temporal_test.exe")
            if ok_link:
                print(f"  temporal_test.exe -> {link_result}")
                # Run the test
                test_exe = os.path.join(BUILD, "temporal_test.exe")
                tr = subprocess.run([test_exe], capture_output=True, text=True, cwd=BUILD)
                print(tr.stdout)
                if tr.returncode != 0:
                    print(f"  TEST FAILED (exit code {tr.returncode})")
                    test_ok = False
                else:
                    print("  TEST PASSED")
            else:
                print(f"  LINK FAILED:")
                print(link_result)
                test_ok = False

    json.dump(cache, open(CACHE, "w"))

    t = time.time() - t0
    status_str = "ALL OK" if (ok1 and ok2 and test_ok) else "SOME FAILURES"
    print(f"\n{status_str} | Main={'OK' if ok1 else 'FAIL'} Window={'OK' if ok2 else 'FAIL'} Test={'OK' if test_ok else 'FAIL'} ({t:.1f}s)")


if __name__ == "__main__":
    main()
