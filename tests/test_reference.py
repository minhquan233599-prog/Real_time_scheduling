import sys, os; sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "simulator"))
from fractions import Fraction
from reference_sim import (
    SETS,
    utilization,
    admission,
    simulate,
    timeline,
    rta
)

def test_utilization():
    assert utilization(SETS["SAFE"]) == Fraction(3,4)
    assert abs(float(utilization(SETS["COMPARE"])) - 0.8690476) < 1e-6
    assert utilization(SETS["EDF_ONLY"]) == Fraction(34,35)
    assert utilization(SETS["OVERLOAD"]) == Fraction(6,5)

def test_rta_values():
    assert rta(SETS["SAFE"]) == (True, [1,3,4])
    assert rta(SETS["COMPARE"]) == (True, [1,2,6])
    assert rta(SETS["EDF_ONLY"])[0] is False and rta(SETS["EDF_ONLY"])[1][-1] == 8

def test_admission():
    assert admission(SETS["SAFE"], "EDF")[0] == "ACCEPT" and admission(SETS["SAFE"], "RM")[0] == "ACCEPT"
    assert admission(SETS["COMPARE"], "RM")[0] == "ACCEPT"
    assert admission(SETS["EDF_ONLY"], "EDF")[0] == "ACCEPT"
    assert admission(SETS["EDF_ONLY"], "RM")[0] == "REJECT_RTA"
    assert admission(SETS["OVERLOAD"], "EDF")[0] == "REJECT_OVERLOAD"
    assert admission(SETS["OVERLOAD"], "RM")[0] == "REJECT_OVERLOAD"

def test_invalid():
    for bad in ([("A",0,5)], [("A",1,0)], [("A",6,5)], [("A",-1,5)]):
        assert admission(bad, "EDF")[0] == "INVALID"

def test_compare_t4():
    e = timeline(simulate(SETS["COMPARE"], "EDF", 21)); r = timeline(simulate(SETS["COMPARE"], "RM", 21))
    assert e[4] == "C" and r[4] == "B"
    assert e != r

def test_compare_no_miss():
    for p in ("EDF","RM"):
        assert sum(simulate(SETS["COMPARE"], p, 84)["misses"].values()) == 0

def test_safe_no_miss_long():
    for p in ("EDF","RM"):
        assert sum(simulate(SETS["SAFE"], p, 200)["misses"].values()) == 0

def test_edf_only_edf_no_miss():
    assert sum(simulate(SETS["EDF_ONLY"], "EDF", 105)["misses"].values()) == 0

def test_rm_bypass_miss_at_7():
    res = simulate(SETS["EDF_ONLY"], "RM", 12, bypass=True)
    miss = [e for e in res["events"] if e[1] == "MISS"]
    assert miss[0][0] == 7 and miss[0][2] == "B1" and res["misses"]["B"] >= 1
    assert simulate(SETS["EDF_ONLY"], "RM", 12, bypass=False)["trace"] == []

def test_miss_counted_once_per_job():
    r = simulate(SETS["EDF_ONLY"], "RM", 40, bypass=True)
    ids = [e[2] for e in r["events"] if e[1] == "MISS"]
    assert len(ids) == len(set(ids)) and ids.count("B1") == 1

def test_finish_at_deadline_is_ontime():
    res = simulate([("A",2,5),("B",3,5)], "EDF", 10)
    assert sum(res["misses"].values()) == 0
    assert any(e[1]=="COMPLETE" and e[0]==5 for e in res["events"])

def test_determinism_and_one_job_per_tick():
    a = simulate(SETS["COMPARE"], "EDF", 21)["trace"]; b = simulate(SETS["COMPARE"], "EDF", 21)["trace"]
    assert a == b and len(a) == 21

def test_same_deadline_tiebreak():
    assert timeline(simulate([("A",1,4),("B",1,4)], "EDF", 4))[:2] == ["A","B"]

def test_overload_no_trace():
    assert simulate(SETS["OVERLOAD"], "EDF", 10)["trace"] == []

def test_simulate_invalid_tasks():
    res = simulate([("A", 6, 5)], "EDF", 10)

    assert res["status"] == "INVALID"
    assert res["trace"] == []
    assert res["events"] == []
    assert res["misses"] == {}


def test_idle_timeline():
    res = simulate([("A", 1, 5)], "EDF", 4)

    assert timeline(res) == ["A", "IDLE", "IDLE", "IDLE"]
