# CodinGame Fall Challenge 2026: TryAngle Catch
## Top 30 Gold League Push Report & Engine Architecture

### 1. Executive Summary & Current Standing

| Metric | Status |
| :--- | :--- |
| **Active Submission** | `41646650` (`v3.7` / [`bot_v3_7_sub.cpp`](file:///home/kali/Desktop/CodingameFallChallenge2026/bot_v3_7_sub.cpp)) |
| **Gold League Rank** | **`#86 / 423`** (`#86 / 862` overall) |
| **Contest Rating (TrueSkill)** | **`23.15`** |
| **Top 30 Cutoff** | **`28.29`** (`#30 Rikus`, score 28.29; `#23 laconic_pixel`, score 29.26) |
| **Version History** | `v1` $\to$ `v2.9` $\to$ `v3.3` (104) $\to$ `v3.4` (85) $\to$ `v3.5` (77) $\to$ `v3.6` (67 in-progress) $\to$ `v3.7` (84) |
| **Git Repository** | Up to date & pushed to [`Ra7ch/CodingameFallChallenge2026`](https://github.com/Ra7ch/CodingameFallChallenge2026) (commit `249ebe8`) |
| **Limits Status** | Submits: 5/5 available (5m); Test games: 10/10 available (1m); Requests: 118/120 available |

---

### 2. Deep Referee Mechanics & The 2-Stroke Factory Cycle

Analysis of the official referee source code (`referee/src/main/java/engine/`) uncovered several vital rules governing high-level play:

#### A. Priority Execution Order & Task Physics
```
Priority 0: ATTACK & SURROUND
Priority 1: MOVE
Priority 2: SPAWN
Priority 3: ADD_PATH (Gold League)
Priority 4: REMOVE_PATH (Gold League)
```
1. **`SPAWN` happens AFTER `MOVE`**:
   - `SpawnTask.java` executes at Priority 2.
   - Newly spawned units cannot move on the turn they appear; they remain on the spawned node until next turn.
   - `SPAWN` consumes **0 units** from triangle corners (`triangle.use(0)`).
2. **The Exact Triangle Reset Condition**:
   - In `Triangle.java`, `use()` sets `owner = null` and `canCapture[player] = false`.
   - In `Board.finalizeTurn()`, `updateAllowedCaptures()` executes:
     ```java
     canCapture[i] |= node1.units[i] == 0 && node2.units[i] == 0 && node3.units[i] == 0;
     ```
   - **Crucial Rule**: A used triangle can ONLY reset `meCan = true` if **all three corners simultaneously have zero friendly units**.
   - Because the newly spawned unit sits on one corner at Priority 2, a triangle **cannot reset on the turn it spawns**.
   - It can only reset on Turn $T+1$ if all friendly units completely vacate all 3 corners.

#### B. The 2-Stroke Factory Alternator
Top 30 players (`H1ggsb0s0n`, `DomiKo`, `laconic_pixel`) maintain a continuous alternating cycle across two disjoint triangle sets (Set A and Set B):

```mermaid
flowchart TD
    subgraph Turn T
        A1["Set A: SPAWN (5-8 units)"] --> A2["Set A corners vacated"]
        B1["Set B: Captured by moving army"]
    end
    subgraph Turn T+1
        B2["Set B: SPAWN (5-8 units)"] --> B3["Set B corners vacated"]
        A3["Set A: meCan resets to 1 -> Recaptured"]
    end
    Turn T --> Turn T+1
    Turn T+1 --> Turn T
```

---

### 3. Key Enhancements in `v3.7`

#### 1. Search Time Budget Expansion (40ms)
- Gold League turn timeout is **50ms** (Turn 2+).
- Previously, the bot capped search at `23.0ms` (Turn 2+) and `18.0ms` (Opening).
- In `v3.7`:
  - `maxTimeMs = (curTurn == 1 ? 350.0 : 40.0)`
  - `openLimit = (curTurn == 1 ? 280.0 : 32.0)`
- This increases simulated annealing iterations from ~7,000 to ~15,000+ per turn.
- Measured server execution time stays safely within **37–41ms**, leaving a ~9ms safety margin without any timeout risk.

#### 2. Dynamic Army Scaling Ceiling (68–96 Units)
- **Root Cause of Mid-Game Stalls**:
  - Previously, `armyTargetGlobal` was hard-capped at 42–44 units (`baseCap = 44`, `maxCap = 64`).
  - Beyond 44 units, marginal spawn gain dropped to 450, falling well below the 1,800 hold bonus.
  - The bot prematurely stopped spawning around Turn 10–12 to hold triangles statically, while top opponents kept spawning to 70–90 units and overran the board.
- **The Fix**:
  - `baseCap = max(68, (int)ceil(N * 1.5))`
  - `matchOpp = totalOppUnitsGlobal + 8`
  - `maxCap = 96`
  - `armyTargetGlobal = clamp(max(baseCap, matchOpp), 60, maxCap)`
- The bot maintains high spawn motivation through the mid- and late-game, scaling army size dynamically to outmatch the opponent.

---

### 4. Hypothesis Testing & Benchmark Ledger

All hypotheses were evaluated using `fast_bench.py` across 48–96 games with dual-seat symmetry.

| Hypothesis | Test Description | In-Sample (1–24) | Out-of-Sample (25–48) | Status / Decision |
| :--- | :--- | :---: | :---: | :--- |
| **Search Budget 40ms** | Increase `maxTimeMs` from 23ms to 40ms | **28W / 20L (58.3%)** | **24W / 2D / 22L (52.1%)** | **ADOPTED** in `v3.6` (55.2% combined) |
| **Dynamic Army Target** | Scale army ceiling to 68–96 units | **28W / 20L (58.3%)** | **26W / 2D / 20L (56.2%)** | **ADOPTED** in `v3.7` (57.3% combined) |
| **Surplus Backline Penalty** | Penalize units on safe nodes (`oppReach == 0`) | 25W / 23L (52.1%) | — | Reverted (reduced frontline stability) |
| **Coordinated Vacating Pass** | Deterministic pre-SA triangle vacating | 24W / 24L (50.0%) | — | Reverted (vacated choke-points prematurely) |
| **Reachable Op 3 Filter** | Restrict Op 3 strictly to 3-reachable triangles | 25W / 23L (52.1%) | — | Reverted (prevented 2-unit triangle staging) |
| **Aggressive ATTACK** | Relax pre-move attack thresholds to kill 2–4 units | 10W / 38L (20.8%) | — | Reverted (killing triangles destroys army growth) |
| **Cluster Synergy (+350)** | Bonus per shared edge among captured triangles | 28W / 20L (58.3%) | 22W / 2D / 24L (47.9%) | Reverted (high execution time ~49ms) |
| **ExactSolver Expansion** | Expand exact DFS threshold to `comb <= 16000` | 19W / 29L (39.6%) | — | Reverted (1-ply greedy hurt multi-ply positional value) |
| **Targeted Triangle Seeding** | Pre-assign units to nearest capturable triangles | 23W / 25L (47.9%) | — | Reverted (anchored SA early, reducing exploration) |

---

### 5. Next Steps to Reach Top 30

1. **Multi-Ply Alternating Factory Evaluator**:
   - Model the 2-stroke cycle directly in `evaluateState()`: credit a triangle that resets next turn if the current move vacates all 3 corners AND leaves units 1 step away on the adjacent ring (`extRing`).
2. **Frontline Encirclement & Combat Density**:
   - Top bots coordinate attacks using Priority 0 to break isolated enemy units. Refine combat awareness so units cluster at key bottleneck nodes without over-stacking.
3. **Continuous Arena Monitoring**:
   - Monitor `v3.7` as TrueSkill settles over 100+ battles.
