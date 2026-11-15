#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import struct
import sys
from datetime import datetime


ROOT = Path(__file__).resolve().parents[1]
LOG_DIR = ROOT / "docs/week4/logs"

# Cấu trúc ảnh đĩa của bản xv6 nhóm đang dùng.
BLOCK_SIZE = 1024
DINODE_SIZE = 64
INODES_PER_BLOCK = BLOCK_SIZE // DINODE_SIZE
FSMAGIC = 0x10203040


def count_free_inodes(image):
    data = image.read_bytes()

    # Superblock gồm 8 số nguyên 32 bit, nằm tại block 1.
    sb = struct.unpack_from("<8I", data, BLOCK_SIZE)

    if sb[0] != FSMAGIC:
        raise ValueError("fs.img không đúng định dạng xv6 đang dùng")

    ninodes = sb[3]
    inodestart = sb[6]

    # Kiểm tra vùng inode không vượt ra ngoài ảnh đĩa.
    if ninodes < 2:
        raise ValueError("Số inode không hợp lệ")

    end = (
        inodestart * BLOCK_SIZE
        + ((ninodes + INODES_PER_BLOCK - 1) // INODES_PER_BLOCK)
        * BLOCK_SIZE
    )

    if end > len(data):
        raise ValueError("Ảnh đĩa bị thiếu dữ liệu vùng inode")

    free = 0

    # Inode 0 được dành riêng, không tính là inode khả dụng.
    for i in range(1, ninodes):
        block = inodestart + i // INODES_PER_BLOCK
        offset = block * BLOCK_SIZE + (i % INODES_PER_BLOCK) * DINODE_SIZE

        inode_type = struct.unpack_from("<H", data, offset)[0]

        if inode_type == 0:
            free += 1

    return free, list(sb)


def main():
    parser = argparse.ArgumentParser(
        description="Đo và so sánh inode trống trước/sau bài test xv6"
    )
    parser.add_argument("--cpus", type=int, choices=(1, 4), required=True)
    parser.add_argument("--phase", choices=("before", "after"), required=True)
    args = parser.parse_args()

    LOG_DIR.mkdir(parents=True, exist_ok=True)

    free, superblock = count_free_inodes(ROOT / "fs.img")

    snapshot = {
        "time": datetime.now().astimezone().isoformat(),
        "cpus": args.cpus,
        "phase": args.phase,
        "image": "fs.img",
        "free_inodes": free,
        "superblock": superblock,
    }

    # Khi đo sau, phải có kết quả trước tương ứng.
    before = None

    if args.phase == "after":
        before_path = LOG_DIR / f"inodes-cpus{args.cpus}-before.json"
        before = json.loads(before_path.read_text())

        if before["superblock"] != superblock:
            raise ValueError("Cấu trúc ảnh đĩa trước/sau không khớp")

    path = LOG_DIR / f"inodes-cpus{args.cpus}-{args.phase}.json"
    path.write_text(
        json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n"
    )

    print(f"CPUS={args.cpus} {args.phase}: FREE INODES={free}")
    print(f"Đã lưu: {path.relative_to(ROOT)}")

    if before is not None:
        old = before["free_inodes"]

        # Bộ rt_w4test không tạo file: kỳ vọng số inode không đổi.
        ok = free == old
        result = (
            f"INODES CPUS={args.cpus}: before={old} after={free} "
            f"[{'PASS' if ok else 'FAIL'}]"
        )

        summary = LOG_DIR / f"inodes-cpus{args.cpus}-result.txt"
        summary.write_text(result + "\n")
        print(result)

        return 0 if ok else 1

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f"INODE CHECK: ERROR — {error}", file=sys.stderr)
        sys.exit(1)