with open("bot.cpp") as f:
    code = f.read()

# 1. Update isInEnemyKillZone & add computeEnemyKillZone
old_killzone = """// Check if a house is adjacent to an opponent triangle capable of attacking
bool isInEnemyKillZone(int u, const vector<Triangle>& triangles, const vector<int>& oppUnits) {
    for (const auto& t : triangles) {
        if (t.owner == 1 && oppUnits[t.h1] >= 1 && oppUnits[t.h2] >= 1 && oppUnits[t.h3] >= 1) {
            if (t.isNeighbor(u, adj) || t.hasNode(u)) {
                return true;
            }
        }
    }
    return false;
}"""

new_killzone = """bool inEnemyKillZone[MAX_HOUSES];

void computeEnemyKillZone(const vector<Triangle>& triangles, const vector<int>& oppUnits) {
    fill(inEnemyKillZone, inEnemyKillZone + MAX_HOUSES, false);
    for (const auto& t : triangles) {
        if (t.owner == 1 && oppUnits[t.h1] >= 1 && oppUnits[t.h2] >= 1 && oppUnits[t.h3] >= 1) {
            inEnemyKillZone[t.h1] = true;
            inEnemyKillZone[t.h2] = true;
            inEnemyKillZone[t.h3] = true;
            for (int c : {t.h1, t.h2, t.h3}) {
                for (int v : adj[c]) {
                    inEnemyKillZone[v] = true;
                }
            }
        }
    }
}

// Check if a house is adjacent to an opponent triangle capable of attacking (O(1) lookup)
bool isInEnemyKillZone(int u, const vector<Triangle>& triangles, const vector<int>& oppUnits) {
    if (u < 0 || u >= MAX_HOUSES) return false;
    return inEnemyKillZone[u];
}"""

assert old_killzone in code, "old_killzone not found"
code = code.replace(old_killzone, new_killzone)

# 2. Call computeEnemyKillZone(triangles, oppUnits) each turn
old_turn_start = """        int linkableCount;
        cin >> linkableCount;
        for (int i = 0; i < linkableCount; ++i) {
            int h1, h2;
            cin >> h1 >> h2;
        }"""

new_turn_start = """        int linkableCount;
        cin >> linkableCount;
        for (int i = 0; i < linkableCount; ++i) {
            int h1, h2;
            cin >> h1 >> h2;
        }

        computeEnemyKillZone(triangles, oppUnits);"""

assert old_turn_start in code, "old_turn_start not found"
code = code.replace(old_turn_start, new_turn_start)

# 3. Replace Turn 2 & Turn 3 solvers with non-allocating, time-guarded, fallback versions
t2_start = "        } else if (turn == 2 && myTriangleCount >= 1 && totalMyUnits == 3) {"
t_gen_start = "        } else {\n            // GENERAL ENGINE (Turn 3+)"

idx_t2 = code.find(t2_start)
idx_gen = code.find(t_gen_start)
assert idx_t2 != -1 and idx_gen != -1, f"Indices not found: {idx_t2}, {idx_gen}"

new_solvers = """        }

        bool turn2Solved = false;
        if (turn == 2 && myTriangleCount >= 1 && totalMyUnits == 3) {
            // TURN 2: Exact Joint Solver across all owned triangles (SPAWN + 3 MOVEs)
            double bestScore = -1e9;
            int bestTriId = -1;
            int bestS = -1, bestD0 = -1, bestD1 = -1, bestD2 = -1;

            vector<int> d0Choices = {unitLocs[0]};
            for (int v : adj[unitLocs[0]]) d0Choices.push_back(v);
            vector<int> d1Choices = {unitLocs[1]};
            for (int v : adj[unitLocs[1]]) d1Choices.push_back(v);
            vector<int> d2Choices = {unitLocs[2]};
            for (int v : adj[unitLocs[2]]) d2Choices.push_back(v);

            int demand[64] = {0};
            bool owned[64];

            for (const auto& startTri : triangles) {
                if (startTri.owner != 0) continue;
                int spawnCorners[3] = {startTri.h1, startTri.h2, startTri.h3};

                for (int s : spawnCorners) {
                    demand[s]++;
                    for (int d0 : d0Choices) {
                        demand[d0]++;
                        for (int d1 : d1Choices) {
                            demand[d1]++;
                            for (int d2 : d2Choices) {
                                demand[d2]++;
                                if (s == d0 || s == d1 || s == d2 || d0 == d1 || d0 == d2 || d1 == d2) {
                                    demand[d2]--;
                                    continue;
                                }

                                bool safe = true;
                                for (int e : {s, d0, d1, d2}) {
                                    if (inEnemyKillZone[e] && oppUnits[e] >= 1) {
                                        safe = false;
                                        break;
                                    }
                                }
                                if (safe) {
                                    for (int i = 0; i < houseCount; ++i) {
                                        owned[i] = (demand[i] > oppUnits[i]);
                                    }
                                    for (int i = 0; i < houseCount; ++i) {
                                        if (!owned[i] && !adj[i].empty()) {
                                            bool allNb = true;
                                            for (int v : adj[i]) {
                                                if (demand[v] <= oppUnits[v]) {
                                                    allNb = false;
                                                    break;
                                                }
                                            }
                                            if (allNb) owned[i] = true;
                                        }
                                    }

                                    int cap = 0;
                                    for (const auto& t : triangles) {
                                        if (t.id == startTri.id) continue;
                                        if (owned[t.h1] && owned[t.h2] && owned[t.h3]) cap++;
                                    }

                                    double sc = cap * 100000.0 + (pos[s].x + pos[d0].x + pos[d1].x + pos[d2].x);
                                    if (d0 == unitLocs[0]) sc += 5.0;
                                    if (d1 == unitLocs[1]) sc += 5.0;
                                    if (d2 == unitLocs[2]) sc += 5.0;

                                    if (sc > bestScore) {
                                        bestScore = sc;
                                        bestTriId = startTri.id;
                                        bestS = s;
                                        bestD0 = d0;
                                        bestD1 = d1;
                                        bestD2 = d2;
                                    }
                                }
                                demand[d2]--;
                            }
                            demand[d1]--;
                        }
                        demand[d0]--;
                    }
                    demand[s]--;
                }
            }

            if (bestS != -1 && bestTriId != -1) {
                turn2Solved = true;
                const auto& startTri = triangles[bestTriId];
                int c2 = (bestS == startTri.h1 ? startTri.h2 : startTri.h1);
                int c3 = (bestS == startTri.h3 ? startTri.h2 : startTri.h3);
                actions.push_back("SPAWN " + to_string(bestS) + " " + to_string(c2) + " " + to_string(c3));
                if (bestD0 != unitLocs[0]) actions.push_back("MOVE " + to_string(unitLocs[0]) + " " + to_string(bestD0) + " 1");
                if (bestD1 != unitLocs[1]) actions.push_back("MOVE " + to_string(unitLocs[1]) + " " + to_string(bestD1) + " 1");
                if (bestD2 != unitLocs[2]) actions.push_back("MOVE " + to_string(unitLocs[2]) + " " + to_string(bestD2) + " 1");
            }
        }

        bool turn3Solved = false;
        if (turn == 3 && totalMyUnits <= 5 && unitLocs.size() == 4) {
            // TURN 3: Exact Joint Solver for 4 units + optional spawn with 30ms Time Guard
            auto tTurn3Start = chrono::high_resolution_clock::now();
            double bestScore = -1e9;
            int bestTriId = -1;
            int bestS = -1, bestD0 = -1, bestD1 = -1, bestD2 = -1, bestD3 = -1;
            int maxCap = 0;

            vector<int> d0Choices = {unitLocs[0]};
            for (int v : adj[unitLocs[0]]) d0Choices.push_back(v);
            vector<int> d1Choices = {unitLocs[1]};
            for (int v : adj[unitLocs[1]]) d1Choices.push_back(v);
            vector<int> d2Choices = {unitLocs[2]};
            for (int v : adj[unitLocs[2]]) d2Choices.push_back(v);
            vector<int> d3Choices = {unitLocs[3]};
            for (int v : adj[unitLocs[3]]) d3Choices.push_back(v);

            struct SpawnChoice {
                int triId;
                int corner;
            };
            vector<SpawnChoice> spawnChoices = {{-1, -1}}; // No spawn option
            for (const auto& t : triangles) {
                if (t.owner == 0) {
                    spawnChoices.push_back({t.id, t.h1});
                    spawnChoices.push_back({t.id, t.h2});
                    spawnChoices.push_back({t.id, t.h3});
                }
            }

            int demand[64] = {0};
            bool owned[64];
            bool timedOut = false;

            for (const auto& sp : spawnChoices) {
                if (timedOut) break;
                int s = sp.corner;
                if (s != -1) demand[s]++;

                for (int d0 : d0Choices) {
                    if (timedOut) break;
                    auto now = chrono::high_resolution_clock::now();
                    if (chrono::duration<double, milli>(now - tTurn3Start).count() > 30.0) {
                        cerr << "Turn 3 solver time guard (30ms). Terminating search early.\\n";
                        timedOut = true;
                        break;
                    }

                    demand[d0]++;
                    for (int d1 : d1Choices) {
                        demand[d1]++;
                        for (int d2 : d2Choices) {
                            demand[d2]++;
                            for (int d3 : d3Choices) {
                                demand[d3]++;

                                bool capacityOk = true;
                                for (int node : {s, d0, d1, d2, d3}) {
                                    if (node != -1) {
                                        if (demand[node] > 2 && oppUnits[node] == 0) {
                                            capacityOk = false;
                                            break;
                                        }
                                        if (inEnemyKillZone[node] && oppUnits[node] >= demand[node]) {
                                            capacityOk = false;
                                            break;
                                        }
                                    }
                                }

                                if (capacityOk) {
                                    for (int i = 0; i < houseCount; ++i) {
                                        owned[i] = (demand[i] > oppUnits[i]);
                                    }
                                    for (int i = 0; i < houseCount; ++i) {
                                        if (!owned[i] && !adj[i].empty()) {
                                            bool allNb = true;
                                            for (int v : adj[i]) {
                                                if (demand[v] <= oppUnits[v]) {
                                                    allNb = false;
                                                    break;
                                                }
                                            }
                                            if (allNb) owned[i] = true;
                                        }
                                    }

                                    int cap = 0;
                                    for (const auto& t : triangles) {
                                        if (sp.triId != -1 && t.id == sp.triId) continue;
                                        if (!t.meCan && t.owner != 0) continue;
                                        if (owned[t.h1] && owned[t.h2] && owned[t.h3]) cap++;
                                    }

                                    double sc = cap * 100000.0 + (pos[d0].x + pos[d1].x + pos[d2].x + pos[d3].x);
                                    if (s != -1) sc += pos[s].x;
                                    if (d0 == unitLocs[0]) sc += 5.0;
                                    if (d1 == unitLocs[1]) sc += 5.0;
                                    if (d2 == unitLocs[2]) sc += 5.0;
                                    if (d3 == unitLocs[3]) sc += 5.0;
                                    if (sp.triId == -1) sc += 50.0;

                                    if (sc > bestScore) {
                                        bestScore = sc;
                                        bestTriId = sp.triId;
                                        bestS = s;
                                        bestD0 = d0;
                                        bestD1 = d1;
                                        bestD2 = d2;
                                        bestD3 = d3;
                                        maxCap = cap;
                                    }
                                }

                                demand[d3]--;
                            }
                            demand[d2]--;
                        }
                        demand[d1]--;
                    }
                    demand[d0]--;
                }
                if (s != -1) demand[s]--;
            }

            if (maxCap > 0 && bestD0 != -1) {
                turn3Solved = true;
                if (bestTriId != -1 && bestS != -1) {
                    const auto& startTri = triangles[bestTriId];
                    int c2 = (bestS == startTri.h1 ? startTri.h2 : startTri.h1);
                    int c3 = (bestS == startTri.h3 ? startTri.h2 : startTri.h3);
                    actions.push_back("SPAWN " + to_string(bestS) + " " + to_string(c2) + " " + to_string(c3));
                }
                if (bestD0 != unitLocs[0]) actions.push_back("MOVE " + to_string(unitLocs[0]) + " " + to_string(bestD0) + " 1");
                if (bestD1 != unitLocs[1]) actions.push_back("MOVE " + to_string(unitLocs[1]) + " " + to_string(bestD1) + " 1");
                if (bestD2 != unitLocs[2]) actions.push_back("MOVE " + to_string(unitLocs[2]) + " " + to_string(bestD2) + " 1");
                if (bestD3 != unitLocs[3]) actions.push_back("MOVE " + to_string(unitLocs[3]) + " " + to_string(bestD3) + " 1");
                cerr << "Turn 3 solver succeeded: maxCap=" << maxCap << " actions=" << actions.size() << "\\n";
            } else {
                cerr << "Turn 3 solver found no 1-step capture (maxCap=" << maxCap << "). Falling back to General Engine.\\n";
            }
        }

        if ((turn == 2 && !turn2Solved) || (turn == 3 && !turn3Solved) || (turn > 3)) {
            // GENERAL ENGINE (Turn 3+)"""

code = code[:idx_t2] + new_solvers + code[idx_gen + len(t_gen_start):]

with open("bot.cpp", "w") as f:
    f.write(code)

print("Patch applied successfully!")
