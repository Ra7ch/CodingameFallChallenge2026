#pragma GCC optimize("O3,unroll-loops")
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>

using namespace std;

static constexpr int MAX_N = 64;
static constexpr int MAX_T = 160;
static constexpr int INF_DIST = 99;

struct Point {
    int x, y;
};

struct Tri {
    int id;
    int h1, h2, h3;
    int c[3];
    uint64_t mask;
    int owner;   // -1 neutral, 0 me, 1 opp
    bool meCan;
    bool oppCan;

    bool hasNode(int u) const {
        return h1 == u || h2 == u || h3 == u;
    }
};

struct FastRNG {
    uint64_t s = 0x9e3779b97f4a7c15ULL;
    inline uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    inline int nextInt(int n) {
        return (int)(next() % (uint64_t)n);
    }
    inline double nextDouble() {
        return (next() >> 11) * (1.0 / 9007199254740992.0);
    }
};

// Global board data per turn
int N;
Point pos[MAX_N];
vector<int> adj[MAX_N];
uint64_t adjMask[MAX_N];
int distMat[MAX_N][MAX_N];
int moveOpts[MAX_N][MAX_N];
int numMoveOpts[MAX_N];

int T_cnt;
Tri allTris[MAX_T];

int myUnits[MAX_N];
int oppUnits[MAX_N];
int oppRobustUnits[MAX_N];
int oppReach[MAX_N];
bool armedEnemyAtkZone[MAX_N];
bool potentialEnemyAtkZone[MAX_N];

uint64_t oppHasUnitsMask;
uint64_t oppRobustHasUnitsMask;
uint64_t possibleSelfSurrMask;
uint64_t canBeSurr1StepMask;

int nodePotential[MAX_N];

// Partitioned triangles after pre-move ATTACKs
int S_cnt;
int ownedTriIdx[MAX_T];
bool oppCanThreatenOwned[MAX_T];

int C_cnt;
int capTriIdx[MAX_T];
uint64_t triNbrUnion[MAX_T]; // corners + neighbors of corners

int R_cnt;
int resetTriIdx[MAX_T];

// Moving units
int M_cnt;
int unitLocs[512];

// Turn context
int curTurn = 0;
int myScoreGlobal = 0, oppScoreGlobal = 0;
int totalOppUnitsGlobal = 0;
int oppTriCountGlobal = 0;
int armyTargetGlobal = 40;
int domThreshold = 100;
int marginalSpawnGain[512]; // prefix sum of spawn gains for k = 0..S_cnt

void computeAllPairsBFS() {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) distMat[i][j] = (i == j ? 0 : INF_DIST);
        int q[MAX_N], qh = 0, qt = 0;
        q[qt++] = i;
        while (qh < qt) {
            int u = q[qh++];
            int nd = distMat[i][u] + 1;
            for (int v : adj[u]) {
                if (distMat[i][v] > nd) {
                    distMat[i][v] = nd;
                    q[qt++] = v;
                }
            }
        }
    }
}

bool triHasNeighborNode(const Tri& t, int target) {
    return ((adjMask[t.h1] | adjMask[t.h2] | adjMask[t.h3]) >> target) & 1ULL;
}

void precomputeTurnMetrics() {
    oppHasUnitsMask = 0;
    oppRobustHasUnitsMask = 0;
    totalOppUnitsGlobal = 0;
    oppTriCountGlobal = 0;

    int oppSpawnCapableCnt[MAX_N];
    memset(oppSpawnCapableCnt, 0, sizeof(oppSpawnCapableCnt));
    for (int i = 0; i < T_cnt; ++i) {
        if (allTris[i].owner == 1) {
            oppTriCountGlobal++;
            oppSpawnCapableCnt[allTris[i].h1]++;
            oppSpawnCapableCnt[allTris[i].h2]++;
            oppSpawnCapableCnt[allTris[i].h3]++;
        }
    }

    for (int u = 0; u < N; ++u) {
        totalOppUnitsGlobal += oppUnits[u];
        if (oppUnits[u] > 0) oppHasUnitsMask |= (1ULL << u);
        int reach = oppUnits[u] + oppSpawnCapableCnt[u];
        for (int v : adj[u]) reach += oppUnits[v];
        oppReach[u] = reach;

        // Robust frontline model: if enemy has nearby units that can step/spawn onto u
        int rob = oppUnits[u];
        if (oppUnits[u] == 0 && reach >= 2) rob = 1;
        else if (oppUnits[u] > 0 && reach >= oppUnits[u] + 2) rob = oppUnits[u] + 1;
        oppRobustUnits[u] = rob;
        if (rob > 0) oppRobustHasUnitsMask |= (1ULL << u);
    }

    possibleSelfSurrMask = 0;
    canBeSurr1StepMask = 0;
    for (int u = 0; u < N; ++u) {
        if (adjMask[u] == 0) continue;
        if ((adjMask[u] & ~oppHasUnitsMask) == 0) {
            possibleSelfSurrMask |= (1ULL << u);
        }
        bool allReach = true;
        int sumReach = 0;
        for (int v : adj[u]) {
            if (oppReach[v] == 0) { allReach = false; break; }
            sumReach += oppReach[v];
        }
        if (allReach && sumReach >= (int)adj[u].size() + 1) {
            canBeSurr1StepMask |= (1ULL << u);
        }
    }

    memset(armedEnemyAtkZone, 0, sizeof(armedEnemyAtkZone));
    memset(potentialEnemyAtkZone, 0, sizeof(potentialEnemyAtkZone));
    for (int i = 0; i < T_cnt; ++i) {
        const Tri& t = allTris[i];
        uint64_t zMask = adjMask[t.h1] | adjMask[t.h2] | adjMask[t.h3] | t.mask;
        if (t.owner == 1) {
            if (oppReach[t.h1] >= 1 && oppReach[t.h2] >= 1 && oppReach[t.h3] >= 1) {
                bool armedNow = (oppUnits[t.h1] >= 1 && oppUnits[t.h2] >= 1 && oppUnits[t.h3] >= 1);
                for (int u = 0; u < N; ++u) {
                    if ((zMask >> u) & 1ULL) {
                        if (armedNow) armedEnemyAtkZone[u] = true;
                        else potentialEnemyAtkZone[u] = true;
                    }
                }
            }
        } else if (t.oppCan) {
            if (oppReach[t.h1] >= 1 && oppReach[t.h2] >= 1 && oppReach[t.h3] >= 1 &&
                (oppReach[t.h1] + oppReach[t.h2] + oppReach[t.h3] >= 4)) {
                for (int u = 0; u < N; ++u) {
                    if ((zMask >> u) & 1ULL) potentialEnemyAtkZone[u] = true;
                }
            }
        }
    }

    S_cnt = C_cnt = R_cnt = 0;
    for (int i = 0; i < T_cnt; ++i) {
        const Tri& t = allTris[i];
        triNbrUnion[i] = t.mask | adjMask[t.h1] | adjMask[t.h2] | adjMask[t.h3];
        if (t.owner == 0) {
            int idx = S_cnt++;
            ownedTriIdx[idx] = i;
            oppCanThreatenOwned[idx] = (t.oppCan && oppReach[t.h1] >= 1 && oppReach[t.h2] >= 1 && oppReach[t.h3] >= 1);
        } else if (t.meCan) {
            capTriIdx[C_cnt++] = i;
        } else if (t.owner == -1 && !t.meCan) {
            resetTriIdx[R_cnt++] = i;
        }
    }

    M_cnt = 0;
    for (int u = 0; u < N; ++u) {
        for (int k = 0; k < myUnits[u]; ++k) {
            unitLocs[M_cnt++] = u;
        }
    }

    // Compute domination threshold: exact IEEE-754 match to Java's `cnt >= T_cnt * 0.8`
    domThreshold = 0;
    while ((double)domThreshold < (double)T_cnt * 0.8) domThreshold++;

    // Army target & marginal spawn curve
    int baseCap = max(38, (int)ceil(N * 0.84));
    int matchOpp = totalOppUnitsGlobal + oppTriCountGlobal + 4;
    armyTargetGlobal = clamp(max(baseCap, matchOpp), 38, 48);

    int remTurns = max(1, 200 - curTurn);
    marginalSpawnGain[0] = 0;
    for (int k = 1; k <= S_cnt; ++k) {
        int uAfter = M_cnt + k;
        int gain = 0;
        if (remTurns <= 12) {
            gain = min(1700, remTurns * 140);
        } else if (uAfter <= 16) {
            gain = 6800;
        } else if (uAfter <= 28) {
            gain = 5400;
        } else if (uAfter <= armyTargetGlobal) {
            gain = 2650 + (armyTargetGlobal - uAfter) * 180;
        } else {
            gain = max(450, 1150 - (uAfter - armyTargetGlobal) * 150);
        }
        marginalSpawnGain[k] = marginalSpawnGain[k - 1] + gain;
    }

    // Precompute multi-step nodePotential[u] toward capturable triangles
    static const double decay[12] = {1.0, 0.56, 0.31, 0.17, 0.09, 0.05, 0.025, 0.012, 0.006, 0.003, 0.001, 0.0005};
    for (int u = 0; u < N; ++u) {
        double pot = 0.0;
        for (int ci = 0; ci < C_cnt; ++ci) {
            const Tri& t = allTris[capTriIdx[ci]];
            int d = min({distMat[u][t.h1], distMat[u][t.h2], distMat[u][t.h3]});
            if (d >= 12) continue;
            double attr = (t.owner == 1 ? 260.0 : 190.0);
            // Prefer triangles that already have friendly units nearby
            int nearCorners = 0;
            for (int c : t.c) {
                bool hasNear = (myUnits[c] > oppUnits[c]);
                if (!hasNear) {
                    for (int nb : adj[c]) if (myUnits[nb] > 0) { hasNear = true; break; }
                }
                if (hasNear) nearCorners++;
            }
            attr += nearCorners * 55.0;
            // Slightly discount heavily fortified corners early when our army is small
            int oppStack = oppUnits[t.h1] + oppUnits[t.h2] + oppUnits[t.h3];
            if (oppStack >= 3 && M_cnt < 20) attr *= 0.65;
            pot += attr * decay[d];
        }
        // If no meCan==1 triangles exist at all (rare), attract adjacent to resettable triangles (d == 1, NOT d == 0!)
        if (C_cnt == 0) {
            for (int ri = 0; ri < R_cnt; ++ri) {
                const Tri& t = allTris[resetTriIdx[ri]];
                int d = min({distMat[u][t.h1], distMat[u][t.h2], distMat[u][t.h3]});
                if (d == 1) pot += 150.0;
                else if (d > 1 && d < 10) pot += 150.0 * decay[d];
            }
        }
        nodePotential[u] = (int)llround(pot);
    }
}

// Exact bitmask referee + strategic state evaluator
int64_t evalState(const int* __restrict__ dest, const int* __restrict__ spawnChoice) {
    int u0[MAX_N];
    memset(u0, 0, N * sizeof(int));
    uint64_t myOccMask = 0;

    int stayCount = 0;
    for (int i = 0; i < M_cnt; ++i) {
        int d = dest[i];
        u0[d]++;
        myOccMask |= (1ULL << d);
        if (d == unitLocs[i]) stayCount++;
    }

    int numSpawns = 0;
    for (int j = 0; j < S_cnt; ++j) {
        int sc = spawnChoice[j];
        if (sc >= 0) {
            int spNode = allTris[ownedTriIdx[j]].c[sc];
            u0[spNode]++;
            myOccMask |= (1ULL << spNode);
            numSpawns++;
        }
    }

    // Build initial advantage masks vs static oppUnits
    uint64_t adv0 = 0;
    for (uint64_t m = myOccMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (u0[u] > oppUnits[u]) adv0 |= (1ULL << u);
    }
    uint64_t adv1 = 0;
    for (uint64_t m = oppHasUnitsMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (oppUnits[u] > u0[u]) adv1 |= (1ULL << u);
    }

    // Step 1: surroundNodes() exact simulation
    int killedMyUnits = 0;
    int killedOppUnits = 0;
    uint64_t killedMyMask = 0;
    uint64_t killedOppMask = 0;

    for (uint64_t m = (myOccMask & possibleSelfSurrMask); m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if ((adjMask[u] & ~adv1) == 0) {
            killedMyUnits += u0[u];
            killedMyMask |= (1ULL << u);
        }
    }
    for (uint64_t m = oppHasUnitsMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (adjMask[u] != 0 && (adjMask[u] & ~adv0) == 0) {
            killedOppUnits += oppUnits[u];
            killedOppMask |= (1ULL << u);
        }
    }

    // Post-surround advantage masks
    if (killedMyMask | killedOppMask) {
        adv0 &= ~killedMyMask;
        adv1 &= ~killedOppMask;
        // Note: killing oppUnits[u] -> 0 makes u0[u] > 0 win adv0[u], and killing u0[u] -> 0 makes oppUnits[u] > 0 win adv1[u]
        for (uint64_t m = (killedOppMask & myOccMask & ~killedMyMask); m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (u0[u] > 0) adv0 |= (1ULL << u);
        }
        for (uint64_t m = (killedMyMask & oppHasUnitsMask & ~killedOppMask); m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (oppUnits[u] > 0) adv1 |= (1ULL << u);
        }
    }

    // Also compute robust advantage mask vs oppRobustUnits
    uint64_t adv0_rob = 0, adv1_rob = 0;
    for (uint64_t m = (myOccMask & ~killedMyMask); m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (u0[u] > oppRobustUnits[u]) adv0_rob |= (1ULL << u);
    }
    for (uint64_t m = (oppRobustHasUnitsMask & ~killedOppMask); m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        int u0_eff = ((killedMyMask >> u) & 1ULL) ? 0 : u0[u];
        if (oppRobustUnits[u] > u0_eff) adv1_rob |= (1ULL << u);
    }

    // Step 2: Node ownership bitmasks
    uint64_t owned0 = 0, owned1 = 0, owned0_rob = 0;
    for (int u = 0; u < N; ++u) {
        uint64_t am = adjMask[u];
        bool o0 = false, o1 = false;
        if (am != 0) {
            if ((am & ~adv0) == 0) o0 = true;
            else if ((am & ~adv1) == 0) o1 = true;
        }
        if (!o0 && !o1) {
            o0 = (adv0 >> u) & 1ULL;
            o1 = (adv1 >> u) & 1ULL;
        }
        if (o0) owned0 |= (1ULL << u);
        else if (o1) owned1 |= (1ULL << u);

        bool or0 = false, or1 = false;
        if (am != 0) {
            if ((am & ~adv0_rob) == 0) or0 = true;
            else if ((am & ~adv1_rob) == 0) or1 = true;
        }
        if (!or0 && !or1) {
            or0 = (adv0_rob >> u) & 1ULL;
        }
        if (or0) owned0_rob |= (1ULL << u);
    }

    int64_t score = 0;
    score += marginalSpawnGain[numSpawns];
    score += (int64_t)killedOppUnits * 3400LL;
    score -= (int64_t)killedMyUnits * 5500LL;

    int totalOwnedEnd = 0;
    int safeOwnedEnd = 0;

    // Evaluate owned triangles (HOLD vs SPAWN)
    for (int j = 0; j < S_cnt; ++j) {
        const Tri& t = allTris[ownedTriIdx[j]];
        if (spawnChoice[j] == -1) {
            // Held! Kept unless opponent captures all 3 corners
            bool oppCapsNow = t.oppCan && ((owned1 & t.mask) == t.mask);
            if (!oppCapsNow) {
                totalOwnedEnd++;
                score += 2200;
                bool oppThreat = oppCanThreatenOwned[j] && ((owned0_rob & t.mask) == 0);
                if (!oppThreat) {
                    safeOwnedEnd++;
                } else {
                    score -= 950; // Vulnerable frontline hold: prefer cashing out via SPAWN or defending!
                }
            }
        } else {
            // Spawned from t: becomes neutral meCan=0 at end of turn.
            // Encourage keeping the other 2 corners empty (or stacking spawns on shared hub) for 1-turn reset!
            int occ = (u0[t.h1] > 0) + (u0[t.h2] > 0) + (u0[t.h3] > 0);
            score -= (occ - 1) * 140;
        }
    }

    // Evaluate capturable triangles (meCan == 1 && owner != 0)
    for (int ci = 0; ci < C_cnt; ++ci) {
        int tid = capTriIdx[ci];
        const Tri& t = allTris[tid];
        uint64_t m = owned0 & t.mask;
        if (m == t.mask) {
            totalOwnedEnd++;
            score += 3900; // 2200 hold + 1700 fresh factory bonus
            if (t.owner == 1) score += 1500; // Steal from opponent!
            if ((owned0_rob & t.mask) == t.mask) {
                safeOwnedEnd++;
                score += 550;
            }
        } else {
            int cnt = __builtin_popcountll(m);
            if (cnt == 2) {
                score += 240;
                int miss = __builtin_ctzll(t.mask & ~m);
                if (adjMask[miss] & myOccMask) score += 220; // 3rd unit is 1 step away!
            } else if (cnt == 1) {
                score += 55;
            }
        }
    }

    // Evaluate neutral meCan == 0 triangles (Triangle Farming Engine!)
    for (int ri = 0; ri < R_cnt; ++ri) {
        int tid = resetTriIdx[ri];
        const Tri& t = allTris[tid];
        // Note: updateAllowedCaptures runs BEFORE surroundNodes, using pre-surround u0!
        if ((myOccMask & t.mask) == 0) {
            // ALL 3 CORNERS VACATED -> meCan resets to 1 at end of this turn!
            if (myOccMask & triNbrUnion[tid]) {
                score += 1850; // Unit poised 1 step away to recapture immediately next turn!
            } else {
                score += 1200;
            }
        } else {
            int occ = (u0[t.h1] > 0) + (u0[t.h2] > 0) + (u0[t.h3] > 0);
            score -= occ * 220;
        }
    }

    // Check Instant 80% Domination Win & Elimination Avoidance
    int totalEndUnits = M_cnt + numSpawns - killedMyUnits;
    if (totalEndUnits == 0 && totalOwnedEnd == 0) return -500000000LL;
    if (totalEndUnits == 1 && totalOwnedEnd == 0 && myScoreGlobal < oppScoreGlobal) return -250000000LL;

    if (safeOwnedEnd >= domThreshold) {
        return 200000000LL + (int64_t)safeOwnedEnd * 50000LL + (int64_t)totalOwnedEnd * 10000LL + totalEndUnits * 100LL;
    }
    if (totalOwnedEnd >= domThreshold) {
        score += 80000000LL + (int64_t)safeOwnedEnd * 50000LL + (int64_t)totalOwnedEnd * 10000LL;
    }

    // Evaluate occupied nodes: potential, dispersion, kill-zone discipline, 1-step surround danger
    uint64_t aliveOccMask = myOccMask & ~killedMyMask;
    for (uint64_t m = aliveOccMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        int cnt = u0[u];
        int need = oppUnits[u] + 1;
        int useful = min(cnt, need);
        int extra = cnt - useful;

        score += (int64_t)useful * nodePotential[u];
        if (extra > 0) {
            score += ((int64_t)extra * nodePotential[u]) >> 2;
            score -= (int64_t)extra * extra * 85LL;
        }

        // Enemy ATTACK kill-zone discipline (for start of Turn T+1!)
        if (cnt >= 3) {
            if (armedEnemyAtkZone[u]) {
                score -= (cnt == 3 ? 550LL : 550LL + (int64_t)(cnt - 3) * 2400LL);
            } else if (potentialEnemyAtkZone[u]) {
                score -= (cnt == 3 ? 220LL : 220LL + (int64_t)(cnt - 3) * 1250LL);
            }
        }

        // 1-step dynamic enemy surround trap caution
        if ((canBeSurr1StepMask >> u) & 1ULL) {
            // Safe if we control at least one neighbor v with u0[v] >= oppReach[v]
            bool hasSafeEscape = false;
            for (int v : adj[u]) {
                if (u0[v] >= oppReach[v] || (u0[v] >= oppUnits[v] + 1 && oppReach[v] <= 2)) {
                    hasSafeEscape = true;
                    break;
                }
            }
            if (!hasSafeEscape) {
                score -= (int64_t)cnt * 650LL;
            }
        }
    }

    // Opening 2-turn exact triangle convergence bonus on Turn 1-2 when 0 triangles are owned yet
    if (curTurn <= 2 && totalOwnedEnd == 0 && totalEndUnits == 3 && C_cnt > 0) {
        int occNodes[3], nOcc = 0;
        for (uint64_t m = aliveOccMask; m && nOcc < 3; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            for (int k = 0; k < u0[u] && nOcc < 3; ++k) occNodes[nOcc++] = u;
        }
        if (nOcc == 3) {
            static constexpr int perms[6][3] = {
                {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}
            };
            int bestMetric = 999999;
            for (int ci = 0; ci < C_cnt; ++ci) {
                const Tri& t = allTris[capTriIdx[ci]];
                if (distMat[occNodes[0]][t.h1] > 5 && distMat[occNodes[1]][t.h1] > 5) continue;
                for (int p = 0; p < 6; ++p) {
                    int d1 = distMat[occNodes[perms[p][0]]][t.h1];
                    int d2 = distMat[occNodes[perms[p][1]]][t.h2];
                    int d3 = distMat[occNodes[perms[p][2]]][t.h3];
                    int mx = max({d1, d2, d3});
                    int sm = d1 + d2 + d3;
                    int met = mx * 100 + sm * 25;
                    if (met < bestMetric) bestMetric = met;
                }
            }
            if (bestMetric < 999999) {
                score += (1500 - bestMetric * 4);
            }
        }
    }

    score += stayCount * 3;
    return score;
}

// Exhaustive solver when state space is small (Turn 1, Turn 2, small battles)
struct ExactSolver {
    int curDest[64];
    int curSpawn[64];
    int bestDest[64];
    int bestSpawn[64];
    int64_t bestScore = -1000000000000000LL;
    chrono::high_resolution_clock::time_point startTp;
    double limitMs = 23.0;
    int leaves = 0;
    bool timedOut = false;

    void dfsSpawn(int sj) {
        if (timedOut) return;
        if (sj == S_cnt) {
            if ((++leaves & 255) == 0) {
                double el = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - startTp).count();
                if (el >= limitMs) { timedOut = true; return; }
            }
            int64_t sc = evalState(curDest, curSpawn);
            if (sc > bestScore) {
                bestScore = sc;
                memcpy(bestDest, curDest, M_cnt * sizeof(int));
                memcpy(bestSpawn, curSpawn, S_cnt * sizeof(int));
            }
            return;
        }
        for (int opt = -1; opt < 3; ++opt) {
            curSpawn[sj] = opt;
            dfsSpawn(sj + 1);
            if (timedOut) return;
        }
    }

    void dfsMove(int ui) {
        if (timedOut) return;
        if (ui == M_cnt) {
            dfsSpawn(0);
            return;
        }
        int u = unitLocs[ui];
        int cnt = numMoveOpts[u];
        for (int k = 0; k < cnt; ++k) {
            curDest[ui] = moveOpts[u][k];
            dfsMove(ui + 1);
            if (timedOut) return;
        }
    }
};

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    if (!(cin >> N)) return 0;
    for (int i = 0; i < N; ++i) {
        int hid, hx, hy;
        cin >> hid >> hx >> hy;
        pos[hid] = {hx, hy};
    }

    FastRNG rng;

    while (cin >> myScoreGlobal >> oppScoreGlobal) {
        auto turnStart = chrono::high_resolution_clock::now();
        curTurn++;

        int totalMyUnitsInit = 0;
        for (int i = 0; i < N; ++i) {
            int hid, uMe, uOpp;
            cin >> hid >> uMe >> uOpp;
            myUnits[hid] = uMe;
            oppUnits[hid] = uOpp;
            totalMyUnitsInit += uMe;
        }

        int pathCount;
        cin >> pathCount;
        for (int i = 0; i < N; ++i) {
            adj[i].clear();
            adjMask[i] = 0;
        }
        for (int i = 0; i < pathCount; ++i) {
            int h1, h2;
            cin >> h1 >> h2;
            adj[h1].push_back(h2);
            adj[h2].push_back(h1);
            adjMask[h1] |= (1ULL << h2);
            adjMask[h2] |= (1ULL << h1);
        }
        for (int u = 0; u < N; ++u) {
            moveOpts[u][0] = u;
            int cnt = 1;
            for (int v : adj[u]) moveOpts[u][cnt++] = v;
            numMoveOpts[u] = cnt;
        }

        computeAllPairsBFS();

        cin >> T_cnt;
        int initOppTris = 0;
        for (int i = 0; i < T_cnt; ++i) {
            int h1, h2, h3, ow, mc, oc;
            cin >> h1 >> h2 >> h3 >> ow >> mc >> oc;
            allTris[i].id = i;
            allTris[i].h1 = h1; allTris[i].h2 = h2; allTris[i].h3 = h3;
            allTris[i].c[0] = h1; allTris[i].c[1] = h2; allTris[i].c[2] = h3;
            allTris[i].mask = (1ULL << h1) | (1ULL << h2) | (1ULL << h3);
            allTris[i].owner = ow;
            allTris[i].meCan = (mc != 0);
            allTris[i].oppCan = (oc != 0);
            if (ow == 1) initOppTris++;
        }

        int linkableCount;
        cin >> linkableCount;
        for (int i = 0; i < linkableCount; ++i) {
            int h1, h2;
            cin >> h1 >> h2;
        }

        vector<string> actions;

        // ====================================================================
        // PHASE 0: TACTICAL PRE-MOVE ATTACKS (Priority 0 in Referee!)
        // Guaranteed hit on oppUnits[target] before opponent's MOVE!
        // ====================================================================
        int curMyTotal = totalMyUnitsInit;
        int curOppTotal = 0;
        for (int u = 0; u < N; ++u) curOppTotal += oppUnits[u];

        while (true) {
            int bestTri = -1, bestTarget = -1, bestVal = 0;
            for (int i = 0; i < T_cnt; ++i) {
                const Tri& t = allTris[i];
                if (t.owner != 0) continue;
                if (myUnits[t.h1] < 1 || myUnits[t.h2] < 1 || myUnits[t.h3] < 1) continue;

                for (int u = 0; u < N; ++u) {
                    int oppU = oppUnits[u];
                    if (oppU < 2) continue;
                    if (!triHasNeighborNode(t, u)) continue;

                    // Count how many enemy-owned triangles rely on u
                    int oppTrisOnU = 0;
                    for (int k = 0; k < T_cnt; ++k) {
                        if (allTris[k].owner == 1 && allTris[k].hasNode(u)) oppTrisOnU++;
                    }

                    bool worthIt = false;
                    if (oppU >= 5 && curMyTotal >= 8) worthIt = true;
                    else if (oppU >= 4 && (curMyTotal >= 12 || curMyTotal > curOppTotal)) worthIt = true;
                    else if (oppU == 3 && (curMyTotal >= 28 || curMyTotal >= curOppTotal + 8 ||
                             (initOppTris >= (int)(T_cnt * 0.65) && oppTrisOnU >= 2))) worthIt = true;
                    else if (oppU == 2 && curMyTotal >= 40 && curMyTotal >= curOppTotal + 14 && oppTrisOnU >= 2) worthIt = true;

                    if (worthIt) {
                        int val = oppU * 100 + oppTrisOnU * 35;
                        // Bonus if attacking leaves 0 units on t so t.meCan resets to 1 immediately!
                        if (myUnits[t.h1] == 1 && myUnits[t.h2] == 1 && myUnits[t.h3] == 1 && !t.hasNode(u)) {
                            val += 25;
                        }
                        if (val > bestVal) {
                            bestVal = val;
                            bestTri = i;
                            bestTarget = u;
                        }
                    }
                }
            }

            if (bestTri == -1) break;
            Tri& bt = allTris[bestTri];
            actions.push_back("ATTACK " + to_string(bt.h1) + " " + to_string(bt.h2) + " " + to_string(bt.h3) + " " + to_string(bestTarget));
            myUnits[bt.h1]--;
            myUnits[bt.h2]--;
            myUnits[bt.h3]--;
            curMyTotal -= 3;
            curOppTotal -= oppUnits[bestTarget];
            oppUnits[bestTarget] = 0;
            bt.owner = -1;
            bt.meCan = false;
        }

        // Precompute bitmasks, potentials, and partitioned triangle lists
        precomputeTurnMetrics();

        // Check total state space size for Exact vs Simulated Annealing
        double comb = 1.0;
        for (int i = 0; i < M_cnt; ++i) {
            comb *= numMoveOpts[unitLocs[i]];
            if (comb > 1e9) break;
        }
        for (int j = 0; j < S_cnt; ++j) {
            comb *= 4.0;
            if (comb > 1e9) break;
        }

        vector<int> bestDest(M_cnt, 0);
        vector<int> bestSpawn(S_cnt, -1);
        int64_t bestScore = -1000000000000000LL;

        double maxTimeMs = (curTurn == 1 ? 300.0 : 23.0);

        if (comb <= 5000.0 && M_cnt < 64 && S_cnt < 64) {
            ExactSolver ex;
            ex.startTp = turnStart;
            ex.limitMs = maxTimeMs;
            ex.dfsMove(0);
            bestScore = ex.bestScore;
            for (int i = 0; i < M_cnt; ++i) bestDest[i] = ex.bestDest[i];
            for (int j = 0; j < S_cnt; ++j) bestSpawn[j] = ex.bestSpawn[j];
        } else {
            // ================================================================
            // SMART SEED + MULTI-OPERATOR SIMULATED ANNEALING + GREEDY POLISH
            // ================================================================
            vector<int> curDest(M_cnt);
            vector<int> curSpawn(S_cnt, -1);

            for (int i = 0; i < M_cnt; ++i) curDest[i] = unitLocs[i];
            for (int j = 0; j < S_cnt; ++j) {
                if (M_cnt + j < armyTargetGlobal) {
                    const Tri& t = allTris[ownedTriIdx[j]];
                    int bestC = 0, bestP = -999999;
                    for (int k = 0; k < 3; ++k) {
                        int c = t.c[k];
                        int p = nodePotential[c] - (armedEnemyAtkZone[c] ? 800 : 0);
                        if (p > bestP) { bestP = p; bestC = k; }
                    }
                    curSpawn[j] = bestC;
                } else {
                    curSpawn[j] = -1;
                }
            }

            int64_t curScore = evalState(curDest.data(), curSpawn.data());
            bestScore = curScore;
            bestDest = curDest;
            bestSpawn = curSpawn;

            // 2 Quick Coordinate-Ascent Passes to warm-start SA
            for (int pass = 0; pass < 2; ++pass) {
                for (int i = 0; i < M_cnt; ++i) {
                    int u = unitLocs[i];
                    int orig = curDest[i];
                    int bestOpt = orig;
                    for (int k = 0; k < numMoveOpts[u]; ++k) {
                        int cand = moveOpts[u][k];
                        if (cand == orig) continue;
                        curDest[i] = cand;
                        int64_t sc = evalState(curDest.data(), curSpawn.data());
                        if (sc > curScore) { curScore = sc; bestOpt = cand; }
                    }
                    curDest[i] = bestOpt;
                }
                for (int j = 0; j < S_cnt; ++j) {
                    int orig = curSpawn[j];
                    int bestOpt = orig;
                    for (int cand = -1; cand < 3; ++cand) {
                        if (cand == orig) continue;
                        curSpawn[j] = cand;
                        int64_t sc = evalState(curDest.data(), curSpawn.data());
                        if (sc > curScore) { curScore = sc; bestOpt = cand; }
                    }
                    curSpawn[j] = bestOpt;
                }
            }
            if (curScore > bestScore) {
                bestScore = curScore;
                bestDest = curDest;
                bestSpawn = curSpawn;
            }

            // Simulated Annealing loop
            const double T_start = 2600.0;
            const double T_end = 10.0;
            double temp = T_start;
            double saBudgetMs = maxTimeMs - 3.0; // Reserve final 3 ms for deterministic polish
            int iter = 0;

            int savedIdx[16], savedVal[16], nSaved = 0;
            int savedSpIdx[16], savedSpVal[16], nSavedSp = 0;

            while (true) {
                if ((iter & 63) == 0) {
                    double elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                    if (elapsed >= saBudgetMs) break;
                    double prog = elapsed / saBudgetMs;
                    if (prog < 0.85) {
                        double r = prog / 0.85;
                        temp = T_start * pow(T_end / T_start, r);
                    } else {
                        temp = 0.0; // Pure hill-climbing in last 15%
                    }
                }
                iter++;

                nSaved = 0;
                nSavedSp = 0;
                int roll = rng.nextInt(100);

                if (roll < 45 && M_cnt > 0) {
                    // Op 1: Single unit move
                    int i = rng.nextInt(M_cnt);
                    int u = unitLocs[i];
                    int cnt = numMoveOpts[u];
                    if (cnt <= 1) continue;
                    int cand = moveOpts[u][rng.nextInt(cnt)];
                    if (cand == curDest[i]) cand = moveOpts[u][(rng.nextInt(cnt - 1) + 1) % cnt];
                    if (cand == curDest[i]) continue;
                    savedIdx[nSaved] = i; savedVal[nSaved] = curDest[i]; nSaved++;
                    curDest[i] = cand;
                } else if (roll < 65 && S_cnt > 0) {
                    // Op 2: Single spawn change
                    int j = rng.nextInt(S_cnt);
                    int cand = rng.nextInt(4) - 1;
                    if (cand == curSpawn[j]) continue;
                    savedSpIdx[nSavedSp] = j; savedSpVal[nSavedSp] = curSpawn[j]; nSavedSp++;
                    curSpawn[j] = cand;
                } else if (roll < 86 && C_cnt > 0 && (M_cnt + S_cnt) > 0) {
                    // Op 3: Exact 3-Corner Relay / Slot Matcher Macro-Move!
                    const Tri& t = allTris[capTriIdx[rng.nextInt(C_cnt)]];
                    bool lockU[512];
                    bool lockS[MAX_T];
                    memset(lockU, 0, M_cnt * sizeof(bool));
                    memset(lockS, 0, S_cnt * sizeof(bool));

                    // Order corners from fewest reachable sources to most reachable sources!
                    int ord[3] = {0, 1, 2};
                    int reachCnt[3] = {0, 0, 0};
                    for (int k = 0; k < 3; ++k) {
                        int c = t.c[k];
                        for (int i = 0; i < M_cnt; ++i) {
                            int u = unitLocs[i];
                            if (u == c || ((adjMask[u] >> c) & 1ULL)) reachCnt[k]++;
                        }
                        for (int j = 0; j < S_cnt; ++j) {
                            if (allTris[ownedTriIdx[j]].hasNode(c)) reachCnt[k]++;
                        }
                    }
                    if (reachCnt[ord[1]] < reachCnt[ord[0]]) swap(ord[0], ord[1]);
                    if (reachCnt[ord[2]] < reachCnt[ord[1]]) swap(ord[1], ord[2]);
                    if (reachCnt[ord[1]] < reachCnt[ord[0]]) swap(ord[0], ord[1]);

                    bool reinforce = (rng.nextInt(3) == 0);
                    for (int idx = 0; idx < 3; ++idx) {
                        int corner = t.c[ord[idx]];
                        int req = (reinforce ? oppRobustUnits[corner] : oppUnits[corner]) + 1;
                        int assigned = 0;

                        // First, lock up to `req` unlocked units/spawns ALREADY bound for `corner`
                        for (int i = 0; i < M_cnt && assigned < req; ++i) {
                            if (!lockU[i] && curDest[i] == corner) {
                                lockU[i] = true;
                                assigned++;
                            }
                        }
                        for (int j = 0; j < S_cnt && assigned < req; ++j) {
                            if (!lockS[j] && curSpawn[j] >= 0 && allTris[ownedTriIdx[j]].c[curSpawn[j]] == corner) {
                                lockS[j] = true;
                                assigned++;
                            }
                        }

                        // Next, pull unlocked units (including surplus/relay units on other corners of t!) or spawns
                        while (assigned < req) {
                            bool found = false;
                            if (M_cnt > 0) {
                                int startOff = rng.nextInt(M_cnt);
                                for (int step = 0; step < M_cnt; ++step) {
                                    int i = (startOff + step) % M_cnt;
                                    if (lockU[i]) continue;
                                    int u = unitLocs[i];
                                    if (u == corner || ((adjMask[u] >> corner) & 1ULL)) {
                                        if (nSaved < 15) {
                                            if (curDest[i] != corner) {
                                                savedIdx[nSaved] = i; savedVal[nSaved] = curDest[i]; nSaved++;
                                                curDest[i] = corner;
                                            }
                                            lockU[i] = true;
                                            assigned++;
                                            found = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!found && S_cnt > 0) {
                                int startS = rng.nextInt(S_cnt);
                                for (int step = 0; step < S_cnt; ++step) {
                                    int j = (startS + step) % S_cnt;
                                    if (lockS[j]) continue;
                                    const Tri& ot = allTris[ownedTriIdx[j]];
                                    for (int sc = 0; sc < 3; ++sc) {
                                        if (ot.c[sc] == corner && nSavedSp < 15) {
                                            if (curSpawn[j] != sc) {
                                                savedSpIdx[nSavedSp] = j; savedSpVal[nSavedSp] = curSpawn[j]; nSavedSp++;
                                                curSpawn[j] = sc;
                                            }
                                            lockS[j] = true;
                                            assigned++;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (found) break;
                                }
                            }
                            if (!found) break;
                        }
                    }
                    if (nSaved == 0 && nSavedSp == 0) continue;
                } else if (roll < 94 && R_cnt > 0) {
                    // Op 4: Coordinated meCan == 0 Triangle Evacuation Macro-Move!
                    const Tri& t = allTris[resetTriIdx[rng.nextInt(R_cnt)]];
                    for (int i = 0; i < M_cnt && nSaved < 15; ++i) {
                        if (t.hasNode(curDest[i])) {
                            int u = unitLocs[i];
                            // Pick an option for u outside t
                            int outs[MAX_N], nOut = 0;
                            for (int k = 0; k < numMoveOpts[u]; ++k) {
                                int cand = moveOpts[u][k];
                                if (!t.hasNode(cand)) outs[nOut++] = cand;
                            }
                            if (nOut > 0) {
                                savedIdx[nSaved] = i; savedVal[nSaved] = curDest[i]; nSaved++;
                                curDest[i] = outs[rng.nextInt(nOut)];
                            }
                        }
                    }
                    for (int j = 0; j < S_cnt && nSavedSp < 15; ++j) {
                        int sc = curSpawn[j];
                        if (sc >= 0 && t.hasNode(allTris[ownedTriIdx[j]].c[sc])) {
                            const Tri& ot = allTris[ownedTriIdx[j]];
                            int outs[4], nOut = 0;
                            for (int k = 0; k < 3; ++k) if (!t.hasNode(ot.c[k])) outs[nOut++] = k;
                            if (nOut == 0) outs[nOut++] = -1;
                            savedSpIdx[nSavedSp] = j; savedSpVal[nSavedSp] = sc; nSavedSp++;
                            curSpawn[j] = outs[rng.nextInt(nOut)];
                        }
                    }
                    if (nSaved == 0 && nSavedSp == 0) continue;
                } else if (M_cnt >= 2) {
                    // Op 5: 2-Unit Conveyor / Relay Move
                    int i1 = rng.nextInt(M_cnt);
                    int u1 = unitLocs[i1];
                    if (numMoveOpts[u1] <= 1) continue;
                    int d1 = moveOpts[u1][1 + rng.nextInt(numMoveOpts[u1] - 1)];
                    if (d1 == curDest[i1]) continue;
                    savedIdx[nSaved] = i1; savedVal[nSaved] = curDest[i1]; nSaved++;
                    curDest[i1] = d1;

                    // Push a stationary unit at d1 forward simultaneously
                    for (int i2 = 0; i2 < M_cnt; ++i2) {
                        if (i2 != i1 && unitLocs[i2] == d1 && curDest[i2] == d1 && numMoveOpts[d1] > 1) {
                            savedIdx[nSaved] = i2; savedVal[nSaved] = curDest[i2]; nSaved++;
                            curDest[i2] = moveOpts[d1][1 + rng.nextInt(numMoveOpts[d1] - 1)];
                            break;
                        }
                    }
                } else {
                    continue;
                }

                int64_t newScore = evalState(curDest.data(), curSpawn.data());
                int64_t diff = newScore - curScore;

                bool accept = false;
                if (diff >= 0) {
                    accept = true;
                } else if (temp > 0.5) {
                    if (diff > -temp * 12.0 && exp((double)diff / temp) > rng.nextDouble()) {
                        accept = true;
                    }
                }

                if (accept) {
                    curScore = newScore;
                    if (newScore > bestScore) {
                        bestScore = newScore;
                        bestDest = curDest;
                        bestSpawn = curSpawn;
                    }
                } else {
                    for (int k = nSaved - 1; k >= 0; --k) curDest[savedIdx[k]] = savedVal[k];
                    for (int k = nSavedSp - 1; k >= 0; --k) curSpawn[savedSpIdx[k]] = savedSpVal[k];
                }
            }

            // Final deterministic greedy polish starting from bestDest / bestSpawn
            curDest = bestDest;
            curSpawn = bestSpawn;
            curScore = bestScore;
            for (int i = 0; i < M_cnt; ++i) {
                double elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                if (elapsed >= maxTimeMs) break;
                int u = unitLocs[i];
                int orig = curDest[i];
                int bestOpt = orig;
                for (int k = 0; k < numMoveOpts[u]; ++k) {
                    int cand = moveOpts[u][k];
                    if (cand == orig) continue;
                    curDest[i] = cand;
                    int64_t sc = evalState(curDest.data(), curSpawn.data());
                    if (sc > curScore) { curScore = sc; bestOpt = cand; }
                }
                curDest[i] = bestOpt;
            }
            for (int j = 0; j < S_cnt; ++j) {
                double elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                if (elapsed >= maxTimeMs) break;
                int orig = curSpawn[j];
                int bestOpt = orig;
                for (int cand = -1; cand < 3; ++cand) {
                    if (cand == orig) continue;
                    curSpawn[j] = cand;
                    int64_t sc = evalState(curDest.data(), curSpawn.data());
                    if (sc > curScore) { curScore = sc; bestOpt = cand; }
                }
                curSpawn[j] = bestOpt;
            }
            bestDest = curDest;
            bestSpawn = curSpawn;
            bestScore = curScore;
        }

        // ====================================================================
        // FORMAT OUTPUT COMMANDS
        // ====================================================================
        // Aggregate MOVE commands by (from, to)
        int moveCount[MAX_N][MAX_N];
        memset(moveCount, 0, sizeof(moveCount));
        for (int i = 0; i < M_cnt; ++i) {
            int fr = unitLocs[i], to = bestDest[i];
            if (fr != to) moveCount[fr][to]++;
        }
        for (int fr = 0; fr < N; ++fr) {
            for (int v : adj[fr]) {
                if (moveCount[fr][v] > 0) {
                    actions.push_back("MOVE " + to_string(fr) + " " + to_string(v) + " " + to_string(moveCount[fr][v]));
                }
            }
        }

        // Emit SPAWN commands
        int spawnsUsed = 0;
        for (int j = 0; j < S_cnt; ++j) {
            int sc = bestSpawn[j];
            if (sc >= 0) {
                const Tri& t = allTris[ownedTriIdx[j]];
                int sp = t.c[sc];
                int c2 = t.c[(sc + 1) % 3];
                int c3 = t.c[(sc + 2) % 3];
                actions.push_back("SPAWN " + to_string(sp) + " " + to_string(c2) + " " + to_string(c3));
                spawnsUsed++;
            }
        }

        double usedMs = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
        cerr << "T" << curTurn << " U:" << M_cnt << "+" << spawnsUsed << " vs " << curOppTotal
             << " | Tri:" << S_cnt << "-" << initOppTris
             << " | eval:" << bestScore << " | " << usedMs << "ms\n";

        if (actions.empty()) {
            cout << "WAIT\n";
        } else {
            for (size_t i = 0; i < actions.size(); ++i) {
                if (i > 0) cout << "; ";
                cout << actions[i];
            }
            cout << "\n";
        }
        cout.flush();
    }
    return 0;
}
