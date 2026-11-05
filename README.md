# CodinGame Fall Challenge 2026: TryAngle Catch

Repository for the **CodinGame Fall Challenge 2026** bot by `Ra7ch` (`Ra07Ch`).

## Current Status
* **League**: Gold — **Rank #104 / 832** (`#104 / 402` in Gold, `score: 20.75`, `50W / 48L`; Legend opens `2026-11-07 09:00 UTC` with no new rules)
* **Latest Bot Version**: `v3.3` ([`bot.cpp`](bot.cpp), archived in [`bot_v3_3_sub.cpp`](bot_v3_3_sub.cpp))
* **Active Arena Submission ID**: `41644058` (climbed +26 ranks from `v2.9` submission `41634872` at `#130` / `19.42`, beating `v2.9` `20W / 0D / 10L` (`66.7%`) in symmetric Gold self-play)

---

## Bot Architecture & Evolution (`v1` -> `v3.3`)

1. **Gold League Tactical Phases (Priority-Exact Graph Separation)**:
   * **Priority 0 (`ATTACK`)**: Positive-EV stack wipeouts (`effKill = oppU + savedBoxed`) and late-game army-lead factory pressure without bleeding factories on low-value early trades.
   * **Priority 1 (`MOVE`) vs Priority 3/4 (`ADD_PATH` / `REMOVE_PATH`)**: Preserves turn-start adjacency (`initAdj` / `initAdjMask`) and reserves corner units for post-move graph edits while evaluating end-of-turn captures on the post-Priority-4 graph.
   * **Priority 3 (`ADD_PATH`)**: Safe empty quadrilateral diagonal builder (`newTris >= 2`, `localSupply >= 5`, `nearOppCorners <= 1`) turning 1 surplus factory into 2 immediate safe backline factories, guarded near 80% domination.
   * **Priority 4 (`REMOVE_PATH`)**: Surround-kill assassination (`surrKill >= 4`) and double enemy-factory edge cuts.

2. **Unified Bitmask Evaluator (`evalState`)**:
   * Exact IEEE-754 80% domination calculation matching the official Java referee (`cnt >= T_cnt * 0.8`).
   * **Topological Pure Degree-2 Ear Guard (`pureEarMask`)**: Excludes empty pure degree-2 ears from `minCapDist` / `nodePotential` distance targets and prevents parking friendly units inside auto-owned dead-end pockets.
   * **Owned-Cluster Boundary Spawning (`interiorOwnedMask`)**: Redirects factory spawns from interior pockets of multi-triangle owned clusters onto external boundary hubs without suppressing growth-phase spawns.
   * **Dynamic Enemy `ATTACK` & Surround Guards**: `oppReach`-verified `activeArmedMask` / `activePotMask` and degree-aware `canBeSurr1StepMask` (`degLe2Mask`) to prevent stack wipeouts and 1-step encirclements.
   * Multi-front wave marching (`-cnt * minCapDist[u] * 85` + dampened quadratic dispersion) scaling cleanly to 44–64 Gold-league armies.

3. **Multi-Operator Simulated Annealing (SA) & Exact Opening Search**:
   * **Op 1**: Single-unit step moves.
   * **Op 2**: Dynamic factory spawn placement.
   * **Op 3**: 3-Corner slot/relay macro-matcher with degree-2 auto-ownership recognition.
   * **Op 4**: Coordinated `meCan == 0` factory evacuation.
   * **Op 5**: 2-unit conveyor/relay push.
   * **Op 6**: Multi-unit stack breakout / coordinated assault move.
   * **Multi-Ply Exact Solver**: Deduplicated 3-ply exact tree search on Turn 1 and 2-ply on Turn 2 for 3-unit opening convergence.

---

## Key Files & Tools

* [`bot.cpp`](bot.cpp): Main Gold-league C++20 bot source code.
* [`sim_arena.cpp`](sim_arena.cpp): Full Gold-league C++20 local arena simulator (`ATTACK`, `MOVE`, `SPAWN`, `ADD_PATH`, `REMOVE_PATH`, mirror-symmetric maps, and optional single-seed verbose trace):
  ```bash
  g++ -O3 -std=c++20 sim_arena.cpp -o sim_arena
  g++ -O3 -std=c++20 bot.cpp -o bot
  ./sim_arena ./bot ./bot_v2_9_sub 15
  ```
* [`cg_test.py`](cg_test.py): CodinGame public REST API runner for live test games and battle replay testing:
  ```bash
  # Test vs top Gold opponent (e.g., H1ggsb0s0n 6850341, DomiKo 6849220)
  python3 cg_test.py 6850341 test_tag bot.cpp

  # Test against a specific battle replay ID
  python3 cg_test.py replay:917063721 replay_tag bot.cpp
  ```
* [`referee/`](referee/): Upstream official Java game engine (`https://github.com/eulerscheZahl/TryAngle-Catch.git`).

---

## Versions Backup
* `bot_v1.cpp`: Initial phase-heuristic baseline bot (Rank ~325).
* `bot_v2_0.cpp`: Initial unified bitmask Simulated Annealing bot (30–0 vs v1).
* `bot_v2_4.cpp`: 3-Corner relay matcher and zero-bias symmetry.
* `bot_v2_6.cpp`: Isolated thin-ring surround mirage guard.
* `bot_v2_7.cpp`: Dynamic enemy `ATTACK` zone tracking and 4-stack wipeout avoidance.
* `bot_v2_8.cpp`: Corner robustness scoring and growth-phase factory valuation.
* `bot_v2_9_sub.cpp`: Multi-ply exact opening solver + 2-unit degree-2 corner recognition (`submission_id: 41634872`, Gold Rank #130).
* `bot_v3_3_sub.cpp`: Gold League `ADD_PATH` / `REMOVE_PATH`, `ATTACK` economics, `pureEarMask`, and `interiorOwnedMask` (`submission_id: 41644058`, Gold Rank #104, score `20.75`).
