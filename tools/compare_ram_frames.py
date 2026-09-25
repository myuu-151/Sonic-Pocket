"""Compare two per-frame RAM dumps (0x4000-0x6FFF, 0x3000 bytes per frame).

Frames are paired both by index (with an optional offset) and by game tick
(GlobalTimer at 0x4000), so emulator timing drift can be told apart from game
state divergence. The stack area and bytes listed with --ignore are skipped.
"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

FRAME = 0x3000
BASE = 0x4000
STACK = range(0x6A00, 0x6C00)


def load(path: Path) -> list[bytes]:
    data = path.read_bytes()
    return [data[i : i + FRAME] for i in range(0, len(data) - FRAME + 1, FRAME)]


def diff(a: bytes, b: bytes, ignore: set[int]) -> list[int]:
    return [
        BASE + k
        for k in range(FRAME)
        if a[k] != b[k] and (BASE + k) not in ignore and (BASE + k) not in STACK
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ours", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--offset", type=int, default=0, help="reference frame = ours + offset")
    parser.add_argument("--ignore", default="0x6C58", help="comma-separated addresses to skip")
    args = parser.parse_args()

    ignore = {int(x, 0) for x in args.ignore.split(",") if x}
    ours = load(args.ours)
    ref = load(args.reference)

    exact = 0
    first = None
    for i, frame in enumerate(ours):
        j = i + args.offset
        if not 0 <= j < len(ref):
            continue
        d = diff(frame, ref[j], ignore)
        if d:
            first = first or (i, j, d)
        else:
            exact += 1
    print(f"by frame: {exact} identical frames; first mismatch: "
          f"{first and (first[0], first[1], [hex(x) for x in first[2][:16]])}")

    def by_tick(frames: list[bytes]) -> dict[int, tuple[int, bytes]]:
        out: dict[int, tuple[int, bytes]] = {}
        for i, f in enumerate(frames):
            out.setdefault(f[0] | f[1] << 8, (i, f))
        return out

    a, b = by_tick(ours), by_tick(ref)
    common = sorted(set(a) & set(b))
    counts: Counter[int] = Counter()
    bad = 0
    for tick in common:
        d = diff(a[tick][1], b[tick][1], ignore)
        bad += bool(d)
        counts.update(d)
    lag = [(t, a[t][0], b[t][0]) for t in common[:: max(1, len(common) // 8)]]
    print(f"by tick: {len(common)} common ticks, {bad} differ; (tick, our frame, ref frame): {lag}")
    print("most frequent differing bytes:", [(hex(k), n) for k, n in counts.most_common(20)])


if __name__ == "__main__":
    main()
