# CodinGame Fall Challenge 2026: Conversation & Development Context

*Updated on 2026-11-05 at 23:55 CET*
*User Account: `Ra7ch` (`Ra07Ch`)*
*Contest: CodinGame Fall Challenge 2026 (`fall-challenge-2026`)*
*Current League: Gold (Rank #87 / 862 overall, #87 / 423 in Gold, Score: 23.28)*
*Legend League Opening: `2026-11-07T09:00:00Z`*
*Active Submission ID: `41646650` (Version `v3.7`)*

---

## 1. Executive Summary & Benchmark Status

This repository contains the complete bot engine, local simulation arena, test harnesses, and versioned milestones for the **CodinGame Fall Challenge 2026: TryAngle Catch**.

* **Current Active Bot**: Version `v3.7` ([`bot.cpp`](bot.cpp), identical to [`bot_best.cpp`](bot_best.cpp) and archived in [`bot_v3_7_sub.cpp`](bot_v3_7_sub.cpp)).
* **Active Arena Submission**: `submission_id: 41646650` (`v3.7`).
  * Score: **23.28** (Gold Rank **#87 / 423**, Overall Rank **#87 / 862**).
  * 100% evaluated, 18W / 12L (60% win rate) across recent battles.
  * Defeated prominent Gold players including `vickingur` (Rank #97, 2/2 wins), `Leonard.`, `Stounate`, `c3lew`, `arukuka`, `Scp-omii`, `MathieuSoysal`, `gllm96`, and `[CG]loick`.
* **Milestone Progression**:
  * `v1.0`: Phase-heuristic machine (Rank ~325, score 22.84 in Bronze/Silver).
  * `v2.0`–`v2.9`: Unified 64-bit Bitmask Evaluator + Multi-Neighborhood Simulated Annealing + Multi-Ply Exact Solver (Silver Rank #103 / 707, score 33.88).
  * `v3.3`: Gold League transition with Priority 3/4 `ADD_PATH` and `REMOVE_PATH` (Gold Rank #104 / 402, score 20.75).
  * `v3.5`: Opening search temporal balancing (Gold Rank #85 / 415, score 22.80).
  * `v3.6`: 40ms SA budget scaling (~15k iterations/turn, peaked at Gold Rank #67 during evaluation).
  * `v3.7`: Dynamic Army Scaling to 96 units, breaking the 44-unit plateau bottleneck (Gold Rank #87, score 23.28, beating `v3.5` 57.3% locally).
* **Next Goal**: Push into **Top 30 in Gold League** (Score $\ge 28.29$, targeting `#30 Rikus` at 28.29, `#23 laconic_pixel` at 29.26, `#2 DomiKo` at 36.78, `#1 H1ggsb0s0n` at 37.26).

---

## 2. Official Referee Mechanics & Domain Forensics

Inspecting the official Java game engine (`referee/src/main/java/`) revealed critical invariants:

1. **Strict Turn Priority Order**:
   * **Priority 0**: `AttackTask` and `SurroundTask` execute simultaneously. Friendly and enemy surround eliminations happen together. Attacks consume 1 unit per corner from the attacking triangle and immediately destroy all enemy units on the targeted node.
   * **Priority 1**: `MoveTask`. Units step along graph edges simultaneously. Movement validity is checked against turn-start graph connectivity (`initAdj`).
   * **Priority 2**: `SpawnTask`. Owned triangles spawn 1 new unit on a designated corner. Spawning consumes **0 units** from the triangle corners.
   * **Priority 3**: `AddEdgeTask` (Gold League `ADD_PATH`). Creates an undirected edge between two non-adjacent nodes.
   * **Priority 4**: `RemoveEdgeTask` (Gold League `REMOVE_PATH`). Destroys an existing edge.
   * **Turn Finalization**: Triangle ownership is updated, and factory reset conditions are evaluated on the post-Priority-4 graph.

2. **The Triangle Reset Invariant**:
   * When spawned from, a triangle becomes unowned (`owner = null`) and `canCapture[player] = false`.
   * In `Board.finalizeTurn()`, `triangle.updateAllowedCaptures()` executes:
     $$\text{canCapture}[i] \gets \text{canCapture}[i] \lor (\text{node1.units}[i] == 0 \land \text{node2.units}[i] == 0 \land \text{node3.units}[i] == 0)$$
   * **Fundamental Rule**: A triangle can ONLY reset `meCan = true` if **all three corners simultaneously have zero friendly units**.
   * Because the newly spawned unit sits on one corner at Priority 2, a triangle **cannot reset on the turn it spawns**. It can only reset on Turn $T+1$ if all units vacate all 3 corners.

3. **The 2-Stroke Factory "Piston" Cycle**:
   * Top 30 Gold players maintain two alternating sets of triangles:
     * **Turn $T$**: Triangle Set A spawns units; Triangle Set B is captured by the moving army.
     * **Turn $T+1$**: Triangle Set B spawns units; Triangle Set A (now vacated) resets and is recaptured.
   * This alternating cycle yields 5–8 units every turn continuously without stalling or army plateaus.

4. **The 44-Unit Army Plateau Bottleneck (Solved in `v3.7`)**:
   * Prior to `v3.7`, `armyTargetGlobal` was hard-capped at 44 units.
   * Once friendly army reached 44, marginal spawn value dropped to 450, falling well below the 1,800 holding bonus (`evalState`). The bot froze factory spawning to hold triangles statically, allowing opponents to scale to 75–95 units and overwhelm the map.
   * `v3.7` dynamically scales army targets:
     $$\text{baseCap} = \max(68, \lceil 1.5 \times N \rceil), \quad \text{matchOpp} = \text{totalOppUnits} + 8, \quad \text{maxCap} = 96$$
   * Result: Outperformed previous champion with 57.3% win rate in 96 local games.

---

## 3. Bot Architecture & Simulated Annealing Engine

1. **Search Budget Scaling**:
   * `maxTimeMs = 40.0 ms`, `openLimit = 32.0 ms` (leaving a safe 10ms margin below the 50ms referee cutoff).
   * Executes ~15,000 SA iterations per turn.

2. **6 Mutation Neighborhoods (SA Operators)**:
   * **Op 1 (Single-Unit Move)**: Mutates one unit's destination to a connected neighbor or holds.
   * **Op 2 (Dynamic Spawn Selection)**: Mutates spawn corner on owned factories to point toward frontline hubs.
   * **Op 3 (3-Corner Slot/Relay Matcher)**: Macro-operator coordinating 2–3 units to capture a target triangle in a single turn. Recognizes degree $\le 2$ auto-owning corners.
   * **Op 4 (Factory Evacuation Pass)**: Macro-operator evacuating all 3 corners of neutral unresetted factories to trigger the end-of-turn reset condition.
   * **Op 5 (2-Unit Conveyor Push)**: Moves pairs of units along linear chains to feed frontline choke points.
   * **Op 6 (Stack Breakout)**: Coordinated dispersal of multi-unit stacks to avoid surround kills and enemy Priority 0 `ATTACK` strikes.

3. **Bitmask Evaluator (`evalState`)**:
   * 64-bit integer bitmasks for node sets, edges, reachability, and ownership.
   * Evaluates factory counts, net army size, multi-front distance potentials (`-cnt * minCapDist[u] * 85`), ear guards (`pureEarMask`), and boundary spawning (`interiorOwnedMask`).
   * Exact IEEE-754 80% domination calculation matching official Java referee (`cnt >= T_cnt * 0.8`).

4. **Gold League Graph Mutators (`ADD_PATH` / `REMOVE_PATH`)**:
   * Evaluated post-move with reserved corner units.
   * Prioritizes safe empty quadrilateral diagonal additions (turning 1 factory into 2 backline factories) and surround-kill edge cuts.

---

## 4. Exhaustive Ledger of Abandoned Hypotheses (Do NOT Repeat)

1. **Factory Holding ($M\_cnt \ge 26$)**: Scored 1W / 47L (2.1%). Sacrificing unit production to hold triangles permanently starves army scaling and loses to domination.
2. **Aggressive Tactical ATTACK (relaxing thresholds to kill 2–4 units)**: Scored 10W / 38L (20.8%). Attacking consumes 3 units and destroys the triangle, reducing net army growth by -4 units relative to spawning.
3. **ExactSolver Expansion (`comb <= 16000`)**: Scored 19W / 29L (39.6%). 1-ply DFS is myopic and greedy, sacrificing multi-turn positional advantage that SA macro-operators maintain.
4. **Targeted Triangle Seeding**: Pre-assigning units greedily before SA scored 23W / 25L (47.9%). Anchored SA early and reduced exploration entropy.
5. **Reachable Op 3 Filter**: Requiring all 3 corners to be reachable in 1 step scored 25W / 23L (52.1%). Prevented Op 3 from staging 2 units on a triangle for next-turn captures.
6. **Deterministic Vacating Pass**: Pre-SA vacating scored 24W / 24L (50.0%). Vacated critical frontline choke points prematurely.
7. **Contiguous Cluster Synergy (+350 / +180)**: Pairwise `endTriMasks` comparisons inside `evalState()` pushed turn runtime close to 49ms while scoring only 53.1%.
8. **Surplus Backline Penalty**: Scored 25W / 23L (52.1%), slightly worse than clean 40ms baseline.
9. **2-Unit Attack Zone Caution**: Scored 25W / 23L (52.1%), caused hesitation when contesting frontline hubs.

---

## 5. Development, Simulation & Testing Workflows

### Compile & Fast Local Benchmark
```bash
# Compile simulator and bot binaries
g++ -O3 -std=c++20 sim_arena.cpp -o sim_arena
g++ -O3 -std=c++20 bot.cpp -o bot
g++ -O3 -std=c++20 bot_best.cpp -o bot_best

# Run 24-seed (48-game) dual-seat benchmark with 4 worker threads
python3 fast_bench.py ./bot ./bot_best 24 4 1
```

### CodinGame API Live Testing
```bash
# Live test game against specific opponent ID
python3 cg_test.py 6851654 test_vickingur bot.cpp

# Inspect battle replay
python3 cg_test.py replay:917218893 replay_vickingur bot.cpp
```

### Submit to CodinGame Arena
```python
python3 -c '
import json, os, urllib.request
token = open(os.path.expanduser("~/.config/codingame/token")).read().strip()
req = urllib.request.Request(
    "https://www.codingame.com/api/v1/challenges/fall-challenge-2026/submissions",
    data=json.dumps({"language": "C++", "code": open("bot.cpp").read()}).encode("utf-8"),
    headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json", "User-Agent": "CodinGame-Agent/1.0"},
    method="POST"
)
print(json.loads(urllib.request.urlopen(req, timeout=30).read().decode("utf-8")))
'
```

---

## 6. Actionable Roadmap to Top 30 (Score $\ge 28.29$)

1. **Explicit 2-Stroke Factory Valuation in `evalState()`**:
   * For neutral unresetted triangles (`owner == -1 && !meCan`):
     * If all 3 corners are vacated at end-of-turn (triggering reset), check if friendly units on the immediate exterior ring are positioned to recapture the triangle on the next turn.
     * Reward this coordinated reset-recapture alignment with a calibrated dynamic bonus.
2. **Frontline Choke Point Reinforcement**:
   * Distinguish contested border hubs from safe backline nodes. Never vacate frontline hubs during factory reset passes when enemy units threaten breakthrough.
3. **Dual-Seat Benchmark Verification**:
   * Any improvement must achieve $\ge 56\%$ win rate against `bot_best` across $\ge 48$ local matches on both in-sample (seeds 1..24) and out-of-sample (seeds 25..48) configurations before deployment.
