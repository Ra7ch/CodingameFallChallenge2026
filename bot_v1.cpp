#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <chrono>

using namespace std;

const int INF = 1e9;
const int MAX_HOUSES = 55;

struct Point {
    int x, y;
};

struct Triangle {
    int id;
    int h1, h2, h3;
    int owner; // -1 neutral, 0 me, 1 opp
    bool meCan, oppCan;

    bool hasNode(int u) const {
        return h1 == u || h2 == u || h3 == u;
    }

    int sharedCorners(const Triangle& ot) const {
        int cnt = 0;
        if (hasNode(ot.h1)) cnt++;
        if (hasNode(ot.h2)) cnt++;
        if (hasNode(ot.h3)) cnt++;
        return cnt;
    }

    bool isNeighbor(int u, const vector<vector<int>>& adj) const {
        for (int c : {h1, h2, h3}) {
            for (int v : adj[c]) {
                if (v == u) return true;
            }
        }
        return false;
    }
};

int houseCount;
vector<Point> pos;
vector<vector<int>> adj;
int distMatrix[MAX_HOUSES][MAX_HOUSES];

void computeDistances() {
    for (int i = 0; i < houseCount; ++i) {
        for (int j = 0; j < houseCount; ++j) {
            distMatrix[i][j] = (i == j ? 0 : INF);
        }
    }
    for (int start = 0; start < houseCount; ++start) {
        queue<int> q;
        q.push(start);
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (int v : adj[u]) {
                if (distMatrix[start][v] > distMatrix[start][u] + 1) {
                    distMatrix[start][v] = distMatrix[start][u] + 1;
                    q.push(v);
                }
            }
        }
    }
}

// Kuhn Bipartite Matcher from Slot perspective
// Each slot represents 1 demanded unit at house slotHouse[s]
// Units already at slotHouse[s] are listed FIRST (0 moves), then units 1 step away (1 move).
struct SlotMatcher {
    int numUnits;
    const vector<int>& unitPositions;
    vector<int> slotHouse;
    vector<vector<int>> slotUnits;
    vector<int> matchSlot; // matchSlot[unit] = slot matched to this unit (-1 if none)
    vector<int> matchUnit; // matchUnit[slot] = unit matched to this slot (-1 if none)
    vector<bool> visited;

    SlotMatcher(int nUnits, const vector<int>& uPos) 
        : numUnits(nUnits), unitPositions(uPos) {
        matchSlot.assign(numUnits, -1);
    }

    bool dfs(int slot) {
        if (visited[slot]) return false;
        visited[slot] = true;
        for (int u : slotUnits[slot]) {
            if (matchSlot[u] == -1 || dfs(matchSlot[u])) {
                matchSlot[u] = slot;
                matchUnit[slot] = u;
                return true;
            }
        }
        return false;
    }

    bool tryAddDemand(const vector<int>& newHouses) {
        auto oldSlotHouse = slotHouse;
        auto oldSlotUnits = slotUnits;
        auto oldMatchSlot = matchSlot;
        auto oldMatchUnit = matchUnit;

        vector<int> newSlotIds;
        for (int h : newHouses) {
            int sId = slotHouse.size();
            slotHouse.push_back(h);
            matchUnit.push_back(-1);

            // Units already at h have highest preference (0 moves!)
            vector<int> canReach;
            for (int u = 0; u < numUnits; ++u) {
                if (unitPositions[u] == h) {
                    canReach.push_back(u);
                }
            }
            // Units 1 step away (1 move)
            for (int u = 0; u < numUnits; ++u) {
                if (unitPositions[u] != h && distMatrix[unitPositions[u]][h] <= 1) {
                    canReach.push_back(u);
                }
            }
            slotUnits.push_back(canReach);
            newSlotIds.push_back(sId);
        }

        bool allOk = true;
        for (int sId : newSlotIds) {
            visited.assign(slotHouse.size(), false);
            if (!dfs(sId)) {
                allOk = false;
                break;
            }
        }

        if (allOk) {
            return true;
        } else {
            // Rollback
            slotHouse = oldSlotHouse;
            slotUnits = oldSlotUnits;
            matchSlot = oldMatchSlot;
            matchUnit = oldMatchUnit;
            return false;
        }
    }
};

bool inEnemyKillZone[MAX_HOUSES];

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
}

// Check if corner c of triangle t is automatically surrounded by the other two corners
bool isNodeAutoSurroundedInTri(int c, const Triangle& t) {
    if (adj[c].empty()) return false;
    for (int nb : adj[c]) {
        if (nb == c) continue;
        if (nb != t.h1 && nb != t.h2 && nb != t.h3) return false;
    }
    return true;
}

// Compute which nodes are owned by player 0 given our planned demand and opponent units
vector<bool> computeOwnedNodes(const map<int, int>& myDemand, const vector<int>& oppUnits) {
    vector<bool> owned(houseCount, false);
    for (int i = 0; i < houseCount; ++i) {
        int myU = (myDemand.count(i) ? myDemand.at(i) : 0);
        if (myU > oppUnits[i]) {
            owned[i] = true;
        }
    }
    // Surrounding check: if all neighbors are owned by player 0, house i is owned even with 0 units
    for (int i = 0; i < houseCount; ++i) {
        if (!owned[i] && !adj[i].empty()) {
            bool allNbOwned = true;
            for (int v : adj[i]) {
                int myU = (myDemand.count(v) ? myDemand.at(v) : 0);
                if (myU <= oppUnits[v]) {
                    allNbOwned = false;
                    break;
                }
            }
            if (allNbOwned) {
                owned[i] = true;
            }
        }
    }
    return owned;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    if (!(cin >> houseCount)) return 0;

    pos.resize(houseCount);
    for (int i = 0; i < houseCount; ++i) {
        int hid, hx, hy;
        cin >> hid >> hx >> hy;
        pos[hid] = {hx, hy};
    }

    int turn = 0;

    while (true) {
        int myScore, oppScore;
        if (!(cin >> myScore >> oppScore)) break;
        turn++;

        vector<int> myUnits(houseCount), oppUnits(houseCount);
        int totalMyUnits = 0, totalOppUnits = 0;
        for (int i = 0; i < houseCount; ++i) {
            int hid, uMe, uOpp;
            cin >> hid >> uMe >> uOpp;
            myUnits[hid] = uMe;
            oppUnits[hid] = uOpp;
            totalMyUnits += uMe;
            totalOppUnits += uOpp;
        }

        int pathCount;
        cin >> pathCount;
        adj.assign(houseCount, vector<int>());
        for (int i = 0; i < pathCount; ++i) {
            int h1, h2;
            cin >> h1 >> h2;
            adj[h1].push_back(h2);
            adj[h2].push_back(h1);
        }

        computeDistances();

        int triangleCount;
        cin >> triangleCount;
        vector<Triangle> triangles(triangleCount);
        int myTriangleCount = 0, oppTriangleCount = 0;
        for (int i = 0; i < triangleCount; ++i) {
            triangles[i].id = i;
            cin >> triangles[i].h1 >> triangles[i].h2 >> triangles[i].h3 
                >> triangles[i].owner >> triangles[i].meCan >> triangles[i].oppCan;
            if (triangles[i].owner == 0) myTriangleCount++;
            if (triangles[i].owner == 1) oppTriangleCount++;
        }

        int linkableCount;
        cin >> linkableCount;
        for (int i = 0; i < linkableCount; ++i) {
            int h1, h2;
            cin >> h1 >> h2;
        }

        computeEnemyKillZone(triangles, oppUnits);

        cerr << "Turn " << turn << " | Score: " << myScore << "-" << oppScore 
             << " | Units: " << totalMyUnits << "-" << totalOppUnits 
             << " | Triangles: " << myTriangleCount << "-" << oppTriangleCount << "\n";

        vector<string> actions;

        // Collect moving units available at start of turn
        vector<int> unitLocs;
        for (int i = 0; i < houseCount; ++i) {
            for (int k = 0; k < myUnits[i]; ++k) {
                unitLocs.push_back(i);
            }
        }

        if (turn == 1) {
            // TURN 1: Exact Joint Solver for 3 initial units
            // Directly searches for 1-step captures (including multi-triangle surrounding captures)
            vector<int> d0Choices = {unitLocs[0]};
            for (int v : adj[unitLocs[0]]) d0Choices.push_back(v);
            vector<int> d1Choices = {unitLocs[1]};
            for (int v : adj[unitLocs[1]]) d1Choices.push_back(v);
            vector<int> d2Choices = {unitLocs[2]};
            for (int v : adj[unitLocs[2]]) d2Choices.push_back(v);

            double bestScore = -1e9;
            int bestD0 = -1, bestD1 = -1, bestD2 = -1;
            int maxCap = 0;

            for (int d0 : d0Choices) {
                for (int d1 : d1Choices) {
                    for (int d2 : d2Choices) {
                        if (d0 == d1 || d0 == d2 || d1 == d2) continue;

                        map<int, int> demand;
                        demand[d0]++; demand[d1]++; demand[d2]++;
                        vector<bool> owned = computeOwnedNodes(demand, oppUnits);

                        int cap = 0;
                        for (const auto& t : triangles) {
                            if (owned[t.h1] && owned[t.h2] && owned[t.h3]) cap++;
                        }

                        double sc = cap * 100000.0 + (pos[d0].x + pos[d1].x + pos[d2].x);
                        if (d0 == unitLocs[0]) sc += 5.0;
                        if (d1 == unitLocs[1]) sc += 5.0;
                        if (d2 == unitLocs[2]) sc += 5.0;

                        if (sc > bestScore) {
                            bestScore = sc;
                            bestD0 = d0;
                            bestD1 = d1;
                            bestD2 = d2;
                            maxCap = cap;
                        }
                    }
                }
            }

            if (maxCap > 0 && bestD0 != -1) {
                if (bestD0 != unitLocs[0]) actions.push_back("MOVE " + to_string(unitLocs[0]) + " " + to_string(bestD0) + " 1");
                if (bestD1 != unitLocs[1]) actions.push_back("MOVE " + to_string(unitLocs[1]) + " " + to_string(bestD1) + " 1");
                if (bestD2 != unitLocs[2]) actions.push_back("MOVE " + to_string(unitLocs[2]) + " " + to_string(bestD2) + " 1");
            } else {
                int minMaxD = INF, minSumD = INF;
                vector<int> bestAssignment(3, -1);
                for (const auto& t : triangles) {
                    if (!t.meCan) continue;
                    int corners[3] = {t.h1, t.h2, t.h3};
                    vector<int> p = {0, 1, 2};
                    do {
                        int maxD = 0, sumD = 0;
                        for (int idx = 0; idx < 3; ++idx) {
                            int d = distMatrix[unitLocs[p[idx]]][corners[idx]];
                            maxD = max(maxD, d);
                            sumD += d;
                        }
                        if (maxD < minMaxD || (maxD == minMaxD && sumD < minSumD)) {
                            minMaxD = maxD;
                            minSumD = sumD;
                            for (int idx = 0; idx < 3; ++idx) {
                                bestAssignment[p[idx]] = corners[idx];
                            }
                        }
                    } while (next_permutation(p.begin(), p.end()));
                }

                for (size_t u = 0; u < unitLocs.size(); ++u) {
                    int from = unitLocs[u];
                    int target = bestAssignment[u];
                    if (target != -1 && from != target) {
                        int nextStep = from;
                        for (int v : adj[from]) {
                            if (distMatrix[v][target] < distMatrix[nextStep][target]) {
                                nextStep = v;
                            }
                        }
                        if (nextStep != from) {
                            actions.push_back("MOVE " + to_string(from) + " " + to_string(nextStep) + " 1");
                        }
                    }
                }
            }
        }

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
                        cerr << "Turn 3 solver time guard (30ms). Terminating search early.\n";
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
                cerr << "Turn 3 solver succeeded: maxCap=" << maxCap << " actions=" << actions.size() << "\n";
            } else {
                cerr << "Turn 3 solver found no 1-step capture (maxCap=" << maxCap << "). Falling back to General Engine.\n";
            }
        }

        if ((turn == 2 && !turn2Solved) || (turn == 3 && !turn3Solved) || (turn > 3)) {
            // GENERAL ENGINE (Turn 3+)

            // PHASE 1: STRATEGIC ATTACK
            // Heavily guarded: only attack if army is massive (>= 25 units) and target has >= 4 units
            if (totalMyUnits >= 25) {
                for (auto& t : triangles) {
                    if (t.owner == 0 && myUnits[t.h1] >= 1 && myUnits[t.h2] >= 1 && myUnits[t.h3] >= 1) {
                        int bestTarget = -1;
                        int maxTargetVal = 0;

                        for (int u = 0; u < houseCount; ++u) {
                            if (t.hasNode(u)) continue;
                            if (oppUnits[u] >= 4 && t.isNeighbor(u, adj)) {
                                int enemyTrianglesBroken = 0;
                                for (const auto& ot : triangles) {
                                    if (ot.owner == 1 && ot.hasNode(u)) {
                                        enemyTrianglesBroken++;
                                    }
                                }
                                int val = oppUnits[u] * 10 + enemyTrianglesBroken * 25;
                                if (val > maxTargetVal) {
                                    maxTargetVal = val;
                                    bestTarget = u;
                                }
                            }
                        }

                        if (bestTarget != -1 && maxTargetVal >= 50) {
                            actions.push_back("ATTACK " + to_string(t.h1) + " " + to_string(t.h2) + " " + to_string(t.h3) + " " + to_string(bestTarget));
                            t.owner = -1;
                            myUnits[t.h1]--;
                            myUnits[t.h2]--;
                            myUnits[t.h3]--;
                            totalMyUnits -= 3;
                            oppUnits[bestTarget] = 0;
                            break;
                        }
                    }
                }
            }

            // PHASE 2: EXPONENTIAL ARMY BOOMING
            // Always preserve at least 1 triangle to score and maintain spawning momentum!
            int maxSpawnsAllowed = 0;
            if (myTriangleCount >= 1) {
                if (totalMyUnits < 8) {
                    maxSpawnsAllowed = max(1, myTriangleCount - 1);
                } else if (totalMyUnits < 20) {
                    if (myTriangleCount >= 2) {
                        maxSpawnsAllowed = max(1, min(myTriangleCount - 1, 3));
                    } else {
                        maxSpawnsAllowed = 0; // Hold the last triangle
                    }
                } else if (totalMyUnits < 35) {
                    if (myTriangleCount >= 3) {
                        maxSpawnsAllowed = max(1, min(myTriangleCount - 2, 4));
                    } else {
                        maxSpawnsAllowed = 0;
                    }
                } else {
                    maxSpawnsAllowed = max(0, min(myTriangleCount - 4, 3));
                }
            }

            struct SpawnOption {
                int triId;
                int bestCorner;
                int c2, c3;
                double sacrificeCost;
            };
            vector<SpawnOption> spawnOptions;

            for (const auto& t : triangles) {
                if (t.owner == 0) {
                    int sharedOwnedEdges = 0;
                    for (const auto& ot : triangles) {
                        if (ot.id != t.id && ot.owner == 0 && t.sharedCorners(ot) == 2) {
                            sharedOwnedEdges++;
                        }
                    }

                    int corners[3] = {t.h1, t.h2, t.h3};
                    int bestCorner = t.h1;
                    int maxPotential = -1e9;
                    for (int c : corners) {
                        int pot = 0;
                        if (isInEnemyKillZone(c, triangles, oppUnits)) pot -= 50;
                        for (const auto& ot : triangles) {
                            if (ot.hasNode(c) && ot.owner != 0 && ot.meCan) {
                                pot += 10;
                                if (ot.owner == 1) pot += 20;
                            }
                        }
                        int minOppD = INF;
                        for (int u = 0; u < houseCount; ++u) {
                            if (oppUnits[u] > 0) minOppD = min(minOppD, distMatrix[c][u]);
                        }
                        pot -= minOppD;
                        if (pot > maxPotential) {
                            maxPotential = pot;
                            bestCorner = c;
                        }
                    }
                    int c2 = (bestCorner == t.h1 ? t.h2 : t.h1);
                    int c3 = (bestCorner == t.h3 ? t.h2 : t.h3);

                    double sacrificeCost = sharedOwnedEdges * 100.0 - maxPotential;
                    spawnOptions.push_back({t.id, bestCorner, c2, c3, sacrificeCost});
                }
            }

            sort(spawnOptions.begin(), spawnOptions.end(), [](const SpawnOption& a, const SpawnOption& b) {
                return a.sacrificeCost < b.sacrificeCost;
            });

            vector<int> spawnedUnitsAt(houseCount, 0);
            int spawnsDone = 0;
            set<int> cornersSpawnedThisTurn;
            for (const auto& so : spawnOptions) {
                if (spawnsDone >= maxSpawnsAllowed) break;
                const auto& t = triangles[so.triId];
                int corners[3] = {t.h1, t.h2, t.h3};
                int chosenCorner = corners[0];
                int maxPot = -1e9;
                for (int c : corners) {
                    int pot = 0;
                    if (cornersSpawnedThisTurn.count(c)) pot -= 1000;
                    if (isInEnemyKillZone(c, triangles, oppUnits)) pot -= 50;
                    for (const auto& ot : triangles) {
                        if (ot.hasNode(c) && ot.owner != 0 && ot.meCan) {
                            pot += 10;
                            if (ot.owner == 1) pot += 20;
                        }
                    }
                    int minOppD = INF;
                    for (int u = 0; u < houseCount; ++u) {
                        if (oppUnits[u] > 0) minOppD = min(minOppD, distMatrix[c][u]);
                    }
                    pot -= minOppD;
                    if (pot > maxPot) {
                        maxPot = pot;
                        chosenCorner = c;
                    }
                }
                int c2 = (chosenCorner == t.h1 ? t.h2 : t.h1);
                int c3 = (chosenCorner == t.h3 ? t.h2 : t.h3);
                actions.push_back("SPAWN " + to_string(chosenCorner) + " " + to_string(c2) + " " + to_string(c3));
                cornersSpawnedThisTurn.insert(chosenCorner);
                spawnedUnitsAt[chosenCorner]++;
                triangles[so.triId].owner = -1; // Consumed!
                spawnsDone++;
            }

            map<pair<int, int>, int> moveCount;
            vector<int> plannedUnits = spawnedUnitsAt;

            unitLocs.clear();
            for (int i = 0; i < houseCount; ++i) {
                for (int k = 0; k < myUnits[i]; ++k) {
                    unitLocs.push_back(i);
                }
            }

            // PHASE 3: 1-STEP WAVE EXPANSION (Dynamic SlotMatcher Engine)
            int M = unitLocs.size();
            SlotMatcher matcher(M, unitLocs);

            map<int, int> committedHouseDemand;
            for (int i = 0; i < houseCount; ++i) {
                if (spawnedUnitsAt[i] > 0) {
                    committedHouseDemand[i] = spawnedUnitsAt[i];
                }
            }
            set<int> committedTriangles;

            for (const auto& t : triangles) {
                if (t.owner == 0) {
                    vector<int> newHouses;
                    map<int, int> tempDemand = committedHouseDemand;
                    for (int c : {t.h1, t.h2, t.h3}) {
                        if (isNodeAutoSurroundedInTri(c, t)) continue;
                        int req = max(1, oppUnits[c] + 1);
                        while (tempDemand[c] < req) {
                            newHouses.push_back(c);
                            tempDemand[c]++;
                        }
                    }
                    if (matcher.tryAddDemand(newHouses)) {
                        committedTriangles.insert(t.id);
                        committedHouseDemand = tempDemand;
                    }
                }
            }

            while (true) {
                vector<bool> ownedNodes = computeOwnedNodes(committedHouseDemand, oppUnits);
                int availableUnits = M - (int)matcher.slotHouse.size();

                struct Candidate {
                    int triId;
                    double score;
                    int extraUnitsNeeded;
                    vector<int> newHouses;
                    map<int, int> newDemand;
                };
                vector<Candidate> candidates;

                for (const auto& t : triangles) {
                    if (!t.meCan && t.owner != 0) continue;
                    if (committedTriangles.count(t.id)) continue;

                    bool unsafeKillZone = false;
                    for (int c : {t.h1, t.h2, t.h3}) {
                        if (isInEnemyKillZone(c, triangles, oppUnits) && oppUnits[c] >= 1) {
                            unsafeKillZone = true;
                            break;
                        }
                    }
                    if (unsafeKillZone) continue;

                    vector<int> newHouses;
                    map<int, int> tempDemand = committedHouseDemand;
                    int alreadyOwnedCorners = 0;

                    for (int c : {t.h1, t.h2, t.h3}) {
                        if (ownedNodes[c] || isNodeAutoSurroundedInTri(c, t)) {
                            alreadyOwnedCorners++;
                        } else {
                            int req = max(1, oppUnits[c] + 1);
                            if (isInEnemyKillZone(c, triangles, oppUnits)) {
                                req = 1;
                            } else if (oppUnits[c] == 0) {
                                bool oppNear = false;
                                for (int nb : adj[c]) {
                                    if (oppUnits[nb] > 0) { oppNear = true; break; }
                                }
                                if (oppNear && availableUnits >= 2) req = 2;
                            }
                            while (tempDemand[c] < req) {
                                newHouses.push_back(c);
                                tempDemand[c]++;
                            }
                        }
                    }

                    int extraUnitsNeeded = (int)newHouses.size();
                    if (extraUnitsNeeded > availableUnits) continue;

                    bool cornersReachable = true;
                    for (int c : newHouses) {
                        bool reachable = false;
                        for (int uPos : unitLocs) {
                            if (uPos == c || distMatrix[uPos][c] <= 1) {
                                reachable = true;
                                break;
                            }
                        }
                        if (!reachable) {
                            cornersReachable = false;
                            break;
                        }
                    }
                    if (!cornersReachable) continue;

                    int newCornersNeeded = 3 - alreadyOwnedCorners;
                    double score = 0;
                    if (extraUnitsNeeded == 0) {
                        score = 1000000.0;
                    } else if (newCornersNeeded == 1) {
                        score = 100000.0;
                    } else if (newCornersNeeded == 2) {
                        score = 10000.0;
                    } else {
                        score = 1000.0;
                    }

                    score -= extraUnitsNeeded * 500.0;

                    if (t.owner == 1) score += 15000.0;

                    int sharedWithCommitted = 0;
                    for (int cTriId : committedTriangles) {
                        sharedWithCommitted = max(sharedWithCommitted, t.sharedCorners(triangles[cTriId]));
                    }
                    if (sharedWithCommitted == 2) {
                        score += 50000.0;
                    } else if (sharedWithCommitted == 1) {
                        score += 10000.0;
                    }

                    for (const auto& ot : triangles) {
                        if (ot.id != t.id && t.sharedCorners(ot) == 2 && ot.meCan) {
                            score += 50.0;
                        }
                    }

                    candidates.push_back({t.id, score, extraUnitsNeeded, newHouses, tempDemand});
                }

                if (candidates.empty()) break;

                sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                    return a.score > b.score;
                });

                bool committedAny = false;
                for (const auto& cand : candidates) {
                    if (cand.newHouses.empty()) {
                        committedTriangles.insert(cand.triId);
                        committedAny = true;
                        break;
                    }
                    if (matcher.tryAddDemand(cand.newHouses)) {
                        committedTriangles.insert(cand.triId);
                        committedHouseDemand = cand.newDemand;
                        committedAny = true;
                        break;
                    }
                }

                if (!committedAny) {
                    break;
                }
            }

            cerr << "  Phase 3 committed " << committedTriangles.size() << " triangles.\n";

            vector<bool> unitAssigned(M, false);
            for (int u = 0; u < M; ++u) {
                int slot = matcher.matchSlot[u];
                if (slot != -1) {
                    unitAssigned[u] = true;
                    int from = unitLocs[u];
                    int to = matcher.slotHouse[slot];
                    plannedUnits[to]++;
                    if (from != to) {
                        moveCount[{from, to}]++;
                    }
                }
            }

            // PHASE 4: COORDINATED TARGET TRIANGLE ASSIGNMENT & MARCHING
            vector<int> surplusUnits;
            for (int u = 0; u < M; ++u) {
                if (!unitAssigned[u]) {
                    surplusUnits.push_back(u);
                }
            }
            cerr << "  Phase 4 surplus units: " << surplusUnits.size() << "/" << M << "\n";

            auto getNodeCapacity = [&](int node) {
                if (isInEnemyKillZone(node, triangles, oppUnits)) return 1;
                bool nearOpp = (oppUnits[node] > 0);
                for (int nb : adj[node]) {
                    if (oppUnits[nb] > 0) { nearOpp = true; break; }
                }
                if (nearOpp) return max(2, oppUnits[node] + 1);
                return 2; // Safe transit node: allows up to 2 units to prevent traffic jams!
            };

            auto wouldBeSurrounded = [&](int candidateNode) {
                if (adj[candidateNode].empty()) return false;
                for (int w : adj[candidateNode]) {
                    if (plannedUnits[w] >= oppUnits[w] || oppUnits[w] == 0) {
                        return false;
                    }
                }
                return true;
            };

            if (!surplusUnits.empty()) {
                auto getUnassignedSurplus = [&]() {
                    vector<int> res;
                    for (int u = 0; u < M; ++u) {
                        if (!unitAssigned[u]) res.push_back(u);
                    }
                    return res;
                };

                auto getSurplusAtNode = [&]() {
                    vector<vector<int>> atNode(houseCount);
                    for (int u = 0; u < M; ++u) {
                        if (!unitAssigned[u]) {
                            atNode[unitLocs[u]].push_back(u);
                        }
                    }
                    return atNode;
                };

                // STAGE 4A: Secure triangles with 3 corners already held (0 moves required)
                bool stage4AChanged = true;
                while (stage4AChanged) {
                    stage4AChanged = false;
                    auto surplusAtNode = getSurplusAtNode();

                    for (const auto& t : triangles) {
                        if (!t.meCan && t.owner != 0) continue;
                        if (committedTriangles.count(t.id)) continue;

                        bool unsafeKillZone = false;
                        for (int c : {t.h1, t.h2, t.h3}) {
                            if (isInEnemyKillZone(c, triangles, oppUnits) && oppUnits[c] >= 1) {
                                unsafeKillZone = true;
                                break;
                            }
                        }
                        if (unsafeKillZone) continue;

                        int corners[3] = {t.h1, t.h2, t.h3};
                        int neededAt[3];
                        bool allHoldable = true;
                        for (int i = 0; i < 3; ++i) {
                            int c = corners[i];
                            if (isNodeAutoSurroundedInTri(c, t)) {
                                neededAt[i] = 0;
                                continue;
                            }
                            int req = max(1, oppUnits[c] + 1);
                            if (isInEnemyKillZone(c, triangles, oppUnits)) req = 1;
                            neededAt[i] = max(0, req - plannedUnits[c]);
                            if ((int)surplusAtNode[c].size() < neededAt[i]) {
                                allHoldable = false;
                                break;
                            }
                        }

                        if (allHoldable) {
                            for (int i = 0; i < 3; ++i) {
                                int c = corners[i];
                                for (int k = 0; k < neededAt[i]; ++k) {
                                    int su = surplusAtNode[c][k];
                                    unitAssigned[su] = true;
                                    plannedUnits[c]++;
                                }
                            }
                            committedTriangles.insert(t.id);
                            stage4AChanged = true;
                            break;
                        }
                    }
                }

                // STAGE 4B: Prioritize triangles with 2 corners already held (1-2 step completion)
                bool stage4BChanged = true;
                while (stage4BChanged) {
                    stage4BChanged = false;
                    auto surplusAtNode = getSurplusAtNode();

                    int bestTriId = -1;
                    double bestScore = -1e9;
                    int bestHeldC1 = -1, bestHeldC2 = -1, bestMissingC = -1;
                    int bestNeededAtC1 = 0, bestNeededAtC2 = 0;
                    vector<int> bestUnitsForMissing;

                    for (const auto& t : triangles) {
                        if (!t.meCan && t.owner != 0) continue;
                        if (committedTriangles.count(t.id)) continue;

                        bool unsafeKillZone = false;
                        for (int c : {t.h1, t.h2, t.h3}) {
                            if (isInEnemyKillZone(c, triangles, oppUnits) && oppUnits[c] >= 1) {
                                unsafeKillZone = true;
                                break;
                            }
                        }
                        if (unsafeKillZone) continue;

                        int corners[3] = {t.h1, t.h2, t.h3};
                        int neededAt[3];
                        bool isHeld[3];
                        int heldCount = 0;

                        for (int i = 0; i < 3; ++i) {
                            int c = corners[i];
                            if (isNodeAutoSurroundedInTri(c, t)) {
                                neededAt[i] = 0;
                                isHeld[i] = true;
                                heldCount++;
                                continue;
                            }
                            int req = max(1, oppUnits[c] + 1);
                            if (isInEnemyKillZone(c, triangles, oppUnits)) {
                                req = 1;
                            } else if (oppUnits[c] == 0) {
                                bool oppNear = false;
                                for (int nb : adj[c]) {
                                    if (oppUnits[nb] > 0) { oppNear = true; break; }
                                }
                                if (oppNear && (int)surplusAtNode[c].size() >= 2) req = 2;
                            }
                            neededAt[i] = max(0, req - plannedUnits[c]);
                            isHeld[i] = ((int)surplusAtNode[c].size() >= neededAt[i]);
                            if (isHeld[i]) heldCount++;
                        }

                        if (heldCount != 2) continue;

                        int missingIdx = -1;
                        int heldIdx1 = -1, heldIdx2 = -1;
                        for (int i = 0; i < 3; ++i) {
                            if (!isHeld[i]) missingIdx = i;
                            else if (heldIdx1 == -1) heldIdx1 = i;
                            else heldIdx2 = i;
                        }

                        int missingC = corners[missingIdx];
                        int reqMissing = max(1, oppUnits[missingC] + 1);
                        if (isInEnemyKillZone(missingC, triangles, oppUnits)) {
                            reqMissing = 1;
                        } else if (oppUnits[missingC] == 0) {
                            bool oppNear = false;
                            for (int nb : adj[missingC]) {
                                if (oppUnits[nb] > 0) { oppNear = true; break; }
                            }
                            if (oppNear) reqMissing = 2;
                        }
                        int unitsNeededForMissing = max(1, reqMissing - plannedUnits[missingC]);

                        set<int> reservedUnits;
                        for (int k = 0; k < neededAt[heldIdx1]; ++k) {
                            reservedUnits.insert(surplusAtNode[corners[heldIdx1]][k]);
                        }
                        for (int k = 0; k < neededAt[heldIdx2]; ++k) {
                            reservedUnits.insert(surplusAtNode[corners[heldIdx2]][k]);
                        }

                        vector<pair<int, int>> candSurplus; // {dist, unitIdx}
                        for (int u = 0; u < M; ++u) {
                            if (!unitAssigned[u] && !reservedUnits.count(u)) {
                                int d = distMatrix[unitLocs[u]][missingC];
                                candSurplus.push_back({d, u});
                            }
                        }

                        if ((int)candSurplus.size() < unitsNeededForMissing) continue;

                        sort(candSurplus.begin(), candSurplus.end());

                        int maxD = 0;
                        vector<int> chosenUnits;
                        for (int k = 0; k < unitsNeededForMissing; ++k) {
                            maxD = max(maxD, candSurplus[k].first);
                            chosenUnits.push_back(candSurplus[k].second);
                        }

                        if (maxD > 3) continue;

                        double score = 200000.0;
                        if (maxD <= 1) score += 100000.0;
                        else if (maxD == 2) score += 50000.0;
                        score -= maxD * 2000.0;

                        if (t.owner == 1) score += 30000.0;

                        for (int cTriId : committedTriangles) {
                            int sc = t.sharedCorners(triangles[cTriId]);
                            if (sc == 2) score += 15000.0;
                            else if (sc == 1) score += 5000.0;
                        }

                        if (score > bestScore) {
                            bestScore = score;
                            bestTriId = t.id;
                            bestHeldC1 = corners[heldIdx1];
                            bestHeldC2 = corners[heldIdx2];
                            bestMissingC = missingC;
                            bestNeededAtC1 = neededAt[heldIdx1];
                            bestNeededAtC2 = neededAt[heldIdx2];
                            bestUnitsForMissing = chosenUnits;
                        }
                    }

                    if (bestTriId != -1) {
                        for (int k = 0; k < bestNeededAtC1; ++k) {
                            int su = surplusAtNode[bestHeldC1][k];
                            unitAssigned[su] = true;
                            plannedUnits[bestHeldC1]++;
                        }
                        for (int k = 0; k < bestNeededAtC2; ++k) {
                            int su = surplusAtNode[bestHeldC2][k];
                            unitAssigned[su] = true;
                            plannedUnits[bestHeldC2]++;
                        }

                        for (int su : bestUnitsForMissing) {
                            int from = unitLocs[su];
                            int nextStep = from;
                            if (from != bestMissingC) {
                                int minD = distMatrix[from][bestMissingC];
                                for (int v : adj[from]) {
                                    if (distMatrix[v][bestMissingC] < minD) {
                                        if (plannedUnits[v] >= getNodeCapacity(v) || wouldBeSurrounded(v)) continue;
                                        minD = distMatrix[v][bestMissingC];
                                        nextStep = v;
                                    }
                                }
                                if (nextStep != from) {
                                    moveCount[{from, nextStep}]++;
                                }
                            }
                            plannedUnits[nextStep]++;
                            unitAssigned[su] = true;
                        }

                        committedTriangles.insert(bestTriId);
                        stage4BChanged = true;
                    }
                }

                // STAGE 4C: Coordinated Marching for Remaining Surplus Units
                vector<int> unassignedSurplus = getUnassignedSurplus();
                while (!unassignedSurplus.empty()) {
                    int bestTri = -1;
                    double bestScore = -1e9;
                    vector<int> bestUnitsForTri;
                    vector<int> bestCornersForUnits;

                    for (const auto& t : triangles) {
                        if (!t.meCan && t.owner != 0) continue;
                        if (committedTriangles.count(t.id)) continue;

                        bool unsafeKillZone = false;
                        for (int c : {t.h1, t.h2, t.h3}) {
                            if (isInEnemyKillZone(c, triangles, oppUnits) && oppUnits[c] >= 1) {
                                unsafeKillZone = true;
                                break;
                            }
                        }
                        if (unsafeKillZone) continue;

                        vector<int> needed;
                        for (int c : {t.h1, t.h2, t.h3}) {
                            if (isNodeAutoSurroundedInTri(c, t)) continue;
                            int req = max(1, oppUnits[c] + 1);
                            if (isInEnemyKillZone(c, triangles, oppUnits)) {
                                req = 1;
                            } else if (oppUnits[c] == 0) {
                                bool oppNear = false;
                                for (int nb : adj[c]) {
                                    if (oppUnits[nb] > 0) { oppNear = true; break; }
                                }
                                if (oppNear && unassignedSurplus.size() >= 4) req = 2;
                            }
                            if (plannedUnits[c] < req) {
                                needed.push_back(c);
                            }
                        }

                        if (needed.empty()) continue;

                        int k = min((int)needed.size(), (int)unassignedSurplus.size());
                        int bestMaxD = INF;
                        int bestSumD = INF;
                        vector<int> bestU;
                        vector<int> bestC;

                        if (k == 1) {
                            for (int targetC : needed) {
                                for (int su : unassignedSurplus) {
                                    int d = distMatrix[unitLocs[su]][targetC];
                                    if (d < bestMaxD) {
                                        bestMaxD = d;
                                        bestSumD = d;
                                        bestU = {su};
                                        bestC = {targetC};
                                    }
                                }
                            }
                        } else if (k == 2) {
                            for (size_t c_i = 0; c_i < needed.size(); ++c_i) {
                                for (size_t c_j = c_i + 1; c_j < needed.size(); ++c_j) {
                                    int nc1 = needed[c_i], nc2 = needed[c_j];
                                    for (size_t i = 0; i < unassignedSurplus.size(); ++i) {
                                        for (size_t j = i + 1; j < unassignedSurplus.size(); ++j) {
                                            int u1 = unassignedSurplus[i], u2 = unassignedSurplus[j];
                                            int d1 = max(distMatrix[unitLocs[u1]][nc1], distMatrix[unitLocs[u2]][nc2]);
                                            int s1 = distMatrix[unitLocs[u1]][nc1] + distMatrix[unitLocs[u2]][nc2];
                                            int d2 = max(distMatrix[unitLocs[u1]][nc2], distMatrix[unitLocs[u2]][nc1]);
                                            int s2 = distMatrix[unitLocs[u1]][nc2] + distMatrix[unitLocs[u2]][nc1];

                                            if (d1 < bestMaxD || (d1 == bestMaxD && s1 < bestSumD)) {
                                                bestMaxD = d1;
                                                bestSumD = s1;
                                                bestU = {u1, u2};
                                                bestC = {nc1, nc2};
                                            }
                                            if (d2 < bestMaxD || (d2 == bestMaxD && s2 < bestSumD)) {
                                                bestMaxD = d2;
                                                bestSumD = s2;
                                                bestU = {u2, u1};
                                                bestC = {nc1, nc2};
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (k >= 3) {
                            for (size_t i = 0; i < unassignedSurplus.size(); ++i) {
                                for (size_t j = i + 1; j < unassignedSurplus.size(); ++j) {
                                    for (size_t l = j + 1; l < unassignedSurplus.size(); ++l) {
                                        int candU[3] = {unassignedSurplus[i], unassignedSurplus[j], unassignedSurplus[l]};
                                        int p[3] = {0, 1, 2};
                                        do {
                                            int maxD = 0, sumD = 0;
                                            for (int idx = 0; idx < 3; ++idx) {
                                                int d = distMatrix[unitLocs[candU[p[idx]]]][needed[idx]];
                                                maxD = max(maxD, d);
                                                sumD += d;
                                            }
                                            if (maxD < bestMaxD || (maxD == bestMaxD && sumD < bestSumD)) {
                                                bestMaxD = maxD;
                                                bestSumD = sumD;
                                                bestU = {candU[p[0]], candU[p[1]], candU[p[2]]};
                                                bestC = {needed[0], needed[1], needed[2]};
                                            }
                                        } while (next_permutation(p, p + 3));
                                    }
                                }
                            }
                        }

                        if (bestU.empty() || bestMaxD >= INF) continue;

                        double triValue = 1000.0;
                        if (t.owner == 1) triValue += 3000.0;
                        int sharedCorners = 3 - (int)needed.size();
                        triValue += sharedCorners * 5000.0;

                        for (int cTriId : committedTriangles) {
                            int sc = t.sharedCorners(triangles[cTriId]);
                            if (sc == 2) triValue += 10000.0;
                            else if (sc == 1) triValue += 2000.0;
                        }

                        for (const auto& ot : triangles) {
                            if (ot.id != t.id && t.sharedCorners(ot) == 2 && ot.meCan) {
                                triValue += 200.0;
                            }
                        }

                        double score = triValue / (1.0 + bestMaxD * 2.0 + bestSumD * 0.5);

                        if (score > bestScore) {
                            bestScore = score;
                            bestTri = t.id;
                            bestUnitsForTri = bestU;
                            bestCornersForUnits = bestC;
                        }
                    }

                    if (bestTri == -1) {
                        // STAGE 4D: Active Frontline Advance & Reinforcement
                        for (int su : unassignedSurplus) {
                            int from = unitLocs[su];

                            bool inKillZone = isInEnemyKillZone(from, triangles, oppUnits);
                            if (inKillZone && plannedUnits[from] >= 1) {
                                // Must evacuate! Do not stay!
                            } else {
                                bool neededToHoldCorner = false;
                                if (plannedUnits[from] == 0) {
                                    for (const auto& t : triangles) {
                                        if (t.owner == 0 && t.hasNode(from)) {
                                            neededToHoldCorner = true;
                                            break;
                                        }
                                    }
                                }

                                if (neededToHoldCorner && !wouldBeSurrounded(from)) {
                                    plannedUnits[from]++;
                                    unitAssigned[su] = true;
                                    continue;
                                }
                            }

                            // Otherwise, actively ADVANCE towards opponent units or higher X
                            int minOppD = INF;
                            for (int h = 0; h < houseCount; ++h) {
                                if (oppUnits[h] > 0) minOppD = min(minOppD, distMatrix[from][h]);
                            }

                            int bestV = from;
                            int bestDist = minOppD;
                            for (int v : adj[from]) {
                                if (plannedUnits[v] >= getNodeCapacity(v) || wouldBeSurrounded(v)) continue;
                                int oppD = INF;
                                for (int h = 0; h < houseCount; ++h) {
                                    if (oppUnits[h] > 0) oppD = min(oppD, distMatrix[v][h]);
                                }
                                if (oppD < bestDist) {
                                    bestDist = oppD;
                                    bestV = v;
                                }
                            }

                            if (bestV != from) {
                                moveCount[{from, bestV}]++;
                                plannedUnits[bestV]++;
                            } else {
                                int bestX = pos[from].x;
                                int bestAdvance = from;
                                for (int v : adj[from]) {
                                    if (pos[v].x > bestX && plannedUnits[v] < getNodeCapacity(v) && !wouldBeSurrounded(v)) {
                                        bestX = pos[v].x;
                                        bestAdvance = v;
                                    }
                                }
                                if (bestAdvance != from) {
                                    moveCount[{from, bestAdvance}]++;
                                    plannedUnits[bestAdvance]++;
                                } else {
                                    if (inKillZone && plannedUnits[from] >= 1) {
                                        int safeV = from;
                                        for (int v : adj[from]) {
                                            if (!isInEnemyKillZone(v, triangles, oppUnits) && plannedUnits[v] < getNodeCapacity(v) && !wouldBeSurrounded(v)) {
                                                safeV = v;
                                                break;
                                            }
                                        }
                                        if (safeV != from) {
                                            moveCount[{from, safeV}]++;
                                            plannedUnits[safeV]++;
                                        } else {
                                            plannedUnits[from]++;
                                        }
                                    } else {
                                        plannedUnits[from]++;
                                    }
                                }
                            }
                            unitAssigned[su] = true;
                        }
                        break;
                    }

                    committedTriangles.insert(bestTri);
                    for (size_t idx = 0; idx < bestUnitsForTri.size(); ++idx) {
                        int su = bestUnitsForTri[idx];
                        int from = unitLocs[su];
                        int target = bestCornersForUnits[idx];

                        int nextStep = from;
                        if (from != target) {
                            int minD = distMatrix[from][target];
                            for (int v : adj[from]) {
                                if (distMatrix[v][target] < minD) {
                                    if (plannedUnits[v] >= getNodeCapacity(v) || wouldBeSurrounded(v)) continue;
                                    minD = distMatrix[v][target];
                                    nextStep = v;
                                }
                            }
                            if (nextStep != from) {
                                moveCount[{from, nextStep}]++;
                            }
                        }
                        plannedUnits[nextStep]++;
                        unitAssigned[su] = true;
                    }

                    unassignedSurplus = getUnassignedSurplus();
                }
            }

            // PHASE 5: HARD KILL-ZONE DEFENSE & DISPERSION ENFORCEMENT
            bool kzChanged = true;
            int kzIterations = 0;
            while (kzChanged && kzIterations++ < 10) {
                kzChanged = false;
                for (int u = 0; u < houseCount; ++u) {
                    int maxAllowed = getNodeCapacity(u);

                    int arrivingAtU = 0;
                    for (int v : adj[u]) {
                        if (moveCount.count({v, u})) {
                            arrivingAtU += moveCount[{v, u}];
                        }
                    }
                    int leavingU = 0;
                    for (int v : adj[u]) {
                        if (moveCount.count({u, v})) {
                            leavingU += moveCount[{u, v}];
                        }
                    }
                    int stayingAtU = max(0, myUnits[u] - leavingU);
                    int totalEndingAtU = stayingAtU + arrivingAtU + spawnedUnitsAt[u];

                    if (totalEndingAtU > maxAllowed) {
                        kzChanged = true;

                        for (int v : adj[u]) {
                            if (totalEndingAtU <= maxAllowed) break;
                            if (moveCount.count({v, u}) && moveCount[{v, u}] > 0) {
                                int cancelCnt = min(moveCount[{v, u}], totalEndingAtU - maxAllowed);
                                moveCount[{v, u}] -= cancelCnt;
                                plannedUnits[u] -= cancelCnt;
                                plannedUnits[v] += cancelCnt;
                                totalEndingAtU -= cancelCnt;
                            }
                        }

                        leavingU = 0;
                        for (int v : adj[u]) {
                            if (moveCount.count({u, v})) leavingU += moveCount[{u, v}];
                        }
                        stayingAtU = max(0, myUnits[u] - leavingU);

                        int excess = min(stayingAtU, totalEndingAtU - maxAllowed);
                        if (excess > 0) {
                            int bestSafeNeighbor = -1;
                            int minPlanned = INF;
                            for (int v : adj[u]) {
                                if (!isInEnemyKillZone(v, triangles, oppUnits) && plannedUnits[v] < minPlanned) {
                                    minPlanned = plannedUnits[v];
                                    bestSafeNeighbor = v;
                                }
                            }
                            if (bestSafeNeighbor == -1) {
                                for (int v : adj[u]) {
                                    if (plannedUnits[v] < minPlanned) {
                                        minPlanned = plannedUnits[v];
                                        bestSafeNeighbor = v;
                                    }
                                }
                            }
                            if (bestSafeNeighbor != -1) {
                                moveCount[{u, bestSafeNeighbor}] += excess;
                                plannedUnits[u] -= excess;
                                plannedUnits[bestSafeNeighbor] += excess;
                                totalEndingAtU -= excess;
                            }
                        }
                    }
                }
            }

            // Anti-Surround Evacuation
            for (int u = 0; u < houseCount; ++u) {
                if (plannedUnits[u] > 0 && !adj[u].empty()) {
                    bool surrounded = true;
                    for (int v : adj[u]) {
                        if (plannedUnits[v] >= oppUnits[v]) {
                            surrounded = false;
                            break;
                        }
                    }
                    if (surrounded) {
                        int bestSafeNeighbor = -1;
                        int minOpp = INF;
                        for (int v : adj[u]) {
                            if (!wouldBeSurrounded(v) && oppUnits[v] < minOpp) {
                                minOpp = oppUnits[v];
                                bestSafeNeighbor = v;
                            }
                        }
                        if (bestSafeNeighbor != -1) {
                            cerr << "Anti-Surround rescue: Evacuating house " << u << " -> " << bestSafeNeighbor << "\n";
                            int initialAtU = myUnits[u];
                            int movingOutFromU = 0;
                            for (int v : adj[u]) {
                                if (moveCount.count({u, v})) {
                                    movingOutFromU += moveCount[{u, v}];
                                }
                            }
                            int stayingAtU = max(0, initialAtU - movingOutFromU);
                            if (stayingAtU > 0) {
                                moveCount[{u, bestSafeNeighbor}] += stayingAtU;
                                plannedUnits[u] -= stayingAtU;
                                plannedUnits[bestSafeNeighbor] += stayingAtU;
                            }

                            for (int w : adj[u]) {
                                if (moveCount.count({w, u}) && moveCount[{w, u}] > 0) {
                                    int cnt = moveCount[{w, u}];
                                    moveCount[{w, u}] = 0;
                                    plannedUnits[u] -= cnt;
                                    bool wAdjToSafe = false;
                                    for (int nb : adj[w]) {
                                        if (nb == bestSafeNeighbor) { wAdjToSafe = true; break; }
                                    }
                                    if (wAdjToSafe && w != bestSafeNeighbor) {
                                        moveCount[{w, bestSafeNeighbor}] += cnt;
                                        plannedUnits[bestSafeNeighbor] += cnt;
                                    } else {
                                        plannedUnits[w] += cnt;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (auto const& [edge, cnt] : moveCount) {
                int from = edge.first;
                int to = edge.second;
                if (from != to && cnt > 0) {
                    actions.push_back("MOVE " + to_string(from) + " " + to_string(to) + " " + to_string(cnt));
                }
            }
        }

        if (actions.empty()) {
            actions.push_back("WAIT");
        }

        string cmd = actions[0];
        for (size_t i = 1; i < actions.size(); ++i) {
            cmd += ";" + actions[i];
        }
        cout << cmd << "\n" << flush;
    }

    return 0;
}
