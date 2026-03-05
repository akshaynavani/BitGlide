#!/usr/bin/env python3
"""Run BitGlide against the shaped HLS origin for each bandwidth profile and check QoE.

For every profile: start hls_origin.py on a free port, run the bitglide binary with
--report, load the JSON session report and evaluate the profile's "expect" block.
Exits non-zero if any scenario fails its expectations.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def start_origin(media: Path, profile: Path, verbose: bool) -> tuple[subprocess.Popen, str]:
    cmd = [sys.executable, str(HERE / "hls_origin.py"), "--root", str(media), "--port", "0", "--profile", str(profile)]
    if verbose:
        cmd.append("--verbose")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if line.startswith("LISTENING "):
            return proc, line.split()[1]
        if proc.poll() is not None:
            break
    proc.kill()
    raise RuntimeError(f"origin failed to start for {profile.name}")


def check(expect: dict, report: dict, exit_code: int) -> list[str]:
    problems = []
    avg_kbps = report["avg_bitrate_bps"] / 1000.0

    def bound(key, actual, op):
        if key in expect:
            limit = expect[key]
            ok = actual <= limit if op == "max" else actual >= limit
            if not ok:
                problems.append(f"{key}={limit} violated (actual {actual:.2f})")

    bound("max_stalls", report["stall_count"], "max")
    bound("max_stall_time_s", report["stall_time_s"], "max")
    bound("max_switches", report["switch_count"], "max")
    bound("min_switches", report["switch_count"], "min")
    bound("max_failures", report["failures"], "max")
    bound("min_avg_bitrate_kbps", avg_kbps, "min")
    bound("max_avg_bitrate_kbps", avg_kbps, "max")
    if expect.get("completes", True) and exit_code != 0:
        problems.append(f"bitglide exited with {exit_code}")
    return problems


def run_one(binary: Path, media: Path, profile_path: Path, abr: str, extra: list[str], verbose: bool) -> dict:
    profile = json.loads(profile_path.read_text())
    origin, base = start_origin(media, profile_path, verbose)
    try:
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
            report_path = Path(tmp.name)
        cmd = [str(binary), "--abr", abr, "--report", str(report_path),
               "--log-level", "info" if verbose else "warn", *extra, base + "master.m3u8"]
        started = time.monotonic()
        proc = subprocess.run(cmd, capture_output=not verbose, text=True, timeout=600)
        report = json.loads(report_path.read_text()) if report_path.stat().st_size else {}
        report_path.unlink(missing_ok=True)
    finally:
        origin.terminate()
        origin.wait(timeout=5)

    if not report:
        problems = [f"no report written (exit {proc.returncode})"]
    else:
        problems = check(profile.get("expect", {}), report, proc.returncode)
    return {
        "profile": profile.get("name", profile_path.stem),
        "abr": abr,
        "report": report,
        "problems": problems,
        "elapsed": time.monotonic() - started,
        "stderr": "" if verbose else (proc.stderr or "")[-2000:],
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=str(ROOT / "build" / "bitglide"))
    ap.add_argument("--media", default=str(ROOT / "build" / "media"))
    ap.add_argument("--profiles", nargs="*", help="profile JSON files (default: all in harness/profiles)")
    ap.add_argument("--abr", nargs="*", default=["hybrid"], help="algorithms to compare")
    ap.add_argument("--json-out", help="write a combined results file")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("extra", nargs=argparse.REMAINDER, help="args after -- are passed to bitglide")
    args = ap.parse_args(argv)

    binary, media = Path(args.binary), Path(args.media)
    if not binary.exists():
        print(f"binary not found: {binary} (run scripts/build.sh)", file=sys.stderr)
        return 2
    if not (media / "master.m3u8").exists():
        print(f"generating test content in {media}")
        subprocess.run([sys.executable, str(HERE / "gen_content.py"), "--out", str(media)], check=True)

    profiles = [Path(p) for p in args.profiles] if args.profiles else sorted((HERE / "profiles").glob("*.json"))
    extra = [a for a in args.extra if a != "--"]

    results = []
    for profile in profiles:
        for abr in args.abr:
            print(f"--> {profile.stem} [{abr}] ...", flush=True)
            res = run_one(binary, media, profile, abr, extra, args.verbose)
            results.append(res)
            status = "PASS" if not res["problems"] else "FAIL"
            print(f"    {status} in {res['elapsed']:.1f}s")
            for p in res["problems"]:
                print(f"      - {p}")
            if res["problems"] and res["stderr"]:
                print("      stderr tail:\n" + "\n".join("        " + l for l in res["stderr"].splitlines()[-10:]))

    print()
    header = f"{'profile':<14}{'abr':<12}{'stalls':>7}{'stall_s':>9}{'switches':>9}{'avg_kbps':>10}{'qoe':>8}  result"
    print(header)
    print("-" * len(header))
    for r in results:
        rep = r["report"] or {}
        print(f"{r['profile']:<14}{r['abr']:<12}{rep.get('stall_count', '-'):>7}"
              f"{rep.get('stall_time_s', 0):>9.2f}{rep.get('switch_count', '-'):>9}"
              f"{rep.get('avg_bitrate_bps', 0) / 1000:>10.0f}{rep.get('qoe', 0):>8.2f}  "
              f"{'PASS' if not r['problems'] else 'FAIL'}")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(results, indent=2))
    return 0 if all(not r["problems"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
