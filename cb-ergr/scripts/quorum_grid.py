"""Quorum dead-head detection: detection latency, false declarations and PDR for several guard / report-repeat / quorum
settings against the table-expiry backstop (N = 100, 10 head kills at 1500 s, held-out training seeds 106-110).
Usage: quorum_grid.py BINARY [OUTROOT]. Needs the scenario binary of a build with QuorumDetection.
"""
import subprocess, csv, sys, os
from concurrent.futures import ThreadPoolExecutor
B=sys.argv[1]
ROOT=sys.argv[2] if len(sys.argv)>2 else 'results/quorum-grid'
os.makedirs(ROOT,exist_ok=True)
variants={
 'backstop': ['--QuorumDetection=false'],
 'q g10 rep10': ['--QuorumDetection=true','--DeadGuardS=10','--DeadReportRepeatS=10'],
 'q g25 rep10': ['--QuorumDetection=true','--DeadGuardS=25','--DeadReportRepeatS=10'],
 'q g50 rep10': ['--QuorumDetection=true','--DeadGuardS=50','--DeadReportRepeatS=10'],
 'q g50 rep50': ['--QuorumDetection=true','--DeadGuardS=50'],
 'q g60 rep10 Q3': ['--QuorumDetection=true','--DeadGuardS=60','--DeadReportRepeatS=10','--DeadQuorum=3'],
}
seeds=[106,107,108,109,110]
def one(job):
    name,seed=job
    out=f"{ROOT}/{name.replace(' ','_')}_{seed}"
    cmd=[B,'--Selector=RoundRobin','--SelectorAttributes=WithFeedback=true',f'--Seed={seed}','--N=100','--SimTimeS=3000','--WarmupS=500','--PktRatePerNodeHz=0.004','--InitialEnergyJ=10000','--KillHeadsAtS=1500','--KillHeadsCells=10',f'--OutDir={out}','--LogLinks=false','--LogPackets=false','--LogNodes=false']+variants[name]
    subprocess.run(cmd,capture_output=True)
    return name,next(csv.DictReader(open(out+'/summary.csv')))
with ThreadPoolExecutor(4) as p:
    res=list(p.map(one,[(n,s) for n in variants for s in seeds]))
print('%-16s %8s %8s %8s %9s %8s'%('variant','declared','false','detected','lat_s','pdr'))
for n in variants:
    rows=[r for m,r in res if m==n]
    f=lambda k: sum(float(r[k]) for r in rows)/len(rows)
    print('%-16s %8.0f %8.0f %8.1f %9.1f %8.3f'%(n,f('dead_declarations'),f('dead_false_declarations'),f('dead_heads_detected'),f('dead_first_latency_mean_s'),f('pdr')))
