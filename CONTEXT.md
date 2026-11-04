# CodinGame Fall Challenge 2026: Conversation & Development Context

*Generated on 2026-11-04 at 16:30 CET*
*User Account: `Ra7ch` (`Ra07Ch`)*
*Contest: CodinGame Fall Challenge 2026 (`fall-challenge-2026`)*
*Current League: Silver (Rank #103 / 707, Score 33.88)*
*Gold League Opening: `2026-11-05T09:00:00Z`*

---

## 1. Executive Summary

This session advanced our bot from a 1,600-line heuristic phase machine (`bot_v1.cpp`, Rank ~325, score 22.84) to a state-of-the-art **Unified 64-bit Bitmask Referee Evaluator + Multi-Neighborhood Simulated Annealing + Multi-Ply Exact Solver** (`v2.0` through `v2.9`, [`bot.cpp`](bot.cpp), archived in [`bot_v2_9_sub.cpp`](bot_v2_9_sub.cpp)).

* **Arena Submission**: `submission_id: 41634872` (Version `v2.9`).
* **Final Arena Result**: **Rank #103 / 707** (Score: **33.88**, 100% battles completed, 58% win rate in Silver).
* **Head-to-Head vs `v1`**: **30 Wins / 0 Losses (100% win rate)** in local mirror-symmetric self-play, winning by domination or total elimination in 13–28 turns.
* **Execution Latency**: Tuned to $\le 23.0\text{ ms}$ wall-clock budget per turn; actual runtimes consistently range between $20.05\text{ ms}$ and $20.45\text{ ms}$ locally and on CodinGame Linux containers.

---

## 2. Game Mechanics & Strategic Fundamentals

* **Map & Topology**: Graph of nodes ($N \le 64$) with undirected planar paths. Triangles ($T \le 160$) are empty 3-cycles with no interior nodes.
* **Turn Sequence & Priority**:
  1. **ATTACK** (Priority 0): Triangles with $\ge 1$ unit on all 3 corners can attack an adjacent enemy node, destroying all enemy units on that node instantly before movements execute. Consumes 1 unit per corner and resets triangle ownership to neutral (`meCan = false`).
  2. **MOVE** (Priority 1): Units step across connected edges. Simultaneous across both players.
  3. **SPAWN** (Priority 2): Owned triangles can spawn 1 unit on one of their corners. Triangle becomes neutral (`meCan = false`) until all 3 corners are vacated.
  4. **Referee Update Phase**:
     * **Surround Elimination**: Any node where all adjacent neighbors have strictly more opponent units than friendly units has all its units eliminated simultaneously.
     * **Triangle Ownership**: A triangle is captured when all 3 corners are owned (`units[me] > units[opp]` or surrounded by friendly nodes).
     * **Factory Reset**: A neutral triangle resets to `meCan = true` at end-of-turn if and only if **all 3 corners have 0 friendly units**.
     * **Domination Win**: Controlling $\ge \lceil 0.8 \times T \rceil$ triangles triggers an immediate instant win.

---

## 3. Bot Architecture Evolution

### Baseline: `v1` (`bot_v1.cpp`)
* Handcrafted heuristic machine divided into rigid phases: opening, expansion, defense, endgame.
* Prone to unit clumping, over-defending low-value hubs, and failing to coordinate 3-unit triangle captures. Rank hovered around ~325.

### `v2.0` (`bot_v2_0.cpp`): Unified Bitmask Simulated Annealing
* Replaced heuristic logic with an exact 64-bit bitmask evaluator mirroring the official Java referee (`Board.java`, `Triangle.java`).
* Simulated Annealing running ~80,000 iterations per turn:
  * Op 1: Single unit movement mutation.
  * Op 2: Spawn choice mutation.
  * Op 3: Triangle corner slot assignment.
  * Op 4: Neutral factory evacuation.
* Result: Beat `v1` **30W / 0L (100%)** locally.

### `v2.4` (`bot_v2_4.cpp`): 3-Corner Relay Matcher & Zero-Bias Symmetry
* Fixed asymmetric `+x` tiebreaker bias.
* Implemented degree-ordered 3-corner slot locking in SA Op 3 to coordinate multi-unit relay captures (`X -> A, A -> B, B -> C`).
* Result: Beat `v2.0` **13W / 11L (54.2%)**.

### `v2.6` (`bot_v2_6.cpp`): Thin-Ring Surround Mirage Guard
* **Root Cause Discovered vs #1 Bauda**: `evalState` assumed placing 1 unit on each neighbor of an enemy hub would surround and kill it, failing to realize the enemy's 2 units would step onto one neighbor, tying the node and breaking all surrounding triangles simultaneously.
* **Fix**: Evaluator checks `thinNbrs` ($u_0[v] \le \text{oppUnits}[v] + 1$). It refuses to hallucinate surround kills or auto-ownership unless the ring is robustly occupied.
* Result: Parity with `v2.4` locally (**12W / 12L**), sweeping seeds 6–8 6–0.

### `v2.7` (`bot_v2_7.cpp`): Dynamic Enemy ATTACK Zone Tracking
* **Root Cause Discovered vs #1 Bauda & #2 Xanrum**: The bot stacked 4 units on nodes adjacent to enemy triangles on Turn $T$, which the enemy wiped out with Priority 0 `ATTACK` at the start of Turn $T+1$.
* **Fix**: Added `EnemyAtkTri` bitmask precomputation. Evaluator dynamically checks whether enemy triangles are neutralized or intact, heavily penalizing 4-unit stacks on vulnerable nodes ($-5000\text{ pts}$).
* Result: Beat `v2.4` **10W / 6L (62.5%)** locally.

### `v2.8` (`bot_v2_8.cpp`): Corner Robustness & Hold-Phase Valuation
* Scaled factory steal values dynamically: during expansion, enemy triangles are discounted (opponents spawn anyway); during late-game holding/domination, steals receive heavy priority.
* Guarded against evacuating contested frontline degree $\ge 3$ hubs.
* Result: Parity with `v2.7` (**10W / 10L**).

### `v2.9` (`bot.cpp` / `bot_v2_9_sub.cpp` - Active Submission)
* **Deduplicated Multi-Ply Exact Opening Solver** (`solveOpening3Units`):
  * Exhaustively searches 3-ply on Turn 1 (using up to 220 ms) and 2-ply on Turn 2 (up to 18 ms) for opening 3-unit configurations.
  * Deduplicates symmetry states (permutations of unit indices) and accurately models factory captures across multi-turn horizons.
* **Degree-2 Corner Optimization**: Op 3 recognizes corners with degree $\le 2$ embedded inside triangles that auto-own without requiring physical unit placement.
* **Uncontested Factory Attraction**: Multi-step BFS potential adds bonus attraction to free backline triangles.
* Result: Beat `v2.8` **11W / 9L (55%)** locally, deployed to Arena, surging to **Rank #103**.

---

## 4. Key Lessons & Abandoned Pitfalls

1. **Avoid Blanket Frontline Discounts (`v2.1`, `v2.3`)**:
   * Attempting to penalize any node where `oppReach >= 1` caused the bot to play cowardly, ceding the center map to single enemy scouts and losing 6–14.
2. **Avoid Uncapped Spawn Matching (`v2.5`)**:
   * Targeting `oppTriCountGlobal + totalOppUnitsGlobal` without a hard cap caused the bot to melt down its own triangles to spawn 68 units when the opponent stopped spawning at 42, giving the opponent an immediate 80% domination victory.
3. **Always Respect IEEE-754 Domination**:
   * Domination in the Java referee uses `cnt >= T_cnt * 0.8` with double precision. Integer math like `(T_cnt * 4 + 4) / 5` can differ by 1 on specific triangle counts.

---

## 5. Development & Testing Infrastructure

* **Source Code**:
  * [`bot.cpp`](bot.cpp): Main bot source file.
  * [`sim_arena.cpp`](sim_arena.cpp): Standalone symmetric local match runner (uses identical graph generation, CCW triangulation, and IEEE-754 referee rules as the official CodinGame engine).
  * [`cg_test.py`](cg_test.py): CodinGame public API client for automated test games and replay debugging.
* **Local Arena Commands**:
  ```bash
  # Compile local referee and bot
  g++ -O3 -std=c++20 sim_arena.cpp -o sim_arena
  g++ -O3 -std=c++20 bot.cpp -o bot

  # Run 10-seed (20-game) mirror symmetric benchmark
  ./sim_arena ./bot ./bot_v2_8 10
  ```
* **CodinGame API Commands**:
  ```bash
  # Test vs top Silver bots (e.g., Bauda 6846565, DomiKo 6846603)
  python3 cg_test.py 6846565 test_bauda

  # Replay a specific match to debug losses
  python3 cg_test.py replay:916598038 replay_domiko
  ```
* **Submitting to Arena**:
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

## 6. Next Steps for Gold League (`2026-11-05T09:00:00Z`)

1. **Top Silver Replay Analysis**:
   * Silver #1 `Bauda` and #3 `DomiKo` use dynamic 1-step move anticipation. When they hold center clusters, they execute predictive attacks and concentrated frontline pushes.
   * Add a fast 1-ply minimax opponent response model inside SA evaluation for frontline nodes.
2. **Coordinated Factory Farming (Op 4 Enhancements)**:
   * When an owned factory is safe and uncontested, coordinate stepping all 3 units off in unison to trigger a 1-turn factory reset, doubling unit generation rates.
3. **Gold League Rules Watch**:
   * At `2026-11-05T09:00:00Z`, inspect `GET /challenges/fall-challenge-2026` for newly unlocked rules (e.g., `allowNewEdges`, linkable edge creation/removal commands, or increased time limits).
