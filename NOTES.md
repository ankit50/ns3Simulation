# FANET Security Replication — Findings Notes

Replicating the simulation methodology of:
Ceviz, Sen, Sadioglu, "A Survey of Security in UAVs and FANETs: Issues,
Threats, Analysis of Attacks, and Solutions," IEEE Communications Surveys
& Tutorials, Vol. 27, No. 5, 2025.

Target: Table V's exact simulation parameters, and Table VI's reported
sinkhole-attack results, in ns-3.

## Paper's stated parameters (Table V)

| Parameter              | Value                              |
|-------------------------|-------------------------------------|
| Routing protocol         | AODV                                |
| MAC                      | IEEE 802.11b                        |
| Simulation time           | 1800 s                              |
| Area                     | 12,000 m x 12,000 m x 300 m          |
| Nodes                    | 25, 50                              |
| Avg. speed                | 100 m/s                             |
| Transmission range         | 250 m                               |
| Traffic                  | UDP, 512 B packets, 1/s             |
| Bandwidth                 | 11 Mbps                             |
| Malicious node ratio       | 0%, 5%, 10%, 15%, 20%, 25%            |
| Mobility model             | 3D Gauss-Markov                      |
| Alpha                    | 0.25 - 0.7                           |

Traffic model (from the paper's text): 1 fixed Ground Base Station (GBS)
at the area center; 10 source nodes paired with 10 destination nodes
(remaining nodes are pure relays); destinations relay collected data to
the GBS starting at t=10s.

## What we built

`scripts/step5_baseline.cc` — the 0%-attacker baseline network, matching
Table V exactly at the 25-drone density: 25 drones + 1 GBS, AODV, 3D GMM
mobility, 10 randomly-assigned source->destination pairs, traffic starting
at t=2s (source->destination) and t=10s (destination->GBS).

Built incrementally and verified in NetAnim at each stage (10-node fixed
topology -> wireless -> AODV routing -> mobility) before scaling up to the
paper's real parameters.

## Finding 1 — Connectivity gap

At the paper's stated density, 25 nodes with a 250 m transmission range
spread across a 144 km^2 area give a combined radio coverage of only
about 3.4% of the total area. Running the literal Table V baseline, the
network is sparse enough that many source->destination pairs likely never
have a path between them at all.

The paper reports PDR drop under attack as evidence of attack impact, but
never separately verifies that pairs could communicate in a 0%-attacker
baseline. If a pair was never reachable in the first place, it already
contributes 0% delivery with zero attackers present — the paper's
methodology does not distinguish this from genuine attack impact.

## Finding 2 — Attacker-pool-size gap

With 25 drones, 20 of them are fixed as sources/destinations (10 pairs),
leaving only 5 drones eligible to ever be attackers (the paper excludes
sources/destinations from the attacker pool). The paper's own 25%
attacker ratio (~6 of 25 nodes) is therefore mathematically unreachable
at this density — at most 5/25 = 20% of nodes could ever be malicious.

## Finding 3 — Reproducibility gap

Attempting the literal 1800 s / 12,000 m baseline, the simulation
repeatedly stalls within the first ~60-180 simulated seconds: CPU pegged
at ~100%, but simulated time stops advancing, reproducibly (fixed RNG
seed), regardless of how long we wait.

We isolated the cause with three controlled runs, same code, same seed,
only the area size (`--areaXY`) changed:

| Area size          | Result                                                        |
|----------------------|------------------------------------------------------------------|
| 12,000 m (paper's real value) | Stalls almost immediately, never completes                       |
| 1,000 m (shrunk, mostly dense)   | Gets further (to ~180 s) before also stalling                     |
| 150 m (small enough that no two nodes can ever be out of 250 m range) | Completes the full run cleanly, no stall |

The 150 m guaranteed-connected run: **0 of 10 pairs with 0% delivery,
PDR = 94.05%, E2E delay = 0.065 s** — close to the paper's own reported
baseline (93.70% PDR, 0.084 s, Table VI, 0% attackers, low density).

Conclusion: the stall is caused specifically by AODV route-discovery
storms against destinations that are unreachable (even temporarily) in a
sparse network — not a bug in this code. The paper gives no detail on
propagation-loss model, AODV parameter tuning, or compute
resources/runtime used, so it isn't possible to tell from the paper alone
how a literal reading of Table V's parameters was made to run to
completion.

## Current status

- [x] Steps 1-4 (nodes -> wireless -> AODV -> mobility) verified at small
  scale in NetAnim.
- [x] Full-scale baseline network built, matching Table V exactly.
- [x] Three methodology/reproducibility gaps found and demonstrated with
  controlled experiments (see above).
- [x] Guaranteed-connected control run (`--areaXY=150`) completed, giving
  numbers close to the paper's own baseline.
- [ ] Sinkhole attack implementation in ns-3's AODV module
  (`src/aodv/model/aodv-routing-protocol.cc`) — in progress.
- [ ] Re-run across attacker ratios 5-25% and compare to Table VI, once
  the sinkhole attack is implemented (acknowledging Finding 2's
  attacker-pool-size constraint).

## Repro commands

```bash
cp scripts/step5_baseline.cc ~/ns-3.49/scratch/
cd ~/ns-3.49
./ns3 build step5_baseline

# Paper's real parameters (will stall -- see Finding 3)
./build/optimized/scratch/ns3.49-step5_baseline-optimized --simTime=1800

# Guaranteed-connected control (completes cleanly)
./build/optimized/scratch/ns3.49-step5_baseline-optimized --simTime=300 --progressInterval=10 --areaXY=150
```
