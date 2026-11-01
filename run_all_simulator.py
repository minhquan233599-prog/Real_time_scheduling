import sys, os, json; sys.path.insert(0, "simulator")
from reference_sim import *; from plot_gantt import gantt
os.makedirs("artifacts", exist_ok=True)
for name, tasks in SETS.items():
    print(name, "U=%.4f" % float(utilization(tasks)), "EDF:", admission(tasks,"EDF"), "RM:", admission(tasks,"RM"))
for p in ("EDF","RM"):
    for n,h in (("SAFE",20),("COMPARE",21)):
        r = simulate(SETS[n], p, h); export_csv(r, f"artifacts/{n.lower()}_{p.lower()}_sim.csv")
        print(n,p,"miss=",sum(r["misses"].values()), " ".join(timeline(r)))
r = simulate(SETS["EDF_ONLY"], "RM", 21, bypass=True); export_csv(r, "artifacts/edf_only_rm_bypass_sim.csv")
print("EDF_ONLY RM bypass", r["status"], r["misses"], [e for e in r["events"] if e[1]=="MISS"][:3]); print(" ".join(timeline(r)))
e = simulate(SETS["EDF_ONLY"], "EDF", 21); export_csv(e, "artifacts/edf_only_edf_sim.csv"); print("EDF_ONLY EDF miss", e["misses"])
gantt([("EDF",simulate(SETS["SAFE"],"EDF",20)),("RM",simulate(SETS["SAFE"],"RM",20))],"TEST-SAFE (U=0,75): EDF và RM","artifacts/gantt_safe.png")
gantt([("EDF",simulate(SETS["COMPARE"],"EDF",21)),("RM",simulate(SETS["COMPARE"],"RM",21))],"TEST-COMPARE (U=0,869): khác nhau tại t=4","artifacts/gantt_compare.png")
gantt([("EDF",e),("RM (bypass)",r)],"TEST-EDF-ONLY (U=0,971): EDF đạt, RM bypass bị miss tại t=7","artifacts/gantt_edf_only.png")
