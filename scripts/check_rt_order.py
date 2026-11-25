#!/usr/bin/env python3
"""Kiểm tra trace workload SAFE/COMPARE và tình huống chọn khác nhau EDF/RM."""

import argparse
import re
from pathlib import Path

FIELDS = (
    "seq type cpu pid job tick release deadline next_release "
    "T C remaining kr aux misses"
).split()

RELEASE, DISPATCH, TICK, BUDGET0 = 1, 2, 3, 4
COMPLETE, MISS, REFILL, NORMAL = 5, 6, 7, 8
KERNEL, EDF, RM = 0, 1, 2


def require(condition, message):
    # Dừng kiểm tra và trả lỗi rõ ràng khi điều kiện không thỏa.
    if not condition:
        raise ValueError(message)


def has_line(text, line):
    return line in (item.strip() for item in text.splitlines())


def load_runs(path):
    # Giữ cấu trúc dữ liệu tương thích với plot_rt_gantt.py.
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    text = text.replace("\r", "")

    starts = list(re.finditer(
        r"SOURCE=XV6 policy=(edf|rm) set=(safe|compare) "
        r"jobs=(\d+)[^\n]*",
        text,
    ))
    require(starts, "không tìm thấy SOURCE=XV6")

    runs = []
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        part = text[match.start():end]

        hash_match = re.search(r"input_hash=([0-9a-fA-F]+)", match[0])
        require(hash_match, "thiếu input_hash")

        inputs = {}
        for name, c, t, blocks in re.findall(
            r"^INPUT ([ABC]): C=(\d+) T=(\d+) blocks=(\d+)\s*$",
            part, re.MULTILINE,
        ):
            require(name not in inputs, f"INPUT {name} bị lặp")
            inputs[name] = (int(c), int(t), int(blocks))

        pids = {}
        # Chấp nhận cả dòng TASK ngắn của Dũng v1 và dòng có thống kê cũ.
        for name, pid in re.findall(
            r"^TASK ([ABC]): pid=(\d+)\b", part, re.MULTILINE
        ):
            require(name not in pids, f"TASK {name} bị lặp")
            pids[name] = int(pid)

        require(set(inputs) == set(pids) == set("ABC"),
                "thiếu INPUT hoặc TASK A/B/C")
        require(len(set(pids.values())) == 3, "PID bị trùng")
        require(all(pid > 0 for pid in pids.values()), "PID không hợp lệ")

        for name, (c, t, blocks) in inputs.items():
            require(0 < c <= t and blocks > 0,
                    f"INPUT {name} không hợp lệ")

        rows = []
        for line in part.splitlines():
            if not line.startswith("CSV,"):
                continue
            if line.startswith("CSV,seq,"):
                require(line == "CSV," + ",".join(FIELDS),
                        "header CSV không đúng định dạng")
                continue

            values = line.split(",")[1:]
            require(len(values) == len(FIELDS), "CSV sai số cột")
            rows.append(dict(zip(FIELDS, map(int, values))))

        summaries = re.findall(
            r"^TRACE: events=(-?\d+) lost=(-?\d+)\s*$",
            part, re.MULTILINE,
        )
        require(len(summaries) == 1, "thiếu hoặc lặp TRACE summary")
        count, lost = map(int, summaries[0])
        require(count > 0 and count == len(rows), "CSV thiếu sự kiện")
        require(lost == 0, f"trace bị mất {lost} sự kiện")

        # Chỉ tin các kết quả thuộc trách nhiệm chạy/xuất dữ liệu của Dũng.
        require(has_line(part, "CHECK workers: PASS"),
                "workers chưa PASS")
        require(has_line(part, "CHECK trace export: PASS"),
                "xuất trace chưa PASS; cần log bản Dũng v1")
        require(has_line(part, "WORKLOAD RESULT: PASS"),
                "workload chưa PASS")

        runs.append({
            "policy": match[1],
            "taskset": match[2],
            "jobs": int(match[3]),
            "input_hash": hash_match[1].lower(),
            "inputs": inputs,
            "pids": pids,
            "events": rows,
            "text": part,
        })

    return runs, text


def validate_trace(run):
    # Theo dõi trạng thái riêng của từng tác vụ, không suy luận RUNNABLE.
    names = {pid: name for name, pid in run["pids"].items()}
    states = {}
    totals = {
        name: {"release": 0, "complete": 0, "tick": 0, "miss": 0, "refill": 0}
        for name in "ABC"
    }
    expected_class = EDF if run["policy"] == "edf" else RM
    previous_tick = -1

    for index, e in enumerate(run["events"]):
        tag = f'{run["policy"]} seq={index}'
        require(e["seq"] == index, f"{tag}: seq không liên tục")
        require(e["tick"] >= previous_tick, f"{tag}: tick đi lùi")
        previous_tick = e["tick"]
        require(RELEASE <= e["type"] <= NORMAL,
                f"{tag}: loại sự kiện không hợp lệ")

        # NORMAL có thể thuộc tiến trình cha hoặc tiến trình thường.
        if e["type"] == NORMAL:
            continue

        require(e["pid"] in names, f"{tag}: PID RT ngoài workload")
        name = names[e["pid"]]
        c, t, _ = run["inputs"][name]
        require(e["cpu"] == 0, f"{tag}: RT chạy ngoài CPU 0")
        require((e["C"], e["T"]) == (c, t), f"{tag}: sai C/T")
        require(0 <= e["remaining"] <= c, f"{tag}: budget ngoài khoảng")
        require(e["kr"] in (0, 1), f"{tag}: kernel_resume không hợp lệ")
        require(e["release"] <= e["tick"], f"{tag}: chạy trước release")
        require(e["deadline"] == e["release"] + t,
                f"{tag}: deadline khác release + T")

        kind = e["type"]
        s = states.get(name)

        if kind == RELEASE:
            require(e["remaining"] == c, f"{tag}: release không cấp đủ C")
            require(e["next_release"] == e["deadline"],
                    f"{tag}: next_release ban đầu sai")
            require(e["tick"] == e["release"],
                    f"{tag}: release không đúng tick")

            if s is None:
                require(e["job"] == 1 and e["misses"] == 0,
                        f"{tag}: job đầu hoặc bộ đếm miss sai")
            else:
                require(s["done"] and not s["need_zero"],
                        f"{tag}: tạo job mới khi job trước chưa xong")
                require(e["job"] == s["job"] + 1,
                        f"{tag}: job_id không tăng một")
                require(e["release"] == s["next_release"],
                        f"{tag}: sai mốc kỳ tiếp")
                require(e["misses"] == s["misses"],
                        f"{tag}: release làm thay đổi bộ đếm miss")

            states[name] = dict(
                e, done=False, missed=False, need_zero=False
            )
            totals[name]["release"] += 1
            continue

        require(s is not None, f"{tag}: sự kiện xuất hiện trước RELEASE")
        require(e["job"] == s["job"], f"{tag}: sai job_id")
        require(e["release"] == s["release"]
                and e["deadline"] == s["deadline"],
                f"{tag}: thay đổi release/deadline của job hiện tại")

        # Sau tick về 0, sự kiện tiếp theo của cùng tác vụ phải là BUDGET0.
        if s["need_zero"]:
            require(kind == BUDGET0, f"{tag}: thiếu BUDGET0")

        if kind != REFILL:
            require(e["next_release"] == s["next_release"],
                    f"{tag}: next_release thay đổi ngoài REFILL")

        if kind != MISS:
            require(e["misses"] == s["misses"],
                    f"{tag}: bộ đếm miss thay đổi ngoài MISS")

        if kind == TICK:
            require(not s["done"] and s["remaining"] > 0,
                    f"{tag}: trừ budget của job không đủ điều kiện")
            require(e["aux"] == s["remaining"]
                    and e["remaining"] == s["remaining"] - 1,
                    f"{tag}: budget không giảm đúng một")
            totals[name]["tick"] += 1
            s["need_zero"] = e["remaining"] == 0

        elif kind == BUDGET0:
            require(s["need_zero"] and e["remaining"] == 0,
                    f"{tag}: BUDGET0 không khớp TICK")
            require(e["tick"] == s["tick"], f"{tag}: BUDGET0 sai tick")
            s["need_zero"] = False

        elif kind == COMPLETE:
            require(not s["done"], f"{tag}: COMPLETE bị lặp")
            require(e["remaining"] == 0, f"{tag}: COMPLETE còn budget")
            # Nếu hoàn thành sau deadline, phải có MISS trước đó.
            require(e["tick"] <= e["deadline"] or s["missed"],
                    f"{tag}: hoàn thành muộn nhưng không có MISS")
            s["done"] = True
            totals[name]["complete"] += 1

        elif kind == MISS:
            require(not s["done"] and not s["missed"],
                    f"{tag}: MISS của job đã xong hoặc bị đếm lặp")
            require(e["tick"] >= e["deadline"], f"{tag}: báo MISS quá sớm")
            require(e["remaining"] == s["remaining"],
                    f"{tag}: MISS làm đổi budget")
            require(e["misses"] == s["misses"] + 1,
                    f"{tag}: bộ đếm MISS không tăng một")
            s["missed"] = True
            totals[name]["miss"] += 1

        elif kind == REFILL:
            require(not s["done"], f"{tag}: refill job đã hoàn thành")
            require(e["tick"] == s["next_release"],
                    f"{tag}: refill sai mốc chu kỳ")
            require(s["missed"], f"{tag}: carry-over thiếu MISS")
            require(e["remaining"] == c
                    and e["next_release"] == s["next_release"] + t,
                    f"{tag}: refill sai budget hoặc mốc tiếp theo")
            totals[name]["refill"] += 1

        elif kind == DISPATCH:
            require(e["remaining"] == s["remaining"],
                    f"{tag}: dispatch tự thay đổi budget")
            require(e["aux"] in (KERNEL, expected_class),
                    f"{tag}: sai lớp dispatch")
            if e["aux"] == expected_class:
                require(not s["done"] and e["remaining"] > 0
                        and e["kr"] == 0,
                        f"{tag}: dispatch workload không đủ điều kiện")

        s.update(e)

    for name in "ABC":
        require(name in states, f"thiếu tác vụ {name}")
        s = states[name]
        n = totals[name]
        require(not s["need_zero"], f"{name}: trace kết thúc thiếu BUDGET0")
        require(n["complete"] == run["jobs"],
                f"{name}: số COMPLETE khác số job yêu cầu")
        # job_done cuối chờ kỳ mới nên có thể xuất hiện thêm một RELEASE.
        require(n["release"] in (run["jobs"], run["jobs"] + 1),
                f"{name}: số RELEASE không hợp lệ")
        print(
            f'{run["policy"].upper()} TASK {name}: '
            f'release={n["release"]} complete={n["complete"]} '
            f'tick={n["tick"]} miss={n["miss"]} refill={n["refill"]}'
        )

    print(f'CHECK {run["policy"].upper()} trace lifecycle: PASS')
    misses = sum(item["miss"] for item in totals.values())
    require(misses == 0,
            f'{run["policy"]}: workload có {misses} deadline miss')
    print(f'CHECK {run["policy"].upper()} no deadline miss: PASS')


def witness(run):
    # Chỉ xét cửa sổ có đủ bằng chứng trong workload cố định của Dũng.
    # Không coi mọi job đã RELEASE là một tiến trình đang RUNNABLE.
    state = {}
    completed = set()
    c_started = False
    pending = False
    names = {pid: name for name, pid in run["pids"].items()}
    origin = min(
        e["release"] for e in run["events"] if e["type"] == RELEASE
    )

    for e in run["events"]:
        if e["type"] == NORMAL:
            continue
        name = names[e["pid"]]

        if e["type"] == RELEASE:
            state[name] = dict(e, done=False)
        else:
            done = state[name]["done"] or e["type"] == COMPLETE
            state[name] = dict(e, done=done)

        if e["type"] == COMPLETE:
            completed.add((name, e["job"]))

        # A1/B1 đã hoàn thành: gate đã mở, C1 có TICK trong workload.
        if (name == "C" and e["job"] == 1 and e["type"] == TICK
                and {("A", 1), ("B", 1)} <= completed):
            c_started = True

        if name == "B" and e["job"] == 2 and e["type"] == RELEASE:
            pending = c_started

        if not pending or e["type"] != DISPATCH or e["aux"] == KERNEL:
            continue

        pending = False
        a, b, c = (state.get(n) for n in "ABC")
        if not (
            a and b and c and a["done"]
            and b["job"] == 2 and c["job"] == 1
            and not b["done"] and not c["done"]
            and b["remaining"] > 0 and c["remaining"] > 0
        ):
            continue

        require(e["tick"] - b["release"] <= 1,
                "phản ứng sau release B2 chậm hơn một tick")

        edf = min((b, c), key=lambda x: (x["deadline"], x["pid"]))
        rm = min((b, c), key=lambda x: (x["T"], x["pid"]))
        require(edf["pid"] != rm["pid"],
                "cửa sổ không phân biệt được EDF/RM")

        expected = edf if run["policy"] == "edf" else rm
        require(e["pid"] == expected["pid"],
                f'{run["policy"]}: expected PID {expected["pid"]}, '
                f'actual PID {e["pid"]}')

        result = (name, e["job"])
        print(
            f'{run["policy"].upper()} ORDER: '
            f't={e["tick"] - origin} chọn {name}{e["job"]}; '
            f'dB={b["deadline"] - origin} '
            f'dC={c["deadline"] - origin} [PASS]'
        )
        return result

    raise ValueError(
        f'{run["policy"]}: thiếu cửa sổ B2/C1 đủ điều kiện; '
        "chưa chứng minh lựa chọn khác nhau"
    )


def check_pairs(runs, text):
    require(len(runs) > 0 and len(runs) % 2 == 0,
            "thiếu một nửa cặp EDF/RM")
    require("WORKLOAD: calibrated fixed blocks" in text,
            "cần log workload hiệu chuẩn của Dũng")

    checked = 0
    for i in range(0, len(runs), 2):
        edf, rm = runs[i:i + 2]
        require((edf["policy"], rm["policy"]) == ("edf", "rm"),
                "thứ tự cặp phải là EDF rồi RM")
        require(edf["taskset"] == rm["taskset"],
                "hai policy dùng khác bộ tác vụ")
        require(edf["jobs"] == rm["jobs"] == 3,
                "số job không đúng hợp đồng workload")
        require(edf["inputs"] == rm["inputs"]
                and edf["input_hash"] == rm["input_hash"],
                "đầu vào EDF/RM không giống nhau")
        require(has_line(rm["text"], "CHECK restore policy: PASS"),
                "chưa khôi phục policy thành công")
        require(has_line(rm["text"], "PAIRED WORKLOAD RESULT: PASS"),
                "cặp workload chưa PASS")

        print(f'\nPAIR {i // 2 + 1}: {edf["taskset"].upper()}')
        validate_trace(edf)
        validate_trace(rm)

        if edf["taskset"] == "safe":
            print("SAFE TRACE: PASS")
            continue

        edf_choice = witness(edf)
        rm_choice = witness(rm)
        require(edf_choice != rm_choice,
                "chưa quan sát được hai lựa chọn khác nhau")
        checked += 1

    require(checked > 0, "cần ít nhất một cặp COMPARE để kiểm tra thứ tự")
    print(f"\nORDER COMPARISON: {checked} compare pair(s) PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="Log chứa cặp EDF/RM của rt_compare")
    args = parser.parse_args()

    try:
        runs, text = load_runs(args.log)
        check_pairs(runs, text)
    except (OSError, ValueError, KeyError) as error:
        print(f"\nORDER COMPARISON: FAIL ({error})")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())