import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
COL = {"A":"#2a7fa8","B":"#e08a30","C":"#3fa38a"}
def gantt(results, title, path):
    fig, axes = plt.subplots(len(results), 1, figsize=(10, 1.4*len(results)+0.9), sharex=True)
    if len(results) == 1: axes = [axes]
    for ax, (label, res) in zip(axes, results):
        for s, e, job, d, alg, src in res["trace"]:
            if job == "IDLE": continue
            ax.barh(0, e-s, left=s, color=COL[job[0]], edgecolor="white", height=0.6)
            ax.text((s+e)/2, 0, job, ha="center", va="center", color="white", fontsize=7, fontweight="bold")
        for t, ev, jid, r, d, rem in res["events"]:
            if ev == "MISS":
                ax.axvline(t, color="red", ls="--", lw=1); ax.text(t, 0.45, "MISS "+jid, color="red", fontsize=7, ha="center")
        ax.set_yticks([]); ax.set_ylabel(label, rotation=0, labelpad=22, fontweight="bold")
        ax.set_ylim(-0.5, 0.75)
    n = len(results[0][1]["trace"])
    axes[-1].set_xticks(range(0, n+1)); axes[-1].tick_params(labelsize=7)
    axes[-1].set_xlabel("Thời gian mô phỏng (tick, mỗi khối là khoảng [t, t+1)) – source: SIMULATOR")
    fig.suptitle(title, fontsize=10, fontweight="bold"); fig.tight_layout(); fig.savefig(path, dpi=150); plt.close(fig)
