#!/usr/bin/env python3
"""Vẽ các tick CPU quan sát được; không nội suy thành thời gian CPU chính xác."""
import argparse
import html
from pathlib import Path
from check_rt_order import load_runs, check_pairs


def draw(run, path):
    # Tick là mẫu tại cuối khoảng, không chứng minh tiến trình chạy trọn khoảng đó.
    events = run["events"]
    origin = min(e["release"] for e in events if e["type"] == 1)
    tasks = {pid: name for name, pid in run["pids"].items()}
    rows = [e for e in events if e["type"] != 8 and e["job"] <= run["jobs"]]
    end = max(e["tick"] for e in rows) - origin + 1
    width, left, right = 1200, 95, 30
    scale = (width - left - right) / max(end, 1)
    colors = {"A": "#2563eb", "B": "#d97706", "C": "#059669"}
    y = {"A": 100, "B": 170, "C": 240}
    def x(t):
        return left + (t - origin) * scale
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="365" viewBox="0 0 {width} 365">',
           '<rect width="100%" height="100%" fill="white"/>',
           '<g font-family="sans-serif" font-size="13" fill="#172033">',
           f'<text x="25" y="28" font-size="20">XV6 {run["policy"].upper()} — {run["taskset"]} — sampled CPU timeline</text>',
           f'<text x="25" y="50">Input hash: {html.escape(run["input_hash"])}; origin tick: {origin}; unit: kernel tick</text>']
    for name in "ABC":
        out += [f'<text x="20" y="{y[name] + 17}">Task {name}</text>',
                f'<line x1="{left}" y1="{y[name] + 30}" x2="{width-right}" y2="{y[name]+30}" stroke="#cbd5e1"/>']
    step = max(1, (end + 11) // 12)
    for t in range(0, end + 1, step):
        xpos = left + t * scale
        out += [f'<line x1="{xpos:.2f}" y1="75" x2="{xpos:.2f}" y2="285" stroke="#e2e8f0"/>',
                f'<text x="{xpos:.2f}" y="307" text-anchor="middle">{t}</text>']
    for e in rows:
        name = tasks[e["pid"]]
        ypos = y[name]
        label = html.escape(f'{name}{e["job"]}: type={e["type"]}, t={e["tick"]-origin}, remaining={e["remaining"]}')
        if e["type"] == 3:
            xpos = x(e["tick"] - 1)
            out.append(f'<rect x="{xpos:.2f}" y="{ypos}" width="{scale:.2f}" height="28" fill="{colors[name]}"><title>{label}</title></rect>')
        elif e["type"] in (1, 5):
            color = "#111827" if e["type"] == 1 else "#dc2626"
            xpos = x(e["tick"])
            out.append(f'<line x1="{xpos:.2f}" y1="{ypos-7}" x2="{xpos:.2f}" y2="{ypos+33}" stroke="{color}" stroke-width="2"><title>{label}</title></line>')
    out += ['<text x="25" y="335">Colored cell: RT_EV_TICK sample; black: RELEASE; red: COMPLETE. Blank does not prove CPU idle.</text>',
            '<text x="25" y="355">Each cell [t-1,t) represents a sample at t, not exact execution duration. Job after final completion is excluded.</text>',
            '</g></svg>']
    path.write_text("\n".join(out), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    parser.add_argument("--out", default="docs/week5/figures")
    args = parser.parse_args()
    try:
        runs, text = load_runs(args.log)
        check_pairs(runs, text)
        folder = Path(args.out)
        folder.mkdir(parents=True, exist_ok=True)
        for i, run in enumerate(runs):
            target = folder / f'pair-{i//2+1}-{run["policy"]}.svg'
            draw(run, target)
            print(f"GANTT: {target}")
    except (OSError, ValueError, KeyError) as exc:
        print(f"GANTT: FAIL ({exc})")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())