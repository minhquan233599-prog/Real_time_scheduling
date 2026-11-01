"""Reference simulator (oracle) cho EDF / RM. 1 CPU, preemptive, D=T, O=0, tick nguyen.
Thu tu su kien moi tick t: (1) phat hien miss (deadline<=t), (2) release job, (3) chon job, (4) chay 1 tick."""
import math, csv
from fractions import Fraction

SETS = {
    "SAFE":     [("A",1,4),("B",2,5),("C",1,10)],
    "COMPARE":  [("A",1,3),("B",1,4),("C",2,7)],
    "EDF_ONLY": [("A",2,5),("B",4,7)],
    "OVERLOAD": [("A",3,5),("B",3,5)],
}

def utilization(tasks):
    return sum(Fraction(c, t) for _, c, t in tasks)

def valid(tasks):
    return all(c > 0 and t > 0 and c <= t for _, c, t in tasks)

def rta(tasks):
    """Tra ve (ok, [R_i]) theo thu tu RM (T tang dan, tie-break theo id)."""
    ts = sorted(tasks, key=lambda x: (x[2], x[0]))
    res = []
    for i, (_, c, t) in enumerate(ts):
        r = c
        while True:
            nxt = c + sum(math.ceil(r / th) * ch for _, ch, th in ts[:i])
            if nxt > t:
                return False, res + [nxt]
            if nxt == r:
                break
            r = nxt
        res.append(r)
    return True, res

def admission(tasks, policy):
    if not valid(tasks): return "INVALID", None
    u = utilization(tasks)
    if u > 1: return "REJECT_OVERLOAD", None
    if policy == "EDF": return "ACCEPT", None
    ok, r = rta(tasks)
    return ("ACCEPT" if ok else "REJECT_RTA"), r

def pick(jobs, policy):
    cand = [j for j in jobs if not j["done"] and not j["dropped"] and j["rem"] > 0]
    if not cand: return None
    if policy == "EDF":
        return min(cand, key=lambda j: (j["d"], j["r"], j["task"]))
    return min(cand, key=lambda j: (j["T"], j["r"], j["task"]))

def simulate(tasks, policy, horizon, bypass=False):
    status, extra = admission(tasks, policy)
    if status != "ACCEPT" and not bypass:
        return {"status": status, "R": extra, "trace": [], "events": [], "misses": {}, "source": "SIMULATOR"}
    jobs, trace, events = [], [], []
    misses = {n: 0 for n, _, _ in tasks}
    def check_miss(t):
        for j in jobs:
            if not j["done"] and not j["dropped"] and not j["miss"] and t >= j["d"]:
                j["miss"] = True; j["dropped"] = True   # DROP-LATE
                misses[j["task"]] += 1
                events.append((t, "MISS", j["id"], j["r"], j["d"], j["rem"]))
    for t in range(horizon):
        check_miss(t)
        for n, c, T in tasks:
            if t % T == 0:
                k = t // T + 1
                jobs.append(dict(task=n, id=f"{n}{k}", r=t, d=t+T, rem=c, T=T, done=False, dropped=False, miss=False))
                events.append((t, "RELEASE", f"{n}{k}", t, t+T, c))
        j = pick(jobs, policy)
        assert j is None or j["r"] <= t                      # eligibility
        trace.append((t, t+1, j["id"] if j else "IDLE", j["d"] if j else "", policy, "SIMULATOR"))
        if j:
            j["rem"] -= 1
            if j["rem"] == 0:
                j["done"] = True
                events.append((t+1, "COMPLETE", j["id"], j["r"], j["d"], 0))
    check_miss(horizon)
    return {"status": "ACCEPT" if status == "ACCEPT" else "BYPASS:" + status, "R": extra,
            "trace": trace, "events": events, "misses": misses, "source": "SIMULATOR"}

def timeline(res):               # tick -> task name
    return [("IDLE" if x[2] == "IDLE" else x[2][0]) for x in res["trace"]]

def export_csv(res, path):
    with open(path, "w", newline="") as f:
        w = csv.writer(f); w.writerow(["start","end","job","abs_deadline","algorithm","source"])
        w.writerows(res["trace"])
