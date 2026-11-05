# CodinGame Fall Challenge 2026: TryAngle Catch

Repository for the **CodinGame Fall Challenge 2026** bot by `Ra7ch` (`Ra07Ch`).

## Current Status
* **League**: Gold — **Rank #87 / 862** (`#87 / 423` in Gold, `score: 23.28`, `18W / 12L` (60% win rate); Legend League opens `2026-11-07 09:00 UTC`)
* **Latest Bot Version**: `v3.7` ([`bot.cpp`](bot.cpp), archived in [`bot_v3_7_sub.cpp`](bot_v3_7_sub.cpp))
* **Active Arena Submission ID**: `41646650` (Score: `23.28`, climbed from `v3.3` at `#104` / `20.75` and `v2.9` at `#130` / `19.42`)
* **Target Objective**: Push into **Top 30 in Gold League** (Score $\ge 28.29$).

---

## Bot Architecture & Evolution (`v1` -> `v3.7`)

1. **Gold League Priority-Exact Graph Mechanics**:
   * **Priority 0 (`ATTACK` & `SURROUND`)**: Instant wipeouts executed before moves. Calculates positive-EV trades (`effKill = oppU + savedBoxed`) without bleeding factories early.
   * **Priority 1 (`MOVE`)**: Units step across graph edges simultaneously. Adjacency preserves turn-start connectivity (`initAdj` / `initAdjMask`) while reserving corner units for Priority 3/4 graph edits.
   * **Priority 2 (`SPAWN`)**: Spawns 1 unit per owned factory; consumes **0 units** from triangle corners.
   * **Priority 3 (`ADD_PATH`)**: Empty quadrilateral diagonal builder (`newTris >= 2`, `localSupply >= 5`, `nearOppCorners <= 1`) converting 1 surplus factory into 2 immediate safe backline factories.
   * **Priority 4 (`REMOVE_PATH`)**: Surround-kill assassination (`surrKill >= 4`) and double enemy-factory edge cuts.

2. **Unified Bitmask Evaluator (`evalState`)**:
   * Exact IEEE-754 80% domination calculation matching the official Java referee (`cnt >= T_cnt * 0.8`).
   * **Topological Pure Degree-2 Ear Guard (`pureEarMask`)**: Excludes empty pure degree-2 ears from distance targets and prevents parking friendly units inside auto-owned dead-end pockets.
   * **Owned-Cluster Boundary Spawning (`interiorOwnedMask`)**: Redirects factory spawns from interior pockets of multi-triangle owned clusters onto external boundary hubs.
   * **Dynamic Enemy `ATTACK` & Surround Guards**: `oppReach`-verified `activeArmedMask` / `activePotMask` and degree-aware `canBeSurr1StepMask` (`degLe2Mask`) preventing stack wipeouts and 1-step encirclements.
   * **Multi-Front Wave Marching**: Scaled potential fields (`-cnt * minCapDist[u] * 85` + dampened quadratic dispersion).

3. **Multi-Operator Simulated Annealing (SA) & Exact Opening Search**:
   * **Op 1**: Single-unit step moves.
   * **Op 2**: Dynamic factory spawn placement.
   * **Op 3**: 3-Corner slot/relay macro-matcher with degree-2 auto-ownership recognition.
   * **Op 4**: Coordinated `meCan == 0` factory evacuation.
   * **Op 5**: 2-unit conveyor/relay push.
   * **Op 6**: Multi-unit stack breakout / coordinated assault move.
   * **Multi-Ply Exact Opening Solver**: Deduplicated 3-ply exact tree search on Turn 1 and 2-ply on Turn 2 for 3-unit opening convergence.

4. **Dynamic Army Scaling to 96 Units & Scaled SA Budget (`v3.6` / `v3.7`)**:
   * **Search Budget (`v3.6`)**: Scaled wall-clock SA budget to 40ms (`maxTimeMs = 40.0`, `openLimit = 32.0`), executing ~15,000 SA mutations per turn without timeouts.
   * **Dynamic Army Scaling (`v3.7`)**: Overcame the 44-unit plateau bottleneck by dynamically raising army target caps (`baseCap = max(68, 1.5 * N)`, `maxCap = 96`, matching opponent army size + 8).
   * **Benchmarking**: Beat `v3.5` **54W / 2D / 40L (57.3% win rate)** across 96 local games.

---

## Arena Track Record (Gold League)

Recent arena victories include top Gold competitors:
* **`vickingur`** (Rank #97, agent ID `6851654`): Defeated in 2 separate arena matches (`917218893` and `917221669`).
* **`Leonard.`** (`6849521`)
* **`Stounate`** (`6852362`, `6852402`)
* **`c3lew`** (`6849287`)
* **`arukuka`** (`6850328`)
* **`Scp-omii`** (`6851358`)
* **`MathieuSoysal`** (`6849878`)
* **`gllm96`** (`6851134`)
* **`[CG]loick`** (`6849274`)

---

## Key Files & Tools

* [`bot.cpp`](bot.cpp): Main Gold-league C++20 bot source code (identical to [`bot_best.cpp`](bot_best.cpp) and [`bot_v3_7_sub.cpp`](bot_v3_7_sub.cpp)).
* [`sim_arena.cpp`](sim_arena.cpp): Full Gold-league C++20 local arena simulator (`ATTACK`, `MOVE`, `SPAWN`, `ADD_PATH`, `REMOVE_PATH`, mirror-symmetric maps, and optional single-seed verbose trace):
  ```bash
  g++ -O3 -std=c++20 sim_arena.cpp -o sim_arena
  g++ -O3 -std=c++20 bot.cpp -o bot
  python3 fast_bench.py ./bot ./bot_best 24 4 1
  ```
* [`cg_test.py`](cg_test.py): CodinGame public REST API runner for live test games and battle replay testing:
  ```bash
  # Test vs top Gold opponent (e.g., vickingur 6851654, DomiKo 6849220, H1ggsb0s0n 6850341)
  python3 cg_test.py 6851654 test_vickingur bot.cpp

  # Test against a specific battle replay ID
  python3 cg_test.py replay:917218893 replay_tag bot.cpp
  ```
* [`referee/`](referee/): Upstream official Java game engine (`https://github.com/eulerscheZahl/TryAngle-Catch.git`).

---

## Versions Backup Ledger
* `bot_v1.cpp`: Initial phase-heuristic baseline bot (Rank ~325).
* `bot_v2_0.cpp`: Initial unified bitmask Simulated Annealing bot (30–0 vs v1).
* `bot_v2_4.cpp`: 3-Corner relay matcher and zero-bias symmetry.
* `bot_v2_6.cpp`: Isolated thin-ring surround mirage guard.
* `bot_v2_7.cpp`: Dynamic enemy `ATTACK` zone tracking and 4-stack wipeout avoidance.
* `bot_v2_8.cpp`: Corner robustness scoring and growth-phase factory valuation.
* `bot_v2_9_sub.cpp`: Multi-ply exact opening solver + 2-unit degree-2 corner recognition (`sub: 41634872`, Gold Rank #130).
* `bot_v3_3_sub.cpp`: Gold League `ADD_PATH` / `REMOVE_PATH`, `ATTACK` economics, `pureEarMask`, and `interiorOwnedMask` (`sub: 41644058`, Gold Rank #104, score `20.75`).
* `bot_v3_4_sub.cpp`: Multi-ply opening search temporal balancing and edge creation validation (`sub: 41645231`).
* `bot_v3_5_sub.cpp`: Opening timing tuning, army target calibration (`sub: 41645744`, Gold Rank #85, score `22.80`).
* `bot_v3_6_sub.cpp`: 40ms wall-clock search budget scaling (~15k SA iterations) (`sub: 41646617`, peaked at Gold Rank #67).
* `bot_v3_7_sub.cpp`: 40ms SA budget + Dynamic Army Scaling (68–96 units) (`sub: 41646650`, Gold Rank #87 / 423, score `23.28`).
