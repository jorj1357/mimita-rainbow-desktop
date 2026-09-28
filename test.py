#!/usr/bin/env python3
"""Build desktop-fx, run briefly, show log output.

Usage: python test.py [timeout_seconds]
"""

import os, sys, subprocess, time, glob

ROOT = os.path.dirname(os.path.abspath(__file__))

def main():
    timeout = int(sys.argv[1]) if len(sys.argv) > 1 else 5

    # ── Build ──
    r = subprocess.run([sys.executable, 'build.py'], cwd=ROOT, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode != 0:
        print(r.stderr)
        return 1
    if 'FAIL' in r.stdout:
        return 1

    # ── Run ──
    exe = os.path.join(ROOT, 'build', 'desktop-fx.exe')
    logs_before = set(glob.glob(os.path.join(ROOT, 'logs', '*', '*')))

    print(f"\n=== Running overlay for {timeout}s ===")
    proc = subprocess.Popen([exe], cwd=ROOT)
    time.sleep(timeout)

    # Kill both processes
    subprocess.run(['taskkill', '/f', '/im', 'desktop-fx.exe'], capture_output=True)
    subprocess.run(['taskkill', '/f', '/im', 'desktop-fx-window.exe'], capture_output=True)
    try: proc.wait(3)
    except: pass

    # ── Show latest log ──
    logs_after = sorted(glob.glob(os.path.join(ROOT, 'logs', '*', '*')))
    new_logs = [l for l in logs_after if l not in logs_before]

    if new_logs:
        latest = new_logs[-1]
        print(f"\n=== Log: {latest} ===")
        with open(latest) as f:
            print(f.read(), end='')
    else:
        print("\nNo new log files found")
        if logs_after:
            print(f"Most recent: {logs_after[-1]}")

    return 0

if __name__ == '__main__':
    sys.exit(main())
