# CodinGame Fall Challenge 2026: TryAngle Catch

Repository for the **CodinGame Fall Challenge 2026** bot by `Ra7ch` (`Ra07Ch`).

## Current Status
* **League**: Silver (currently progressing through Silver battles toward Gold unlock)
* **Latest Bot Version**: `v2.9` ([`bot.cpp`](bot.cpp), archived in [`bot_v2_9_sub.cpp`](bot_v2_9_sub.cpp))
* **Active Arena Submission ID**: `41634872`

---

## Bot Architecture & Evolution (`v1` -> `v2.9`)

1. **Unified Bitmask Evaluator**:
   * Exact IEEE-754 80% domination calculation matching the official Java referee.
   * Bitmask BFS distance matrices and surrounding-node kill / auto-ownership logic.
   * Thin-ring mirage guard on static opponent models.
   * Dynamic enemy `ATTACK` zone bitmasks avoiding 4-unit wipeouts.

2. **Multi-Operator Simulated Annealing (SA)**:
   * **Op 1**: Single-unit step moves.
   * **Op 2**: Dynamic factory spawn placement.
   * **Op 3**: 3-Corner slot/relay macro-matcher with degree-2 corner recognition.
   * **Op 4**: Coordinated `meCan == 0` factory evacuation.
   * **Op 5**: 2-unit conveyor/relay push.

3. **Multi-Ply Exact Solver**:
   * Deduplicated 3-ply exact tree search on Turn 1 and 2-ply on Turn 2 for 3-unit opening convergence.
   * Exact coordinate DFS solver for small combinatorial states.

---

## Key Files & Tools

* [`bot.cpp`](bot.cpp): Main bot source code.
* [`sim_arena.cpp`](sim_arena.cpp): High-performance C++20 local match runner supporting mirror-symmetric self-play and version benchmarking:
  ```bash
  g++ -O3 -std=c++20 sim_arena.cpp -o sim_arena
  ./sim_arena ./bot ./bot_v2_8 10
  ```
* [`cg_test.py`](cg_test.py): CodinGame public REST API runner for live test games and game replay testing:
  ```bash
  # Test vs top Silver bot (e.g., Bauda 6846565, DomiKo 6846603)
  python3 cg_test.py 6846565 test_tag

  # Test against a specific battle replay ID
  python3 cg_test.py replay:916598038 replay_tag
  ```
* [`referee/`](referee/): Upstream official Java game engine (`https://github.com/eulerscheZahl/TryAngle-Catch.git`).

---

## Versions Backup
* `bot_v1.cpp`: Initial phase-heuristic baseline bot (Rank ~325).
* `bot_v2_0.cpp`: Initial unified bitmask Simulated Annealing bot (30–0 vs v1).
* `bot_v2_4.cpp`: 3-Corner relay matcher and zero-bias symmetry.
* `bot_v2_6.cpp`: Isolated thin-ring surround mirage guard.
* `bot_v2_7.cpp`: Dynamic enemy ATTACK zone tracking and 4-stack wipeout avoidance.
* `bot_v2_8.cpp`: Corner robustness scoring and growth-phase factory valuation.
* `bot_v2_9_sub.cpp`: Multi-ply exact opening solver + 2-unit degree-2 corner recognition (active submission).
