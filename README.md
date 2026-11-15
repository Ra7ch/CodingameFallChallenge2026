# CodinGame Fall Challenge 2026: TryAngle Catch

My bot implementation and engine for the **CodinGame Fall Challenge 2026** (*TryAngle Catch*).

* **Contest Dates:** November 2 – November 9, 2026 (1-week AI contest)
* **Final Rank:** **#66** worldwide (**Legend League**, top ~6% among 1,040+ global participants)
* **Peak Rank:** **#41** (Score: 20.82) during Legend League matchmaking
* **Language:** C++17 (single-file, zero external dependencies, compiles with `-O3`)
* **Handle:** `Ra07Ch` (`ra7ch`)

---

## What is TryAngle Catch?

The game is played on a symmetric planar graph (up to 64 nodes and 256 possible triangles) between two players:

1. **Units & Movement:** Each turn, your units move along graph edges to adjacent nodes. Units move simultaneously.
2. **Triangles (Factories):** Triangles formed by graph edges can be captured by controlling all three vertices or by completely surrounding them.
3. **Spawning:** Each triangle you own acts as a factory, producing +1 new unit per turn at one of its three corners.
4. **Dominance Condition:** If a player controls $\ge 80\%$ of all triangles on the board, they win immediately. Otherwise, the game runs for up to 200 turns, and the player with the most triangles/units wins.
5. **Graph Edges (Gold/Legend):** In higher leagues, bots can also build new edges (`ADD_PATH`) or cut existing edges (`REMOVE_PATH`) to create new safe backline triangles or sever enemy supply routes.

### Turn Resolution Order (Referee Mechanics)
Order of execution inside the referee is critical:
```
Priority 0: ATTACK & SURROUND  (instant wipeouts before any movement)
Priority 1: MOVE               (units step along edges simultaneously)
Priority 2: SPAWN              (owned triangles produce 1 unit at chosen corner)
Priority 3: ADD_PATH           (create a new edge if vertices are valid)
Priority 4: REMOVE_PATH        (remove an edge to break triangles)
```
Because `ATTACK` and `SURROUND` happen *before* movement, walking blindly into an opponent-controlled node will get your units wiped out before they can even act.

---

## Core Bot Architecture

With a strict **50ms time limit per turn** (1000ms on turn 1) and a massive branching factor (up to 60+ units moving to 4–6 neighbors each), classical minimax or full tree search is out of the question. 

The bot is built around **Simulated Annealing (SA)** combined with a **Double-Oracle game-theoretic portfolio search** and a **2-ply rollout lookahead**.

```
                           ┌────────────────────────┐
                           │   Game State & Graph   │
                           └───────────┬────────────┘
                                       │
                    ┌──────────────────┴──────────────────┐
                    ▼                                     ▼
        ┌───────────────────────┐             ┌───────────────────────┐
        │ 64-bit Bitmask State  │             │ Opponent Portfolio    │
        │ - Node reachability   │             │ - Aggressive assault  │
        │ - Armed attack zones  │             │ - Factory turtle      │
        │ - Potential fields    │             │ - Surround hunter     │
        └───────────┬───────────┘             └───────────┬───────────┘
                    │                                     │
                    └──────────────────┬──────────────────┘
                                       ▼
                    ┌─────────────────────────────────────┐
                    │ Multi-Operator Simulated Annealing │
                    │ (~15,000 mutations in 35ms)        │
                    │ - Single step / dynamic spawn       │
                    │ - 3-corner triangle macro           │
                    │ - Highway hub stack breakout        │
                    └──────────────────┬──────────────────┘
                                       ▼
                    ┌─────────────────────────────────────┐
                    │ Deduplicated Top-8 2-Ply Rollout   │
                    │ - Evaluates candidate actions       │
                    │   against worst-case counter-moves  │
                    └──────────────────┬──────────────────┘
                                       ▼
                    ┌─────────────────────────────────────┐
                    │ Clean Output Actions                │
                    │ - Reciprocal edge cancellation      │
                    │ - MOVE; SPAWN; ADD_PATH             │
                    └─────────────────────────────────────┘
```

### 1. Unified 64-Bit Bitmask Evaluator
All nodes, edges, triangles, and territories are represented as `uint64_t` bitmasks:
* **Degree & Ear Masks (`pureEarMask`, `degLe2Mask`):** Topological filters that prevent units from wasting turns getting stuck inside dead-end degree-2 pockets.
* **Armed Threat Zones:** Pre-computing opponent reach masks to ensure friendly units never step into lethal 1-turn capture zones.
* **Cluster Boundary Tracking (`interiorOwnedMask`):** Redirects factory spawns away from deep interior pockets onto external boundary hubs where reinforcements are actually needed.

### 2. Multi-Operator Simulated Annealing (SA)
We run ~12,000 to 15,000 state mutations per turn within a strict 35–40ms wall-clock budget. Rather than mutating random single units, the SA engine uses domain-specific macro operators:
* **Op 1 (Single Step):** Move a unit to an adjacent neighbor.
* **Op 2 (Dynamic Spawn):** Switch a triangle's spawn target to the corner closest to the contest frontier.
* **Op 3 (3-Corner Macro):** Atomically moves units toward all three vertices of a target triangle simultaneously to complete captures in a single synchronized move.
* **Op 4 (Factory Evacuation):** Relocates units off endangered factories under enemy attack threat.
* **Op 5 (Conveyor / Relay Push):** 2-unit relay moves across choke points.
* **Op 6 (Stack Breakout):** Moves clustered unit stacks through graph hubs toward active fronts.

### 3. Double-Oracle Portfolio Search (v4.0+)
In Legend League, assuming the opponent won't react to your moves gets you punished immediately. 
From `v4.0` onward, the bot generates a **portfolio of realistic opponent counter-responses** each turn:
1. An aggressive push targeting our exposed triangles.
2. A defensive reinforcement holding current lines.
3. A flank/surround hunter punishing isolated forward units.

Candidate moves generated by SA are scored against this worst-case/weighted response portfolio, filtering out risky over-extensions that look good on a static board but fail against strong counter-play.

### 4. Deduplicated Top-8 2-Ply Rollout
During the final 10–12ms of each turn, the search transitions from SA to a **2-ply lookahead**:
* The top 8 structurally distinct candidate solutions from the SA pool are selected.
* Each candidate is simulated 2 plies forward against the opponent portfolio using the exact referee mechanics.
* The line that retains the best combination of safe factory ownership, unit safety, and board control is selected.

### 5. Highway Flow & Hub Stacking (v6.4 Final)
In earlier versions, our units frequently choked each other when routing through tight graph corridors. 
`v6.4` added **hub-aware unit stacking**:
* High-degree graph nodes ("hubs") are identified at initialization.
* Units funnel through these hubs as high-throughput relays, allowing reinforcements to reach the frontier 2–3 turns faster without clogging interior factory corners.

---

## League Progression & Evolution

| Version | League & Status | Key Mechanics & Changes |
| :--- | :--- | :--- |
| **`v1.0`** | Bronze / Silver (Rank ~325) | Greedy BFS pathing and heuristic triangle capture. |
| **`v2.0`–`v2.9`** | Silver (Rank #103) | 64-bit Bitmask evaluator + Simulated Annealing engine + exact opening solver for turns 1–2. |
| **`v3.3`** | Gold (Rank #104) | Initial Gold league transition: added `ADD_PATH` and `REMOVE_PATH` handling. |
| **`v3.7`** | Gold (Rank #82) | Scaled SA budget to 40ms, dynamic army target scaling (up to 96 units). |
| **`v4.0`** | Gold $\to$ Legend Promotion | **Double-Oracle Portfolio Search**: Zero-sum opponent counter-modeling. |
| **`v5.4`** | Legend League | Deduplicated Top-8 2-Ply rollout lookahead + highway flow routing. |
| **`v5.6`** | **Legend Rank #41 (Peak)** | Armed enemy attack zone defense + lethal wipeout protection. |
| **`v5.23`** | Legend League | Rebalanced search timing budget (41.5ms) and global gauntlet tuning. |
| **`v6.0`–`v6.2`** | Legend League | Anti-death-spiral hold logic + contestable triangle neutralization penalties. |
| **`v6.4`** | **Legend Rank #66 (Final)** | **Final Contest Submission**: Hub-aware unit stacking + defensive gap fixes. |

---

## Project Structure

```
.
├── README.md               # You are here
├── Makefile                # Build targets (make, make sim, make clean)
├── bot.cpp                 # Final Legend League submission (v6.4)
├── sim_arena.cpp           # Fast C++ headless simulator for local games
├── versions/               # Historical evolution archive (v1.0 through v7.x)
│   ├── bot_v1.cpp          # Early heuristic baseline
│   ├── bot_v2_0.cpp        # First Simulated Annealing version
│   ├── bot_v3_7_sub.cpp    # Gold League scaled SA champion
│   ├── bot_v4_oracle.cpp   # Double-Oracle game-theoretic engine
│   ├── bot_v5_6.cpp        # Arena peak version (#41 / 20.82)
│   └── pool/               # Comprehensive archive of all iterative builds
├── tools/                  # Python utilities & battle scripts
│   ├── cg_submit.py        # CodinGame arena submission script
│   ├── cg_leaderboard.py   # Live contest progress & leaderboard fetcher
│   ├── cg_battles.py       # Recent arena battle history & win-rate parser
│   ├── cg_test_game.py     # Runs test games against arena opponents
│   └── fast_bench.py       # Multi-threaded dual-seat head-to-head benchmark
└── referee/                # Official upstream Java referee engine
```

---

## Compiling & Running Locally

### 1. Build the Bot
```bash
make
# or directly:
g++ -O3 -std=c++17 -Wall bot.cpp -o bot
```

### 2. Build the Local Simulator
```bash
make sim
# or directly:
g++ -O3 -std=c++17 sim_arena.cpp -o sim_arena
```

### 3. Run a Benchmark Match
```bash
# Benchmark final bot against an earlier version across 50 games:
python3 tools/fast_bench.py ./bot ./versions/bot_v3_7_sub.cpp 50
```

---

## Takeaways

1. **Simulated Annealing over Pure Minimax for Large Coordination Games:** When a game has dozens of units that must coordinate across graph vertices, traditional tree search branches uncontrollably. SA with carefully designed macro perturbations explores the combinatorial space far more effectively.
2. **Double-Oracle is King in Legend League:** You cannot optimize against a stationary board. Anticipating 2–3 distinct opponent counter-strategies and optimizing against their weighted impact was what separated top-100 play from mid-tier bots.
3. **Flow Dynamics Matter:** In the late game, units often get stranded behind their own captured territory. Treating unit movement like a network flow problem (highway routing through hubs) solved unit starvation and kept the front lines constantly reinforced.
