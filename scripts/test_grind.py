#!/usr/bin/env python3

import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "docs/week4/logs"

DURATION = 60       # Thời gian theo dõi grind, tính bằng giây.
STALL_LIMIT = 30    # Giới hạn không có tiến độ A/B.
BOOT_LIMIT = 60     # Thời gian tối đa chờ shell.

ERROR = re.compile(
    r"panic|grind:|exec grind failed|"
    r"failed to get.*lock|make: \*\*\*|: error:",
    re.IGNORECASE,
)


def stop_qemu(process):
    if process.poll() is not None:
        return

    try:
        # Ctrl+A rồi X: yêu cầu QEMU thoát.
        process.stdin.write(b"\x01x")
        process.stdin.flush()
        process.wait(timeout=5)
        return
    except (OSError, subprocess.TimeoutExpired):
        pass

    # Dọn cả make và QEMU nếu chưa thoát.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass

    process.wait()


def test_grind(cpus):
    path = LOGS / f"grind-cpus{cpus}.log"
    print(f"Đang kiểm tra grind CPUS={cpus}...", flush=True)

    process = subprocess.Popen(
        ["make", "qemu", f"CPUS={cpus}"],
        cwd=ROOT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
        bufsize=0,
    )

    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    boot_time = time.monotonic()
    started = None
    last_progress = None
    scan = ""
    count_a = 0
    count_b = 0
    passed = False
    reason = ""

    with path.open("wb") as log:
        try:
            while not reason:
                for key, _ in selector.select(timeout=0.5):
                    data = os.read(key.fileobj.fileno(), 65536)

                    if not data:
                        reason = "Output QEMU đã đóng."
                        break

                    log.write(data)
                    log.flush()

                    text = data.decode("utf-8", errors="replace")
                    scan = (scan + text)[-8192:]

                    match = ERROR.search(scan)
                    if match:
                        reason = f"Phát hiện thông báo lỗi: {match.group(0)}"
                        break

                    if started is None:
                        # Chỉ gửi lệnh khi shell xv6 đã sẵn sàng.
                        if re.search(r"(?:^|\n)\$ ", scan):
                            process.stdin.write(b"grind\n")
                            process.stdin.flush()
                            started = time.monotonic()
                            last_progress = started
                            scan = ""
                    else:
                        a = text.count("A")
                        b = text.count("B")
                        count_a += a
                        count_b += b

                        if a or b:
                            last_progress = time.monotonic()

                        if re.search(r"(?:^|\n)\$ ", scan):
                            reason = "Grind trở về shell ngoài dự kiến."
                            break

                if reason:
                    break

                if process.poll() is not None:
                    reason = "QEMU kết thúc sớm."
                    break

                now = time.monotonic()

                if started is None:
                    if now - boot_time > BOOT_LIMIT:
                        reason = "Hết thời gian chờ shell xv6."
                    continue

                if now - last_progress > STALL_LIMIT:
                    reason = "Không thấy tiến độ A/B trong 30 giây."
                elif now - started >= DURATION:
                    passed = count_a > 0 and count_b > 0
                    reason = (
                        "Đủ 60 giây, có A/B và không phát hiện lỗi."
                        if passed else
                        "Chưa thấy tiến độ của cả hai worker A/B."
                    )

        finally:
            stop_qemu(process)
            selector.close()
            process.stdout.close()
            process.stdin.close()

        result = (
            f"\nHARNESS RESULT — GRIND CPUS={cpus}: "
            f"{'PASS' if passed else 'FAIL'}\n"
            f"A={count_a}, B={count_b}\n"
            f"{reason}\n"
        )
        log.write(result.encode("utf-8"))

    print(result.strip(), flush=True)
    return passed


def main():
    LOGS.mkdir(parents=True, exist_ok=True)

    # Chạy tuần tự để không tranh quyền truy cập fs.img.
    cpu1_ok = test_grind(1)
    cpu4_ok = test_grind(4)

    passed = cpu1_ok and cpu4_ok
    print("GRIND SUITE:", "PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())