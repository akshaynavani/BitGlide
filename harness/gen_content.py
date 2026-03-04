#!/usr/bin/env python3
"""Generate a synthetic multi-bitrate HLS VOD ladder for exercising BitGlide.

Segments are filler bytes sized to match each rendition's bitrate, which is all the
controller cares about (it never decodes media). The layout is:

    <out>/master.m3u8
    <out>/v<i>/index.m3u8
    <out>/v<i>/seg<n>.ts
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

DEFAULT_LADDER = [
    # (bandwidth bps, width, height)
    (400_000, 416, 234),
    (1_200_000, 640, 360),
    (2_500_000, 1280, 720),
    (5_000_000, 1920, 1080),
]

TS_PACKET = 188


def segment_bytes(bandwidth_bps: int, duration_s: float) -> int:
    raw = int(bandwidth_bps * duration_s / 8)
    return max(TS_PACKET, raw - raw % TS_PACKET)


def write_segment(path: Path, size: int, seq: int) -> None:
    # MPEG-TS-ish packets (sync byte 0x47) so the files look plausible in a hex dump.
    packet = bytearray(TS_PACKET)
    packet[0] = 0x47
    packet[1:4] = seq.to_bytes(3, "big")
    with path.open("wb") as fh:
        for _ in range(size // TS_PACKET):
            fh.write(packet)


def generate(out: Path, segments: int, duration: float, ladder) -> None:
    out.mkdir(parents=True, exist_ok=True)
    master = ["#EXTM3U", "#EXT-X-VERSION:3"]
    for i, (bw, w, h) in enumerate(ladder):
        vdir = out / f"v{i}"
        vdir.mkdir(exist_ok=True)
        media = [
            "#EXTM3U",
            "#EXT-X-VERSION:3",
            f"#EXT-X-TARGETDURATION:{int(round(duration + 0.499))}",
            "#EXT-X-MEDIA-SEQUENCE:0",
            "#EXT-X-PLAYLIST-TYPE:VOD",
        ]
        size = segment_bytes(bw, duration)
        for n in range(segments):
            write_segment(vdir / f"seg{n}.ts", size, n)
            media += [f"#EXTINF:{duration:.3f},", f"seg{n}.ts"]
        media.append("#EXT-X-ENDLIST")
        (vdir / "index.m3u8").write_text("\n".join(media) + "\n")
        master += [
            f'#EXT-X-STREAM-INF:BANDWIDTH={bw},RESOLUTION={w}x{h},CODECS="avc1.4d401f,mp4a.40.2"',
            f"v{i}/index.m3u8",
        ]
    (out / "master.m3u8").write_text("\n".join(master) + "\n")


def parse_ladder(text: str):
    ladder = []
    for i, kbps in enumerate(text.split(",")):
        base = DEFAULT_LADDER[min(i, len(DEFAULT_LADDER) - 1)]
        ladder.append((int(float(kbps) * 1000), base[1], base[2]))
    return ladder


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="media", help="output directory (default: media)")
    ap.add_argument("--segments", type=int, default=30)
    ap.add_argument("--duration", type=float, default=2.0, help="segment duration in seconds")
    ap.add_argument("--ladder-kbps", help="comma separated bitrates, e.g. 400,1200,2500,5000")
    args = ap.parse_args(argv)

    ladder = parse_ladder(args.ladder_kbps) if args.ladder_kbps else DEFAULT_LADDER
    out = Path(args.out)
    generate(out, args.segments, args.duration, ladder)
    total = sum(f.stat().st_size for f in out.rglob("*.ts"))
    print(f"wrote {len(ladder)} renditions x {args.segments} segments to {os.fspath(out)} ({total / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
