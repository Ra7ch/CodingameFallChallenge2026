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
static constexpr int MAX_T = 256;
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
vector<int> initAdj[MAX_N];
uint64_t initAdjMask[MAX_N];
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
int oppPredUnits[MAX_N];
int oppReach[MAX_N];
bool armedEnemyAtkZone[MAX_N];
bool potentialEnemyAtkZone[MAX_N];

struct EnemyAtkTri {
    int c0, c1, c2;
    uint64_t mask;
    uint64_t zMask;
    bool ownedNow;
    bool armedNow;
};
int E_cnt;
EnemyAtkTri eatkTris[MAX_T];

uint64_t oppHasUnitsMask;
uint64_t oppRobustHasUnitsMask;
uint64_t oppPredHasUnitsMask;
uint64_t possibleSelfSurrMask;
uint64_t canBeSurr1StepMask;
uint64_t pureEarMask;
uint64_t degLe2Mask;
uint64_t interiorOwnedMask;

int nodePotential[MAX_N];
int minCapDist[MAX_N];

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
bool oppHoldMode = false;
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

inline int dist2Nodes(int a, int b) {
    int dx = pos[a].x - pos[b].x;
    int dy = pos[a].y - pos[b].y;
    return dx * dx + dy * dy;
}

inline bool ccwNodes(int a, int b, int c) {
    return (int64_t)(pos[c].y - pos[a].y) * (pos[b].x - pos[a].x) >
           (int64_t)(pos[b].y - pos[a].y) * (pos[c].x - pos[a].x);
}

inline bool edgesCross(int a1, int a2, int b1, int b2) {
    if (a1 == a2) return false;
    if (a1 == b1 || a2 == b1 || a1 == b2 || a2 == b2) return false;
    return ccwNodes(a1, b1, b2) != ccwNodes(a2, b1, b2) &&
           ccwNodes(a1, a2, b1) != ccwNodes(a1, a2, b2);
}

inline bool nodeInsideTri(int m, int a, int b, int c) {
    if (m == a || m == b || m == c) return false;
    bool c1 = ccwNodes(m, a, b);
    bool c2 = ccwNodes(m, b, c);
    bool c3 = ccwNodes(m, c, a);
    bool hasNeg = !c1 || !c2 || !c3;
    bool hasPos = c1 || c2 || c3;
    return !(hasNeg && hasPos);
}

bool canAddEdgeNow(int u, int v) {
    if (u < 0 || v < 0 || u >= N || v >= N || u == v) return false;
    if ((adjMask[u] >> v) & 1ULL) return false;
    if (dist2Nodes(u, v) > 400 * 400) return false;
    for (int x = 0; x < N; ++x) {
        for (int y : adj[x]) {
            if (y <= x) continue;
            if (edgesCross(u, v, x, y)) return false;
        }
    }
    return true;
}

void addGraphEdge(int u, int v) {
    if (!((adjMask[u] >> v) & 1ULL)) {
        adj[u].push_back(v);
        sort(adj[u].begin(), adj[u].end());
        adj[v].push_back(u);
        sort(adj[v].begin(), adj[v].end());
        adjMask[u] |= (1ULL << v);
        adjMask[v] |= (1ULL << u);
    }
}

void removeGraphEdge(int u, int v) {
    if ((adjMask[u] >> v) & 1ULL) {
        adj[u].erase(remove(adj[u].begin(), adj[u].end(), v), adj[u].end());
        adj[v].erase(remove(adj[v].begin(), adj[v].end(), u), adj[v].end());
        adjMask[u] &= ~(1ULL << v);
        adjMask[v] &= ~(1ULL << u);
    }
}

void rebuildTrianglesInBot() {
    int oldCnt = T_cnt;
    Tri oldTris[MAX_T];
    for (int i = 0; i < oldCnt; ++i) oldTris[i] = allTris[i];

    T_cnt = 0;
    for (int i = 0; i < N; ++i) {
        for (int j : adj[i]) {
            if (j <= i) continue;
            uint64_t common = adjMask[i] & adjMask[j];
            for (uint64_t m = common; m; m &= (m - 1)) {
                int k = __builtin_ctzll(m);
                if (k <= j) continue;
                bool anyIn = false;
                for (int nd = 0; nd < N; ++nd) {
                    if (nodeInsideTri(nd, i, j, k)) { anyIn = true; break; }
                }
                if (!anyIn && T_cnt < MAX_T) {
                    Tri nt;
                    nt.id = T_cnt;
                    nt.h1 = i; nt.h2 = j; nt.h3 = k;
                    nt.c[0] = i; nt.c[1] = j; nt.c[2] = k;
                    nt.mask = (1ULL << i) | (1ULL << j) | (1ULL << k);
                    nt.owner = -1;
                    nt.meCan = true;
                    nt.oppCan = true;
                    for (int oi = 0; oi < oldCnt; ++oi) {
                        if (oldTris[oi].mask == nt.mask) {
                            nt.owner = oldTris[oi].owner;
                            nt.meCan = oldTris[oi].meCan;
                            nt.oppCan = oldTris[oi].oppCan;
                            break;
                        }
                    }
                    allTris[T_cnt++] = nt;
                }
            }
        }
    }
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
    pureEarMask = 0;
    degLe2Mask = 0;
    for (int u = 0; u < N; ++u) {
        if (adjMask[u] == 0) continue;
        if (adj[u].size() <= 2) degLe2Mask |= (1ULL << u);
        if (adj[u].size() == 2) {
            int x = adj[u][0], y = adj[u][1];
            if ((adjMask[x] >> y) & 1ULL) {
                pureEarMask |= (1ULL << u);
            }
        }
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
    E_cnt = 0;
    for (int i = 0; i < T_cnt; ++i) {
        const Tri& t = allTris[i];
        uint64_t zMask = adjMask[t.h1] | adjMask[t.h2] | adjMask[t.h3] | t.mask;
        triNbrUnion[i] = zMask;
        if ((t.owner == 1 || t.oppCan) &&
            oppReach[t.h1] >= 1 && oppReach[t.h2] >= 1 && oppReach[t.h3] >= 1) {
            int supply = 0;
            for (uint64_t m = (zMask & oppHasUnitsMask); m; m &= (m - 1)) {
                supply += oppUnits[__builtin_ctzll(m)];
            }
            for (int k = 0; k < T_cnt && supply < 3; ++k) {
                if (k != i && allTris[k].owner == 1 && (allTris[k].mask & t.mask)) {
                    supply++;
                }
            }
            if (supply >= 3) {
                bool ownedNow = (t.owner == 1);
                bool armedNow = ownedNow && (oppUnits[t.h1] >= 1 && oppUnits[t.h2] >= 1 && oppUnits[t.h3] >= 1);
                eatkTris[E_cnt++] = {t.h1, t.h2, t.h3, t.mask, zMask, ownedNow, armedNow};
                for (uint64_t m = zMask; m; m &= (m - 1)) {
                    int u = __builtin_ctzll(m);
                    if (armedNow) armedEnemyAtkZone[u] = true;
                    else potentialEnemyAtkZone[u] = true;
                }
            }
        }
    }

    S_cnt = C_cnt = R_cnt = 0;
    uint64_t ownedUnionMask = 0;
    for (int i = 0; i < T_cnt; ++i) {
        const Tri& t = allTris[i];
        if (t.owner == 0) {
            int idx = S_cnt++;
            ownedTriIdx[idx] = i;
            ownedUnionMask |= t.mask;
            oppCanThreatenOwned[idx] = (t.oppCan && oppReach[t.h1] >= 1 && oppReach[t.h2] >= 1 && oppReach[t.h3] >= 1);
        } else if (t.meCan) {
            capTriIdx[C_cnt++] = i;
        } else if (t.owner == -1 && !t.meCan) {
            resetTriIdx[R_cnt++] = i;
        }
    }
    interiorOwnedMask = 0;
    for (int u = 0; u < N; ++u) {
        if (adjMask[u] != 0 && ((ownedUnionMask >> u) & 1ULL) && (adjMask[u] & ~ownedUnionMask) == 0) {
            interiorOwnedMask |= (1ULL << u);
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

    oppHoldMode = (totalOppUnitsGlobal >= 34 || oppTriCountGlobal >= domThreshold - 5);

    // Army target & marginal spawn curve (scale smoothly to 70-90 units matching top players)
    int baseCap = max(68, (int)ceil(N * 1.5));
    int matchOpp = totalOppUnitsGlobal + 8;
    int maxCap = 96;
    if (oppTriCountGlobal >= domThreshold - 4) {
        maxCap = max(56, totalOppUnitsGlobal + 4);
    }
    armyTargetGlobal = clamp(max(baseCap, matchOpp), 60, maxCap);

    int remTurns = max(1, 200 - curTurn);
    marginalSpawnGain[0] = 0;
    for (int k = 1; k <= S_cnt; ++k) {
        int uAfter = M_cnt + k;
        int gain = 0;
        if (remTurns <= 12) {
            gain = min(1700, remTurns * 140);
        } else if (uAfter <= 18) {
            gain = 6800;
        } else if (uAfter <= 32) {
            gain = 5400;
        } else if (uAfter <= armyTargetGlobal) {
            gain = 2900 + (armyTargetGlobal - uAfter) * 165;
        } else {
            gain = max(450, 1150 - (uAfter - armyTargetGlobal) * 150);
        }
        marginalSpawnGain[k] = marginalSpawnGain[k - 1] + gain;
    }

    // Precompute multi-step nodePotential[u] and minCapDist[u] toward capturable triangles
    static const double decay[12] = {1.0, 0.56, 0.31, 0.17, 0.09, 0.05, 0.025, 0.012, 0.006, 0.003, 0.001, 0.0005};
    for (int u = 0; u < N; ++u) {
        double pot = 0.0;
        int bestDCap = 10;
        for (int ci = 0; ci < C_cnt; ++ci) {
            const Tri& t = allTris[capTriIdx[ci]];
            int d = INF_DIST;
            for (int c : t.c) {
                if (!((pureEarMask >> c) & 1ULL) || oppUnits[c] > 0) {
                    if (distMat[u][c] < d) d = distMat[u][c];
                }
            }
            if (d == INF_DIST) d = min({distMat[u][t.h1], distMat[u][t.h2], distMat[u][t.h3]});
            if (d < bestDCap) bestDCap = d;
            if (d >= 12) continue;
            double attr = 205.0;
            if (t.owner == 1) attr = (oppHoldMode ? 270.0 : 205.0);
            if (oppReach[t.h1] == 0 && oppReach[t.h2] == 0 && oppReach[t.h3] == 0) {
                attr += 60.0; // Free uncontested factory!
            }
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
            // Discount fortified enemy corners while we still have safer expansion targets
            int oppStack = oppUnits[t.h1] + oppUnits[t.h2] + oppUnits[t.h3];
            if (oppStack >= 3 && M_cnt < 30) attr *= 0.72;
            pot += attr * decay[d];
        }
        // If no meCan==1 triangles exist at all (rare), attract adjacent to resettable triangles (d == 1, NOT d == 0!)
        if (C_cnt == 0) {
            bestDCap = 0;
            for (int ri = 0; ri < R_cnt; ++ri) {
                const Tri& t = allTris[resetTriIdx[ri]];
                int d = min({distMat[u][t.h1], distMat[u][t.h2], distMat[u][t.h3]});
                if (d == 1) pot += 150.0;
                else if (d > 1 && d < 10) pot += 150.0 * decay[d];
            }
        }
        minCapDist[u] = bestDCap;
        nodePotential[u] = (int)llround(pot);
    }

    // Fast 1-ply opponent action prediction (conserves real opponent units + spawns, ~0.15ms)
    {
        auto oppT0 = chrono::high_resolution_clock::now();
        double oppBudgetMs = (curTurn == 1 ? 10.0 : 0.65);
        int O_cnt = 0;
        int oppLocs[512];
        uint64_t myHasMask = 0;
        for (int u = 0; u < N; ++u) {
            if (myUnits[u] > 0) myHasMask |= (1ULL << u);
            for (int k = 0; k < oppUnits[u] && O_cnt < 512; ++k) {
                oppLocs[O_cnt++] = u;
            }
        }
        int OS_cnt = 0, OC_cnt = 0, OR_cnt = 0;
        int oppOwnedIdx[MAX_T], oppCapIdx[MAX_T], oppResetIdx[MAX_T];
        for (int i = 0; i < T_cnt; ++i) {
            const Tri& t = allTris[i];
            if (t.owner == 1) oppOwnedIdx[OS_cnt++] = i;
            else if (t.oppCan) oppCapIdx[OC_cnt++] = i;
            else if (t.owner == -1 && !t.oppCan) oppResetIdx[OR_cnt++] = i;
        }
        if (O_cnt == 0 && OS_cnt == 0) {
            memset(oppPredUnits, 0, sizeof(oppPredUnits));
            oppPredHasUnitsMask = 0;
        } else {
            uint64_t oppSelfSurrMask = 0;
            for (int u = 0; u < N; ++u) {
                if (adjMask[u] != 0 && (adjMask[u] & ~myHasMask) == 0) {
                    oppSelfSurrMask |= (1ULL << u);
                }
            }
            int oppPot[MAX_N], oppMinD[MAX_N];
            bool myHoldThreat = (M_cnt >= 34 || S_cnt >= domThreshold - 5);
            for (int u = 0; u < N; ++u) {
                double pot = 0.0;
                int bestD = 10;
                for (int ci = 0; ci < OC_cnt; ++ci) {
                    const Tri& t = allTris[oppCapIdx[ci]];
                    int d = min({distMat[u][t.h1], distMat[u][t.h2], distMat[u][t.h3]});
                    if (d < bestD) bestD = d;
                    if (d >= 12) continue;
                    double attr = (t.owner == 0 && myHoldThreat) ? 270.0 : 205.0;
                    int nearC = 0;
                    for (int c : t.c) {
                        bool hn = (oppUnits[c] > myUnits[c]);
                        if (!hn) {
                            for (int nb : adj[c]) if (oppUnits[nb] > 0) { hn = true; break; }
                        }
                        if (hn) nearC++;
                    }
                    attr += nearC * 55.0;
                    pot += attr * decay[d];
                }
                oppMinD[u] = bestD;
                oppPot[u] = (int)llround(pot);
            }

            int oppTarget = clamp(max(baseCap, M_cnt + 6), 42, 64);

            auto evalOpp = [&](const int* oDest, const int* oSp) -> int64_t {
                int u1[MAX_N];
                memset(u1, 0, N * sizeof(int));
                uint64_t oOcc = 0;
                for (int i = 0; i < O_cnt; ++i) {
                    int d = oDest[i];
                    u1[d]++;
                    oOcc |= (1ULL << d);
                }
                for (int j = 0; j < OS_cnt; ++j) {
                    if (oSp[j] >= 0) {
                        int spN = allTris[oppOwnedIdx[j]].c[oSp[j]];
                        u1[spN]++;
                        oOcc |= (1ULL << spN);
                    }
                }
                uint64_t a1 = 0, a0 = 0;
                for (uint64_t m = oOcc; m; m &= (m - 1)) {
                    int u = __builtin_ctzll(m);
                    if (u1[u] > myUnits[u]) a1 |= (1ULL << u);
                }
                for (uint64_t m = myHasMask; m; m &= (m - 1)) {
                    int u = __builtin_ctzll(m);
                    if (myUnits[u] > u1[u]) a0 |= (1ULL << u);
                }
                uint64_t kOppMask = 0, kMeMask = 0;
                int kOpp = 0, kMe = 0;
                for (uint64_t m = (oOcc & oppSelfSurrMask); m; m &= (m - 1)) {
                    int u = __builtin_ctzll(m);
                    if ((adjMask[u] & ~a0) == 0) { kOpp += u1[u]; kOppMask |= (1ULL << u); }
                }
                for (uint64_t m = myHasMask; m; m &= (m - 1)) {
                    int u = __builtin_ctzll(m);
                    if (adjMask[u] != 0 && (adjMask[u] & ~a1) == 0) {
                        int thin = 0;
                        for (int v : adj[u]) if (u1[v] <= myUnits[v] + 1) thin++;
                        if (thin == 0 || (myUnits[u] == 1 && thin == 1 && adj[u].size() >= 4)) {
                            kMe += myUnits[u];
                            kMeMask |= (1ULL << u);
                        }
                    }
                }
                if (kOppMask | kMeMask) {
                    a1 &= ~kOppMask;
                    a0 &= ~kMeMask;
                    for (uint64_t m = (kMeMask & oOcc & ~kOppMask); m; m &= (m - 1)) {
                        int u = __builtin_ctzll(m);
                        if (u1[u] > 0) a1 |= (1ULL << u);
                    }
                    for (uint64_t m = (kOppMask & myHasMask & ~kMeMask); m; m &= (m - 1)) {
                        int u = __builtin_ctzll(m);
                        if (myUnits[u] > 0) a0 |= (1ULL << u);
                    }
                }
                uint64_t ow1 = 0;
                for (int u = 0; u < N; ++u) {
                    uint64_t am = adjMask[u];
                    bool o1 = false, o0 = false;
                    if (am != 0) {
                        if ((am & ~a1) == 0 && (myUnits[u] == 0 || ((kMeMask >> u) & 1ULL))) o1 = true;
                        else if ((am & ~a0) == 0) o0 = true;
                    }
                    if (!o1 && !o0) o1 = (a1 >> u) & 1ULL;
                    if (o1) ow1 |= (1ULL << u);
                }

                int64_t sc = (int64_t)kMe * 3400LL - (int64_t)kOpp * 5500LL;
                int totOw = 0;
                for (int j = 0; j < OS_cnt; ++j) {
                    if (oSp[j] == -1) { totOw++; sc += 2200; }
                    else {
                        int uAfter = O_cnt + j + 1;
                        sc += (uAfter <= 32 ? 5400 : (uAfter <= oppTarget ? 3600 : 900));
                    }
                }
                for (int ci = 0; ci < OC_cnt; ++ci) {
                    const Tri& t = allTris[oppCapIdx[ci]];
                    uint64_t m = ow1 & t.mask;
                    if (m == t.mask) {
                        totOw++;
                        sc += 4500 + (t.owner == 0 ? 500 : 0);
                    } else {
                        int cnt = __builtin_popcountll(m);
                        if (cnt == 2) sc += 300;
                        else if (cnt == 1) sc += 55;
                    }
                }
                for (int ri = 0; ri < OR_cnt; ++ri) {
                    const Tri& t = allTris[oppResetIdx[ri]];
                    if ((oOcc & t.mask) == 0) sc += 1250;
                    else {
                        int occ = (u1[t.h1] > 0) + (u1[t.h2] > 0) + (u1[t.h3] > 0);
                        sc -= 350 + occ * 250;
                    }
                }
                if (totOw >= domThreshold) sc += 80000000LL + totOw * 20000LL;

                uint64_t aliveO = oOcc & ~kOppMask;
                for (uint64_t m = aliveO; m; m &= (m - 1)) {
                    int u = __builtin_ctzll(m);
                    int cnt = u1[u];
                    int useful = min(cnt, myUnits[u] + 1);
                    int extra = cnt - useful;
                    sc += (int64_t)useful * oppPot[u] - (int64_t)cnt * oppMinD[u] * 85LL;
                    if (extra > 0) {
                        sc += ((int64_t)extra * oppPot[u]) >> 2;
                        sc -= (int64_t)extra * extra * 65LL;
                    }
                }
                return sc;
            };

            vector<int> oDest(O_cnt);
            vector<int> oSp(OS_cnt, -1);
            for (int i = 0; i < O_cnt; ++i) oDest[i] = oppLocs[i];
            for (int j = 0; j < OS_cnt; ++j) {
                if (O_cnt + j < oppTarget) {
                    const Tri& t = allTris[oppOwnedIdx[j]];
                    int bestC = 0, bestP = -999999;
                    for (int k = 0; k < 3; ++k) {
                        int c = t.c[k];
                        int pen = (adjMask[c] != 0 && (adjMask[c] & ~t.mask) == 0) ? 1800 : 0;
                        int p = oppPot[c] - pen;
                        if (p > bestP) { bestP = p; bestC = k; }
                    }
                    oSp[j] = bestC;
                }
            }
            int64_t bestOppSc = evalOpp(oDest.data(), oSp.data());

            // Greedy 3-corner triangle capture pass for opponent
            int sortedOC[MAX_T];
            for (int ci = 0; ci < OC_cnt; ++ci) sortedOC[ci] = oppCapIdx[ci];
            sort(sortedOC, sortedOC + OC_cnt, [&](int a, int b) {
                const Tri& ta = allTris[a]; const Tri& tb = allTris[b];
                return (oppPot[ta.h1] + oppPot[ta.h2] + oppPot[ta.h3]) > (oppPot[tb.h1] + oppPot[tb.h2] + oppPot[tb.h3]);
            });
            bool lockO[512] = {false};
            bool lockOS[MAX_T] = {false};
            for (int ci = 0; ci < OC_cnt; ++ci) {
                if ((ci & 7) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - oppT0).count() >= oppBudgetMs) break;
                const Tri& t = allTris[sortedOC[ci]];
                int svIdx[16], svVal[16], nSv = 0;
                int svSpIdx[8], svSpVal[8], nSvSp = 0;
                bool tmpLockO[32]; int nTmpO = 0;
                bool tmpLockS[16]; int nTmpS = 0;
                int tmpOList[32], tmpSList[16];
                bool okAll = true;
                for (int k = 0; k < 3; ++k) {
                    int corner = t.c[k];
                    int req = myUnits[corner] + 1;
                    if (adjMask[corner] != 0 && (adjMask[corner] & ~t.mask) == 0 && myUnits[corner] == 0) req = 0;
                    int asg = 0;
                    for (int i = 0; i < O_cnt && asg < req; ++i) {
                        if (!lockO[i] && oDest[i] == corner) {
                            lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                        }
                    }
                    for (int j = 0; j < OS_cnt && asg < req; ++j) {
                        if (!lockOS[j] && oSp[j] >= 0 && allTris[oppOwnedIdx[j]].c[oSp[j]] == corner) {
                            lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                        }
                    }
                    for (int i = 0; i < O_cnt && asg < req; ++i) {
                        if (lockO[i]) continue;
                        int u = oppLocs[i];
                        if ((u == corner || ((initAdjMask[u] >> corner) & 1ULL)) && nSv < 16 && nTmpO < 32) {
                            svIdx[nSv] = i; svVal[nSv] = oDest[i]; nSv++;
                            oDest[i] = corner;
                            lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                        }
                    }
                    for (int j = 0; j < OS_cnt && asg < req; ++j) {
                        if (lockOS[j]) continue;
                        const Tri& ot = allTris[oppOwnedIdx[j]];
                        for (int sc = 0; sc < 3; ++sc) {
                            if (ot.c[sc] == corner && nSvSp < 8 && nTmpS < 16) {
                                svSpIdx[nSvSp] = j; svSpVal[nSvSp] = oSp[j]; nSvSp++;
                                oSp[j] = sc;
                                lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                                break;
                            }
                        }
                    }
                    if (asg < req) { okAll = false; break; }
                }
                (void)tmpLockO; (void)tmpLockS;
                if (okAll && (nSv > 0 || nSvSp > 0)) {
                    int64_t sc = evalOpp(oDest.data(), oSp.data());
                    if (sc > bestOppSc) {
                        bestOppSc = sc;
                        continue;
                    }
                }
                for (int k = nSv - 1; k >= 0; --k) oDest[svIdx[k]] = svVal[k];
                for (int k = nSvSp - 1; k >= 0; --k) oSp[svSpIdx[k]] = svSpVal[k];
                for (int k = 0; k < nTmpO; ++k) lockO[tmpOList[k]] = false;
                for (int k = 0; k < nTmpS; ++k) lockOS[tmpSList[k]] = false;
            }

            // Single fast coordinate-ascent refinement pass for opponent
            for (int pass = 0; pass < 1; ++pass) {
                if (chrono::duration<double, milli>(chrono::high_resolution_clock::now() - oppT0).count() >= oppBudgetMs) break;
                for (int i = 0; i < O_cnt; ++i) {
                    if ((i & 15) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - oppT0).count() >= oppBudgetMs) break;
                    int u = oppLocs[i];
                    int orig = oDest[i], bestOpt = orig;
                    for (int k = 0; k < numMoveOpts[u]; ++k) {
                        int cand = moveOpts[u][k];
                        if (cand == orig) continue;
                        oDest[i] = cand;
                        int64_t sc = evalOpp(oDest.data(), oSp.data());
                        if (sc > bestOppSc) { bestOppSc = sc; bestOpt = cand; }
                    }
                    oDest[i] = bestOpt;
                }
                for (int j = 0; j < OS_cnt; ++j) {
                    int orig = oSp[j], bestOpt = orig;
                    for (int cand = -1; cand < 3; ++cand) {
                        if (cand == orig) continue;
                        oSp[j] = cand;
                        int64_t sc = evalOpp(oDest.data(), oSp.data());
                        if (sc > bestOppSc) { bestOppSc = sc; bestOpt = cand; }
                    }
                    oSp[j] = bestOpt;
                }
            }

            memset(oppPredUnits, 0, sizeof(oppPredUnits));
            oppPredHasUnitsMask = 0;
            for (int i = 0; i < O_cnt; ++i) {
                oppPredUnits[oDest[i]]++;
                oppPredHasUnitsMask |= (1ULL << oDest[i]);
            }
            for (int j = 0; j < OS_cnt; ++j) {
                if (oSp[j] >= 0) {
                    int spN = allTris[oppOwnedIdx[j]].c[oSp[j]];
                    oppPredUnits[spN]++;
                    oppPredHasUnitsMask |= (1ULL << spN);
                }
            }
        }
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

    // Step 1: surroundNodes() simulation (guard against thin-ring mirages around oppUnits[u] > 0)
    int killedMyUnits = 0;
    int killedOppUnits = 0;
    int oppContainedBonus = 0;
    uint64_t killedMyMask = 0;
    uint64_t killedOppMask = 0;

    for (uint64_t m = myOccMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if ((adjMask[u] & ~adv1) == 0) {
            killedMyUnits += u0[u];
            killedMyMask |= (1ULL << u);
        }
    }
    for (uint64_t m = oppHasUnitsMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (adjMask[u] != 0 && (adjMask[u] & ~adv0) == 0) {
            int thinNbrs = 0;
            for (int v : adj[u]) {
                if (u0[v] <= oppUnits[v] + 1) thinNbrs++;
            }
            if (thinNbrs == 0 || (oppUnits[u] == 1 && thinNbrs == 1 && adj[u].size() >= 4)) {
                killedOppUnits += oppUnits[u];
                killedOppMask |= (1ULL << u);
            } else {
                oppContainedBonus += 250;
            }
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

    // Also compute robust advantage mask vs oppRobustUnits & predicted 1-ply mask vs oppPredUnits
    uint64_t adv0_rob = 0, adv1_rob = 0;
    uint64_t adv0_pred = 0, adv1_pred = 0;
    for (uint64_t m = (myOccMask & ~killedMyMask); m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (u0[u] > oppRobustUnits[u]) adv0_rob |= (1ULL << u);
        if (u0[u] > oppPredUnits[u]) adv0_pred |= (1ULL << u);
    }
    for (uint64_t m = (oppRobustHasUnitsMask & ~killedOppMask); m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        int u0_eff = ((killedMyMask >> u) & 1ULL) ? 0 : u0[u];
        if (oppRobustUnits[u] > u0_eff) adv1_rob |= (1ULL << u);
    }
    for (uint64_t m = oppPredHasUnitsMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        int u0_eff = ((killedMyMask >> u) & 1ULL) ? 0 : u0[u];
        if (oppPredUnits[u] > u0_eff) adv1_pred |= (1ULL << u);
    }

    uint64_t predBlockMask = oppPredHasUnitsMask & ~adv0_pred;

    // Step 2: Node ownership bitmasks
    uint64_t owned0 = 0, owned1 = 0, owned0_rob = 0;
    for (int u = 0; u < N; ++u) {
        uint64_t am = adjMask[u];
        bool o0 = false, o1 = false;
        if (am != 0) {
            if ((am & ~adv0) == 0) {
                if (((killedOppMask >> u) & 1ULL) != 0) {
                    o0 = true;
                } else if (oppUnits[u] == 0) {
                    // Guard against vacating contested frontline hubs (deg<=2 corners have no external neighbors!)
                    if (u0[u] > 0 || __builtin_popcountll(am) <= 2 || oppReach[u] == 0 || (am & ~adv0_rob) == 0) {
                        o0 = true;
                    }
                }
            } else if ((am & ~adv1) == 0) {
                o1 = true;
            }
        }
        if (!o0 && !o1) {
            o0 = (adv0 >> u) & 1ULL;
            o1 = (adv1 >> u) & 1ULL;
        }
        if (o0) owned0 |= (1ULL << u);
        else if (o1) owned1 |= (1ULL << u);

        bool or0 = false, or1 = false;
        if (am != 0) {
            if ((am & ~adv0_rob) == 0 && (oppUnits[u] == 0 || ((killedOppMask >> u) & 1ULL))) or0 = true;
            else if ((am & ~adv1_rob) == 0) or1 = true;
        }
        if (!or0 && !or1) {
            or0 = (adv0_rob >> u) & 1ULL;
        }
        if (or0) owned0_rob |= (1ULL << u);
    }

    int64_t score = oppContainedBonus;
    score += marginalSpawnGain[numSpawns];
    score += (int64_t)killedOppUnits * 3400LL;
    score -= (int64_t)killedMyUnits * 5500LL;

    int totalOwnedEnd = 0;
    int safeOwnedEnd = 0;

    // Evaluate owned triangles (HOLD vs SPAWN)
    for (int j = 0; j < S_cnt; ++j) {
        const Tri& t = allTris[ownedTriIdx[j]];
        int sc = spawnChoice[j];
        if (sc == -1) {
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
            int spNode = t.c[sc];
            if (adjMask[spNode] != 0 && (adjMask[spNode] & ~t.mask) == 0) {
                score -= 1800; // Never spawn on a degree-2 pocket corner with no 1-step exit out of t!
            } else if (((interiorOwnedMask >> spNode) & 1ULL) && (t.mask & ~interiorOwnedMask) != 0) {
                score -= 1100; // Prefer spawning on boundary hubs of owned clusters over interior pockets!
            }
            int occ = (u0[t.h1] > 0) + (u0[t.h2] > 0) + (u0[t.h3] > 0);
            score -= (occ - 1) * 200;
        }
    }

    // Evaluate capturable triangles (meCan == 1 && owner != 0)
    for (int ci = 0; ci < C_cnt; ++ci) {
        int tid = capTriIdx[ci];
        const Tri& t = allTris[tid];
        if (!t.meCan) continue;
        uint64_t m = owned0 & t.mask;
        if (m == t.mask) {
            totalOwnedEnd++;
            int robCnt = __builtin_popcountll(owned0_rob & t.mask);
            int predHits = __builtin_popcountll(predBlockMask & t.mask);
            score += 4750 - (3 - robCnt) * 650 - (predHits == 0 ? 0 : (predHits == 1 ? 550 : 950));
            if (robCnt == 3 && predHits == 0) {
                safeOwnedEnd++;
            }
            if (t.owner == 1) {
                if (oppHoldMode) {
                    score += 750 + robCnt * 250; // Up to +1500 when opponent is holding/dominating
                } else {
                    score += 250 + robCnt * 150; // Modest bonus during growth phase (opp spawns anyway!)
                }
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
            uint64_t extRing = triNbrUnion[tid] & ~t.mask;
            int poised = 0;
            for (uint64_t pm = (myOccMask & extRing); pm && poised < 3; pm &= (pm - 1)) {
                poised += u0[__builtin_ctzll(pm)];
            }
            score += 1350 + min(3, poised) * 260;
        } else {
            int occ = (u0[t.h1] > 0) + (u0[t.h2] > 0) + (u0[t.h3] > 0);
            int totU = u0[t.h1] + u0[t.h2] + u0[t.h3];
            score -= 420 + occ * 280 + min(4, totU) * 80;
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

    // Dynamic enemy ATTACK zone mask (accounts for triangles we genuinely block against oppReach!)
    uint64_t activeArmedMask = 0, activePotMask = 0;
    for (int ei = 0; ei < E_cnt; ++ei) {
        const EnemyAtkTri& et = eatkTris[ei];
        if (et.ownedNow) {
            if ((owned0_rob & et.mask) != et.mask) {
                if (et.armedNow && (killedOppMask & et.mask) == 0) activeArmedMask |= et.zMask;
                else activePotMask |= et.zMask;
            }
        } else {
            bool weBlock = false;
            int cs[3] = {et.c0, et.c1, et.c2};
            for (int k = 0; k < 3; ++k) {
                int c = cs[k];
                int uAlive = ((killedMyMask >> c) & 1ULL) ? 0 : u0[c];
                if (((owned0_rob >> c) & 1ULL) && uAlive >= oppReach[c]) {
                    weBlock = true;
                    break;
                }
            }
            if (!weBlock) {
                activePotMask |= et.zMask;
            }
        }
    }

    // Evaluate occupied nodes: potential, multi-front dispersion + forward march, anti-lock & kill-zone discipline
    uint64_t aliveOccMask = myOccMask & ~killedMyMask;
    for (uint64_t m = aliveOccMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        int cnt = u0[u];
        int need = oppUnits[u] + 1;
        int useful = min(cnt, need);
        int extra = cnt - useful;

        score -= (int64_t)cnt * minCapDist[u] * 85LL;
        score += (int64_t)useful * nodePotential[u];
        if (extra > 0) {
            if (oppReach[u] > 0) {
                score += ((int64_t)extra * nodePotential[u]) >> 1;
                score -= (int64_t)extra * extra * 48LL;
            } else {
                score += ((int64_t)extra * nodePotential[u]) >> 2;
                score -= (int64_t)extra * extra * 85LL;
            }
        }

        // Pure degree-2 ear & interior owned-cluster pocket guards
        if (((pureEarMask >> u) & 1ULL) && oppUnits[u] == 0) {
            score -= (int64_t)cnt * 950LL;
        } else if (((interiorOwnedMask >> u) & 1ULL) && oppUnits[u] == 0) {
            score -= (int64_t)cnt * 450LL;
        }

        // Enemy ATTACK kill-zone discipline (for Priority 0 at start of Turn T+1!)
        if (cnt >= 3) {
            if ((activeArmedMask >> u) & 1ULL) {
                score -= (cnt == 3 ? 800LL : 2600LL + (int64_t)(cnt - 3) * 5000LL);
            } else if ((activePotMask >> u) & 1ULL) {
                score -= (cnt == 3 ? 450LL : 2000LL + (int64_t)(cnt - 3) * 4400LL);
            }
        }

        // 1-ply predicted surround ("Matador" bait-and-step-off) trap guard!
        if (adjMask[u] != 0 && (adjMask[u] & ~adv1_pred) == 0) {
            score -= (int64_t)cnt * 2800LL;
        } else if ((canBeSurr1StepMask >> u) & 1ULL) {
            // 1-step dynamic enemy surround trap caution (only fires when disconnected without safe supply neighbor!)
            bool hasSafeEscape = false;
            for (int v : adj[u]) {
                if (u0[v] >= oppReach[v] || (oppReach[v] >= 4 && u0[v] >= oppUnits[v] + 3)) {
                    hasSafeEscape = true;
                    break;
                }
            }
            if (!hasSafeEscape) {
                if (((degLe2Mask >> u) & 1ULL) || (adjMask[u] != 0 && (adjMask[u] & ~adv1_rob) == 0)) {
                    score -= (int64_t)cnt * 1850LL;
                } else {
                    score -= (int64_t)cnt * 1150LL;
                }
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

// Helper to find which meCan triangles are captured by a 3-unit configuration {a, b, c}
void findCapturedBy3(int a, int b, int c, vector<int>& capTids) {
    capTids.clear();
    int u0[MAX_N];
    memset(u0, 0, N * sizeof(int));
    u0[a]++; u0[b]++; u0[c]++;
    uint64_t adv0 = 0;
    for (int u : {a, b, c}) if (u0[u] > oppUnits[u]) adv0 |= (1ULL << u);
    uint64_t owned0 = 0;
    for (int u = 0; u < N; ++u) {
        uint64_t am = adjMask[u];
        bool o0 = false;
        if (am != 0 && (am & ~adv0) == 0 && oppUnits[u] == 0) {
            if (u0[u] > 0 || __builtin_popcountll(am) <= 2 || oppReach[u] == 0) o0 = true;
        }
        if (!o0) o0 = (adv0 >> u) & 1ULL;
        if (o0) owned0 |= (1ULL << u);
    }
    for (int ci = 0; ci < C_cnt; ++ci) {
        int tid = capTriIdx[ci];
        const Tri& t = allTris[tid];
        if (t.meCan && (owned0 & t.mask) == t.mask) capTids.push_back(tid);
    }
}

// Multi-ply deduplicated exact opening solver for Turn 1 (3-ply) & Turn 2 (2-ply) when M_cnt == 3, S_cnt == 0
void solveOpening3Units(chrono::high_resolution_clock::time_point startTp, double limitMs,
                        vector<int>& outDest, int64_t& outScore) {
    int origLocs[3] = {unitLocs[0], unitLocs[1], unitLocs[2]};
    int origTurn = curTurn;
    curTurn = 99; // Disable single-ply heuristic distance fallback inside evalState
    int dummySp[1] = {-1};

    struct Ply1Move {
        int d0, d1, d2;
        int64_t sc1;
        int u1Idx;
    };
    struct State3 {
        int a, b, c;
    };

    vector<Ply1Move> p1Moves;
    p1Moves.reserve(350);
    vector<State3> uniq1;
    uniq1.reserve(128);
    vector<int> key1Map(262144, -1);

    int n0 = numMoveOpts[origLocs[0]];
    int n1 = numMoveOpts[origLocs[1]];
    int n2 = numMoveOpts[origLocs[2]];

    for (int i0 = 0; i0 < n0; ++i0) {
        int d0 = moveOpts[origLocs[0]][i0];
        for (int i1 = 0; i1 < n1; ++i1) {
            int d1 = moveOpts[origLocs[1]][i1];
            for (int i2 = 0; i2 < n2; ++i2) {
                int d2 = moveOpts[origLocs[2]][i2];
                int dest[3] = {d0, d1, d2};
                int64_t sc1 = evalState(dest, dummySp);
                int s[3] = {d0, d1, d2};
                if (s[1] < s[0]) swap(s[0], s[1]);
                if (s[2] < s[1]) swap(s[1], s[2]);
                if (s[1] < s[0]) swap(s[0], s[1]);
                int key = (s[0] << 12) | (s[1] << 6) | s[2];
                int idx = key1Map[key];
                if (idx == -1) {
                    idx = (int)uniq1.size();
                    key1Map[key] = idx;
                    uniq1.push_back({s[0], s[1], s[2]});
                }
                p1Moves.push_back({d0, d1, d2, sc1, idx});
            }
        }
    }

    int U1 = (int)uniq1.size();
    vector<int64_t> bestP2Only(U1, -1000000000000LL);
    bool doPly3 = (origTurn == 1);

    struct Edge12 {
        int64_t sc2;
        int u2Idx;
    };
    vector<vector<Edge12>> edges12(doPly3 ? U1 : 0);
    vector<State3> uniq2;
    vector<int> key2Map;
    if (doPly3) {
        uniq2.reserve(1024);
        key2Map.assign(262144, -1);
    }

    vector<int> cap1, cap2;
    bool timedOut = false;
    int savedOppPred[MAX_N];
    memcpy(savedOppPred, oppPredUnits, sizeof(oppPredUnits));
    uint64_t savedOppPredMask = oppPredHasUnitsMask;
    memset(oppPredUnits, 0, sizeof(oppPredUnits));
    oppPredHasUnitsMask = 0;

    for (int u1 = 0; u1 < U1; ++u1) {
        double el = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - startTp).count();
        if (el >= limitMs) { timedOut = true; break; }
        int a = uniq1[u1].a, b = uniq1[u1].b, c = uniq1[u1].c;
        findCapturedBy3(a, b, c, cap1);
        for (int tid : cap1) allTris[tid].meCan = false;
        int64_t p1SpawnBonus = (int64_t)cap1.size() * 6800LL;

        unitLocs[0] = a; unitLocs[1] = b; unitLocs[2] = c;
        int ma = numMoveOpts[a], mb = numMoveOpts[b], mc = numMoveOpts[c];
        for (int ia = 0; ia < ma; ++ia) {
            int e0 = moveOpts[a][ia];
            for (int ib = 0; ib < mb; ++ib) {
                int e1 = moveOpts[b][ib];
                for (int ic = 0; ic < mc; ++ic) {
                    int e2 = moveOpts[c][ic];
                    int dest2[3] = {e0, e1, e2};
                    int64_t sc2 = evalState(dest2, dummySp) + p1SpawnBonus;
                    if (sc2 > bestP2Only[u1]) bestP2Only[u1] = sc2;
                    if (doPly3 && cap1.empty()) {
                        int s[3] = {e0, e1, e2};
                        if (s[1] < s[0]) swap(s[0], s[1]);
                        if (s[2] < s[1]) swap(s[1], s[2]);
                        if (s[1] < s[0]) swap(s[0], s[1]);
                        int key = (s[0] << 12) | (s[1] << 6) | s[2];
                        int idx2 = key2Map[key];
                        if (idx2 == -1) {
                            idx2 = (int)uniq2.size();
                            key2Map[key] = idx2;
                            uniq2.push_back({s[0], s[1], s[2]});
                        }
                        edges12[u1].push_back({sc2, idx2});
                    }
                }
            }
        }
        for (int tid : cap1) allTris[tid].meCan = true;
    }

    vector<int64_t> bestSubtree(U1, 0);
    if (doPly3 && !timedOut && uniq2.size() <= 2500) {
        int U2 = (int)uniq2.size();
        vector<int64_t> bestP3(U2, 0);
        bool p3Complete = true;
        for (int u2 = 0; u2 < U2; ++u2) {
            if ((u2 & 15) == 0) {
                double el = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - startTp).count();
                if (el >= limitMs) { p3Complete = false; break; }
            }
            int a = uniq2[u2].a, b = uniq2[u2].b, c = uniq2[u2].c;
            findCapturedBy3(a, b, c, cap2);
            for (int tid : cap2) allTris[tid].meCan = false;
            int64_t p2SpawnBonus = (int64_t)cap2.size() * 6800LL;

            unitLocs[0] = a; unitLocs[1] = b; unitLocs[2] = c;
            int ma = numMoveOpts[a], mb = numMoveOpts[b], mc = numMoveOpts[c];
            int64_t mx3 = -1000000000000LL;
            for (int ia = 0; ia < ma; ++ia) {
                int f0 = moveOpts[a][ia];
                for (int ib = 0; ib < mb; ++ib) {
                    int f1 = moveOpts[b][ib];
                    for (int ic = 0; ic < mc; ++ic) {
                        int dest3[3] = {f0, f1, moveOpts[c][ic]};
                        int64_t sc3 = evalState(dest3, dummySp) + p2SpawnBonus;
                        if (sc3 > mx3) mx3 = sc3;
                    }
                }
            }
            for (int tid : cap2) allTris[tid].meCan = true;
            bestP3[u2] = mx3;
        }
        if (p3Complete) {
            for (int u1 = 0; u1 < U1; ++u1) {
                if (edges12[u1].empty()) {
                    bestSubtree[u1] = bestP2Only[u1] * 4;
                    continue;
                }
                int64_t mx = -1000000000000LL;
                for (const auto& ed : edges12[u1]) {
                    int64_t v = ed.sc2 * 3 + bestP3[ed.u2Idx];
                    if (v > mx) mx = v;
                }
                bestSubtree[u1] = mx;
            }
        } else {
            for (int u1 = 0; u1 < U1; ++u1) bestSubtree[u1] = bestP2Only[u1] * 3;
        }
    } else {
        for (int u1 = 0; u1 < U1; ++u1) bestSubtree[u1] = bestP2Only[u1] * 3;
    }

    int64_t bestTotal = -1000000000000000LL;
    for (const auto& pm : p1Moves) {
        int64_t sub = (bestP2Only[pm.u1Idx] > -900000000000LL) ? bestSubtree[pm.u1Idx] : 0;
        int64_t tot = pm.sc1 * 3 + sub;
        if (tot > bestTotal) {
            bestTotal = tot;
            outDest[0] = pm.d0;
            outDest[1] = pm.d1;
            outDest[2] = pm.d2;
            outScore = pm.sc1;
        }
    }

    memcpy(oppPredUnits, savedOppPred, sizeof(oppPredUnits));
    oppPredHasUnitsMask = savedOppPredMask;
    unitLocs[0] = origLocs[0]; unitLocs[1] = origLocs[1]; unitLocs[2] = origLocs[2];
    curTurn = origTurn;
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
            sort(adj[u].begin(), adj[u].end());
            initAdj[u] = adj[u];
            initAdjMask[u] = adjMask[u];
            moveOpts[u][0] = u;
            int cnt = 1;
            for (int v : initAdj[u]) moveOpts[u][cnt++] = v;
            numMoveOpts[u] = cnt;
        }

        computeAllPairsBFS();

        cin >> T_cnt;
        int initOppTris = 0;
        int curMyTris = 0;
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
            else if (ow == 0) curMyTris++;
        }

        int linkableCount;
        cin >> linkableCount;
        vector<pair<int, int>> linkablePairs(linkableCount);
        for (int i = 0; i < linkableCount; ++i) {
            cin >> linkablePairs[i].first >> linkablePairs[i].second;
        }

        vector<string> actions;
        vector<string> postMoveActions;

        // ====================================================================
        // PHASE 0: TACTICAL PRE-MOVE ATTACKS (Priority 0 in Referee!)
        // Positive-EV stack wipeouts & encirclement breakouts before MOVE!
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

                    int oppTrisOnU = 0;
                    for (int k = 0; k < T_cnt; ++k) {
                        if (allTris[k].owner == 1 && allTris[k].hasNode(u)) oppTrisOnU++;
                    }

                    // Check if wiping u rescues a boxed-in friendly multi-unit stack
                    int savedBoxed = 0;
                    for (int v : adj[u]) {
                        if (myUnits[v] >= 2 && !adj[v].empty()) {
                            bool allBoxed = true;
                            for (int nb : adj[v]) {
                                if (oppUnits[nb] < myUnits[nb] || oppUnits[nb] == 0) {
                                    allBoxed = false;
                                    break;
                                }
                            }
                            if (allBoxed) savedBoxed += min(4, myUnits[v]);
                        }
                    }

                    int effKill = oppU + savedBoxed;
                    bool worthIt = false;
                    if (effKill >= 6 && curMyTotal >= 8) worthIt = true;
                    else if (effKill == 5 && curMyTotal >= 16 && (curMyTris >= 2 || curMyTotal >= curOppTotal)) worthIt = true;
                    else if (effKill == 4 && curMyTotal >= 36 && curMyTotal >= curOppTotal + 2 && curMyTris >= 2) worthIt = true;
                    else if (effKill == 3 && curMyTotal >= 46 && curMyTotal >= curOppTotal + 12 && curMyTris >= 4) worthIt = true;
                    else if (oppU == 2 && curMyTotal >= 52 && curMyTotal >= curOppTotal + 18 && oppTrisOnU >= 2 && curMyTris >= 5) worthIt = true;

                    if (worthIt) {
                        int val = effKill * 120 + oppU * 40 + oppTrisOnU * 35;
                        // Bonus if attacking leaves 0 units on t so t.meCan resets to 1 immediately!
                        if (myUnits[t.h1] == 1 && myUnits[t.h2] == 1 && myUnits[t.h3] == 1 && !t.hasNode(u)) {
                            val += 40;
                        }
                        // Prefer spending safe backline triangles over contested frontline ones
                        int frontExposed = (oppUnits[t.h1] > 0) + (oppUnits[t.h2] > 0) + (oppUnits[t.h3] > 0);
                        val -= frontExposed * 30;
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
            curMyTris--;
            curOppTotal -= oppUnits[bestTarget];
            oppUnits[bestTarget] = 0;
            bt.owner = -1;
            bt.meCan = false;
        }

        // ====================================================================
        // PHASE 0.5: GOLD LEAGUE TACTICAL ADD_PATH (Prio 3) & REMOVE_PATH (Prio 4)
        // Reserves 1 unit on each used corner so MOVE (Prio 1) never touches them!
        // ====================================================================
        bool graphChanged = false;
        int initDomNow = 0;
        while ((double)initDomNow < (double)T_cnt * 0.8) initDomNow++;
        int maxAddPaths = (curMyTris >= initDomNow - 3) ? 0 :
                          ((curMyTotal >= 55 && curMyTotal >= curOppTotal + 12 && curMyTris >= 6) ? 1 : 0);

        for (int addStep = 0; addStep < maxAddPaths && !linkablePairs.empty(); ++addStep) {
            uint64_t oppNowMask = 0;
            for (int u = 0; u < N; ++u) if (oppUnits[u] > 0) oppNowMask |= (1ULL << u);

            int bestTri = -1, bestFrom = -1, bestTo = -1, bestVal = 0;
            for (int i = 0; i < T_cnt; ++i) {
                const Tri& t = allTris[i];
                if (t.owner != 0) continue;
                if (myUnits[t.h1] < 1 || myUnits[t.h2] < 1 || myUnits[t.h3] < 1) continue;

                for (const auto& lp : linkablePairs) {
                    for (int dir = 0; dir < 2; ++dir) {
                        int fr = (dir == 0 ? lp.first : lp.second);
                        int to = (dir == 0 ? lp.second : lp.first);
                        if (!t.hasNode(fr) || t.hasNode(to)) continue;
                        if (!canAddEdgeNow(fr, to)) continue;

                        uint64_t common = adjMask[fr] & adjMask[to];
                        int newTris = 0;
                        uint64_t quadMask = (1ULL << fr) | (1ULL << to);
                        for (uint64_t m = common; m; m &= (m - 1)) {
                            int w = __builtin_ctzll(m);
                            bool anyIn = false;
                            for (int nd = 0; nd < N; ++nd) {
                                if (nodeInsideTri(nd, fr, to, w)) { anyIn = true; break; }
                            }
                            if (!anyIn) {
                                newTris++;
                                quadMask |= (1ULL << w);
                            }
                        }
                        if (newTris < 2) continue; // Only build double-triangle quad diagonals!
                        if (quadMask & oppNowMask) continue; // Must be free of enemy units

                        int nearOppCorners = 0;
                        uint64_t supplyZone = quadMask;
                        for (uint64_t m = quadMask; m; m &= (m - 1)) {
                            int q = __builtin_ctzll(m);
                            if (adjMask[q] & oppNowMask) nearOppCorners++;
                            supplyZone |= initAdjMask[q];
                        }
                        if (nearOppCorners > 1) continue; // Keep new factories safe from enemy frontline

                        int localSupply = 0;
                        for (uint64_t m = supplyZone; m; m &= (m - 1)) {
                            int u = __builtin_ctzll(m);
                            localSupply += myUnits[u] - (t.hasNode(u) ? 1 : 0);
                        }
                        for (int k = 0; k < T_cnt; ++k) {
                            if (k != i && allTris[k].owner == 0 && (allTris[k].mask & quadMask)) localSupply++;
                        }
                        if (localSupply < 5) continue; // Ensure SA can immediately capture both new triangles!

                        int val = newTris * 1000 + min(9, localSupply) * 45 - dist2Nodes(fr, to) / 2000;
                        if (myUnits[t.h1] == 1 && myUnits[t.h2] == 1 && myUnits[t.h3] == 1) val += 150;
                        if (val > bestVal) {
                            bestVal = val;
                            bestTri = i;
                            bestFrom = fr;
                            bestTo = to;
                        }
                    }
                }
            }

            if (bestTri == -1) break;
            Tri& bt = allTris[bestTri];
            int n2 = -1, n3 = -1;
            for (int c : bt.c) {
                if (c == bestFrom) continue;
                if (n2 == -1) n2 = c; else n3 = c;
            }
            postMoveActions.push_back("ADD_PATH " + to_string(bestFrom) + " " + to_string(n2) + " " + to_string(n3) + " " + to_string(bestTo));
            myUnits[bt.h1]--;
            myUnits[bt.h2]--;
            myUnits[bt.h3]--;
            curMyTotal -= 3;
            curMyTris--;
            bt.owner = -1;
            bt.meCan = false;
            addGraphEdge(bestFrom, bestTo);
            rebuildTrianglesInBot();
            graphChanged = true;
        }

        // Priority 4: Tactical REMOVE_PATH (Surround-Kill Assassination or Double Enemy-Factory Cut)
        if (curTurn >= 4 && curMyTotal >= 18 && curMyTris >= 2) {
            int domNow = 0;
            while ((double)domNow < (double)T_cnt * 0.8) domNow++;
            int oppTrisNow = 0;
            for (int k = 0; k < T_cnt; ++k) if (allTris[k].owner == 1) oppTrisNow++;

            int bestTri = -1, bestFrom = -1, bestTo = -1, bestVal = 0;
            for (int i = 0; i < T_cnt; ++i) {
                const Tri& t = allTris[i];
                if (t.owner != 0) continue;
                if (myUnits[t.h1] < 1 || myUnits[t.h2] < 1 || myUnits[t.h3] < 1) continue;

                for (int cIdx = 0; cIdx < 3; ++cIdx) {
                    int fr = t.c[cIdx];
                    for (int to : adj[fr]) {
                        if (t.hasNode(to)) continue; // Must be external edge!

                        int myDel = 0, oppDel = 0;
                        for (int k = 0; k < T_cnt; ++k) {
                            if (allTris[k].hasNode(fr) && allTris[k].hasNode(to)) {
                                if (allTris[k].owner == 0) myDel++;
                                else if (allTris[k].owner == 1) oppDel++;
                            }
                        }
                        if (myDel > 0) continue;

                        int surrKill = 0;
                        for (int pass = 0; pass < 2; ++pass) {
                            int v = (pass == 0 ? to : fr);
                            int other = (pass == 0 ? fr : to);
                            if (oppUnits[v] >= 4 && adj[v].size() >= 2) {
                                int remOther = myUnits[other] - (t.hasNode(other) ? 1 : 0);
                                if (remOther > oppUnits[other]) continue;
                                bool allOtherHeld = true;
                                for (int nb : adj[v]) {
                                    if (nb == other) continue;
                                    int remNb = myUnits[nb] - (t.hasNode(nb) ? 1 : 0);
                                    int adjOpp = oppUnits[nb];
                                    for (int w : adj[nb]) if (w != v) adjOpp += oppUnits[w];
                                    if (remNb <= oppUnits[nb] || remNb < adjOpp) {
                                        allOtherHeld = false;
                                        break;
                                    }
                                }
                                if (allOtherHeld) surrKill += oppUnits[v];
                            }
                        }

                        bool worthCut = false;
                        int val = 0;
                        if (surrKill >= 4 && (surrKill >= 5 || curMyTotal >= 24)) {
                            worthCut = true;
                            val = surrKill * 1000 + oppDel * 350;
                        } else if (oppDel >= 2 && (oppTrisNow >= domNow - 4 ||
                                  (curMyTotal >= 46 && curMyTotal >= curOppTotal + 8 && curMyTris >= 4))) {
                            worthCut = true;
                            val = 2500 + oppDel * 500;
                        }

                        if (worthCut && val > bestVal) {
                            bestVal = val;
                            bestTri = i;
                            bestFrom = fr;
                            bestTo = to;
                        }
                    }
                }
            }

            if (bestTri != -1) {
                Tri& bt = allTris[bestTri];
                int n2 = -1, n3 = -1;
                for (int c : bt.c) {
                    if (c == bestFrom) continue;
                    if (n2 == -1) n2 = c; else n3 = c;
                }
                postMoveActions.push_back("REMOVE_PATH " + to_string(bestFrom) + " " + to_string(n2) + " " + to_string(n3) + " " + to_string(bestTo));
                myUnits[bt.h1]--;
                myUnits[bt.h2]--;
                myUnits[bt.h3]--;
                curMyTotal -= 3;
                curMyTris--;
                bt.owner = -1;
                bt.meCan = false;
                removeGraphEdge(bestFrom, bestTo);
                rebuildTrianglesInBot();
                graphChanged = true;
            }
        }

        if (graphChanged) {
            computeAllPairsBFS();
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

        double maxTimeMs = (curTurn == 1 ? 800.0 : 38.0);

        if (curTurn <= 2 && M_cnt == 3 && S_cnt == 0) {
            double openLimit = (curTurn == 1 ? 280.0 : 32.0);
            solveOpening3Units(turnStart, openLimit, bestDest, bestScore);
        } else if (comb <= 5000.0 && M_cnt < 64 && S_cnt < 64) {
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
                        int pocketPen = (adjMask[c] != 0 && (adjMask[c] & ~t.mask) == 0) ? 2000 :
                                        ((((interiorOwnedMask >> c) & 1ULL) && (t.mask & ~interiorOwnedMask) != 0) ? 1100 : 0);
                        int p = nodePotential[c] - (armedEnemyAtkZone[c] ? 800 : 0) - pocketPen;
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
            double saBudgetMs = (curTurn == 1 ? 750.0 : 34.0);
            int iter = 0;

            int savedIdx[64], savedVal[64], nSaved = 0;
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

                if (roll < 42 && M_cnt > 0) {
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
                } else if (roll < 61 && S_cnt > 0) {
                    // Op 2: Single spawn change
                    int j = rng.nextInt(S_cnt);
                    int cand = rng.nextInt(4) - 1;
                    if (cand == curSpawn[j]) continue;
                    savedSpIdx[nSavedSp] = j; savedSpVal[nSavedSp] = curSpawn[j]; nSavedSp++;
                    curSpawn[j] = cand;
                } else if (roll < 81 && C_cnt > 0 && (M_cnt + S_cnt) > 0) {
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
                            if (u == c || ((initAdjMask[u] >> c) & 1ULL)) reachCnt[k]++;
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
                        int req = (reinforce ? max(oppRobustUnits[corner], oppPredUnits[corner]) : oppUnits[corner]) + 1;
                        if (adjMask[corner] != 0 && (adjMask[corner] & ~t.mask) == 0 && oppUnits[corner] == 0) {
                            req = 0; // Degree-2 corner inside t is auto-owned when the other 2 corners are owned!
                        }
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
                                    if (u == corner || ((initAdjMask[u] >> corner) & 1ULL)) {
                                        if (nSaved < 60) {
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
                } else if (roll < 88 && R_cnt > 0) {
                    // Op 4: Coordinated meCan == 0 Triangle Evacuation Macro-Move!
                    const Tri& t = allTris[resetTriIdx[rng.nextInt(R_cnt)]];
                    for (int i = 0; i < M_cnt && nSaved < 60; ++i) {
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
                } else if (roll < 92 && M_cnt >= 2) {
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
                } else if (M_cnt >= 2) {
                    // Op 6: Multi-Unit Stack Breakout / Coordinated Assault Move!
                    int i0 = rng.nextInt(M_cnt);
                    int u = unitLocs[i0];
                    if (numMoveOpts[u] <= 1) continue;
                    int lo = i0, hi = i0;
                    while (lo > 0 && unitLocs[lo - 1] == u) lo--;
                    while (hi + 1 < M_cnt && unitLocs[hi + 1] == u) hi++;
                    int stackSz = hi - lo + 1;
                    if (stackSz < 2) continue;

                    int cand = moveOpts[u][rng.nextInt(numMoveOpts[u])];
                    int targetAmt = (rng.nextInt(2) == 0) ? stackSz : clamp(oppRobustUnits[cand] + 1, 2, stackSz);
                    int moved = 0;
                    for (int idx = lo; idx <= hi && moved < targetAmt && nSaved < 62; ++idx) {
                        if (curDest[idx] != cand) {
                            savedIdx[nSaved] = idx; savedVal[nSaved] = curDest[idx]; nSaved++;
                            curDest[idx] = cand;
                        }
                        moved++;
                    }
                    if (nSaved == 0) continue;
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

            // Final deterministic greedy polish with strict exit guard
            curDest = bestDest;
            curSpawn = bestSpawn;
            curScore = bestScore;
            for (int i = 0; i < M_cnt; ++i) {
                double elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                if (elapsed >= maxTimeMs) goto polish_done;
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
                if (elapsed >= maxTimeMs) goto polish_done;
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
        polish_done:
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
        for (int u = 0; u < N; ++u) {
            for (int v : initAdj[u]) {
                if (u < v) {
                    int cancel = min(moveCount[u][v], moveCount[v][u]);
                    if (cancel > 0) {
                        moveCount[u][v] -= cancel;
                        moveCount[v][u] -= cancel;
                    }
                }
            }
        }
        for (int fr = 0; fr < N; ++fr) {
            for (int v : initAdj[fr]) {
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

        // Emit Priority 3 & 4 Gold League commands (ADD_PATH / REMOVE_PATH)
        for (const auto& pma : postMoveActions) {
            actions.push_back(pma);
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
