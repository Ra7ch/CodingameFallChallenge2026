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
uint64_t rTriUnionMask;

int nodePotential[MAX_N];
int minCapDist[MAX_N];
int oppSupplyNear[MAX_N];
int K_cnt;
int surrTargetIdx[MAX_N];

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

// ============================================================================
// DOUBLE-ORACLE SIMULTANEOUS ZERO-SUM PORTFOLIO DATA STRUCTURES (v4.0)
// ============================================================================
struct OppResponse {
    int u1[MAX_N];
    int totalU1;
    uint64_t occ1Mask;
    uint64_t canSurrMeMask;
    bool held1[MAX_T]; // indexed by osIdx in [0..OS_cnt_global-1]
    bool isStatic;
    int nBlockCorners;
    int blockCorners[MAX_N];
};

OppResponse oppPortfolio[5];
int numOppResp = 0;

int O_cnt_global = 0;
int oppLocsGlobal[512];
int OS_cnt_global = 0, OC_cnt_global = 0, OR_cnt_global = 0;
int oppOwnedIdxGlobal[MAX_T], oppCapIdxGlobal[MAX_T], oppResetIdxGlobal[MAX_T];
int oppPotGlobal[MAX_N], oppMinDGlobal[MAX_N];
int oppSpawnGainGlobal[512];
int b0DestGlobal[512], b0SpGlobal[MAX_T];
int b1DestGlobal[512], b1SpGlobal[MAX_T];

// Flat L1-cached triangle bitmasks for evalPortfolio & buildAdversarialResponse
uint64_t sTriMask[MAX_T];
bool sTriOppCan[MAX_T];

uint64_t cTriMask[MAX_T];
int cTriOwner1OsIdx[MAX_T]; // -1 if neutral, else index in [0..OS_cnt_global-1]
bool cTriOppCan[MAX_T];

int ROpp_cnt = 0;
uint64_t rOppTriMask[MAX_T];

void finalizeOppResponse(OppResponse& br, const int* oDest, const int* oSp) {
    memset(br.u1, 0, N * sizeof(int));
    br.totalU1 = 0;
    br.occ1Mask = 0;
    br.isStatic = false;
    for (int i = 0; i < O_cnt_global; ++i) {
        int d = oDest[i];
        br.u1[d]++;
        br.totalU1++;
        br.occ1Mask |= (1ULL << d);
    }
    for (int j = 0; j < OS_cnt_global; ++j) {
        if (oSp[j] >= 0) {
            br.held1[j] = false;
            int spN = allTris[oppOwnedIdxGlobal[j]].c[oSp[j]];
            br.u1[spN]++;
            br.totalU1++;
            br.occ1Mask |= (1ULL << spN);
        } else {
            br.held1[j] = true;
        }
    }
    br.canSurrMeMask = 0;
    for (int u = 0; u < N; ++u) {
        if (adjMask[u] != 0 && (adjMask[u] & ~br.occ1Mask) == 0) {
            br.canSurrMeMask |= (1ULL << u);
        }
    }
    // Precompute corners where opponent threatens a triangle capture so Op 8 can tie-block!
    uint64_t seenCornerMask = 0;
    br.nBlockCorners = 0;
    for (int i = 0; i < T_cnt; ++i) {
        const Tri& t = allTris[i];
        if (!t.oppCan) continue;
        bool allCovered = true;
        for (int k = 0; k < 3; ++k) {
            int c = t.c[k];
            if (br.u1[c] == 0 && !((br.canSurrMeMask >> c) & 1ULL)) {
                allCovered = false;
                break;
            }
        }
        if (allCovered) {
            for (int k = 0; k < 3; ++k) {
                int c = t.c[k];
                if (br.u1[c] >= 1 && br.u1[c] <= 4 && !((seenCornerMask >> c) & 1ULL)) {
                    seenCornerMask |= (1ULL << c);
                    br.blockCorners[br.nBlockCorners++] = c;
                }
            }
        }
    }
}

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

void precomputeTurnMetrics(bool buildPortfolio = true) {
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

        // Robust frontline model: accounts for concentrated enemy strike groups stepping onto u
        int maxOppNb = 0;
        for (int v : adj[u]) maxOppNb = max(maxOppNb, oppUnits[v]);
        int rob = oppUnits[u];
        if (reach > 0) {
            rob = max(rob, min(reach, max(1, maxOppNb)));
        }
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
        uint64_t ringM = adjMask[u];
        uint64_t supZone = ringM;
        bool allReach = true;
        for (int v : adj[u]) {
            if (oppReach[v] == 0) { allReach = false; }
            supZone |= adjMask[v] | (1ULL << v);
        }
        int sup = 0;
        for (uint64_t sm = (supZone & oppHasUnitsMask); sm; sm &= (sm - 1)) {
            sup += oppUnits[__builtin_ctzll(sm)];
        }
        for (int ti = 0; ti < T_cnt; ++ti) {
            if (allTris[ti].owner == 1 && (allTris[ti].mask & ringM)) sup++;
        }
        oppSupplyNear[u] = sup;
        if (allReach && sup >= (int)adj[u].size()) {
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
    rTriUnionMask = 0;
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
            rTriUnionMask |= t.mask;
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
                if (!((pureEarMask >> c) & 1ULL)) {
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
    if (!buildPortfolio) return;

    // Populate flat triangle tables & construct B_static, B_0 (Standard), and B_1 (Aggressive)
    {
        O_cnt_global = 0;
        uint64_t myHasMask = 0;
        for (int u = 0; u < N; ++u) {
            if (myUnits[u] > 0) myHasMask |= (1ULL << u);
            for (int k = 0; k < oppUnits[u] && O_cnt_global < 512; ++k) {
                oppLocsGlobal[O_cnt_global++] = u;
            }
        }
        OS_cnt_global = OC_cnt_global = OR_cnt_global = 0;
        int tidToOsIdx[MAX_T];
        memset(tidToOsIdx, -1, sizeof(tidToOsIdx));
        for (int i = 0; i < T_cnt; ++i) {
            const Tri& t = allTris[i];
            if (t.owner == 1) {
                tidToOsIdx[i] = OS_cnt_global;
                oppOwnedIdxGlobal[OS_cnt_global++] = i;
            } else if (t.oppCan) {
                oppCapIdxGlobal[OC_cnt_global++] = i;
            } else if (t.owner == -1 && !t.oppCan) {
                oppResetIdxGlobal[OR_cnt_global++] = i;
            }
        }

        for (int j = 0; j < S_cnt; ++j) {
            const Tri& t = allTris[ownedTriIdx[j]];
            sTriMask[j] = t.mask;
            sTriOppCan[j] = t.oppCan;
        }
        for (int ci = 0; ci < C_cnt; ++ci) {
            int tid = capTriIdx[ci];
            const Tri& t = allTris[tid];
            cTriMask[ci] = t.mask;
            cTriOwner1OsIdx[ci] = tidToOsIdx[tid];
            cTriOppCan[ci] = t.oppCan;
        }
        ROpp_cnt = 0;
        for (int ri = 0; ri < R_cnt; ++ri) {
            const Tri& t = allTris[resetTriIdx[ri]];
            if (t.oppCan) {
                rOppTriMask[ROpp_cnt++] = t.mask;
            }
        }

        // [0] B_static: Start-of-turn statue opponent
        {
            OppResponse& bs = oppPortfolio[0];
            memcpy(bs.u1, oppUnits, N * sizeof(int));
            bs.totalU1 = totalOppUnitsGlobal;
            bs.occ1Mask = oppHasUnitsMask;
            bs.canSurrMeMask = possibleSelfSurrMask;
            for (int j = 0; j < OS_cnt_global; ++j) bs.held1[j] = true;
            bs.isStatic = true;
            bs.nBlockCorners = 0;
        }

        if (O_cnt_global == 0 && OS_cnt_global == 0) {
            memset(oppPredUnits, 0, sizeof(oppPredUnits));
            oppPredHasUnitsMask = 0;
            numOppResp = 1;
        } else {
            uint64_t oppSelfSurrMask = 0;
            for (int u = 0; u < N; ++u) {
                if (adjMask[u] != 0 && (adjMask[u] & ~myHasMask) == 0) {
                    oppSelfSurrMask |= (1ULL << u);
                }
            }
            int oppAggPot[MAX_N];
            bool myHoldThreat = (M_cnt >= 34 || S_cnt >= domThreshold - 5);
            for (int u = 0; u < N; ++u) {
                double pot = 0.0;
                double aggPot = 0.0;
                int bestD = 10;
                for (int ci = 0; ci < OC_cnt_global; ++ci) {
                    const Tri& t = allTris[oppCapIdxGlobal[ci]];
                    int d = min({distMat[u][t.h1], distMat[u][t.h2], distMat[u][t.h3]});
                    if (d < bestD) bestD = d;
                    if (d >= 12) continue;
                    double attr = (t.owner == 0 && myHoldThreat) ? 270.0 : 205.0;
                    double aggAttr = (t.owner == 0) ? 430.0 : ((t.mask & (myHasMask | ownedUnionMask)) ? 340.0 : 185.0);
                    int nearC = 0;
                    for (int c : t.c) {
                        bool hn = (oppUnits[c] > myUnits[c]);
                        if (!hn) {
                            for (int nb : adj[c]) if (oppUnits[nb] > 0) { hn = true; break; }
                        }
                        if (hn) nearC++;
                    }
                    attr += nearC * 55.0;
                    aggAttr += nearC * 55.0;
                    pot += attr * decay[d];
                    aggPot += aggAttr * decay[d];
                }
                if (adjMask[u] & myHasMask) aggPot += 180.0;
                oppMinDGlobal[u] = bestD;
                oppPotGlobal[u] = (int)llround(pot);
                oppAggPot[u] = (int)llround(aggPot);
            }

            int oppTarget = clamp(max(baseCap, M_cnt + 6), 48, 88);
            oppSpawnGainGlobal[0] = 0;
            for (int k = 1; k <= OS_cnt_global; ++k) {
                int uAfter = O_cnt_global + k;
                int g = (remTurns <= 12) ? min(1700, remTurns * 140) :
                        (uAfter <= 32 ? 5400 : (uAfter <= oppTarget ? 3400 : 950));
                oppSpawnGainGlobal[k] = oppSpawnGainGlobal[k - 1] + g;
            }

            auto buildHeuristicOpp = [&](const int* potArr, bool aggMode, double budgetMs, int* outDest, int* outSp) {
                auto t0 = chrono::high_resolution_clock::now();
                auto evalOppMode = [&](const int* oDest, const int* oSp) -> int64_t {
                    int u1[MAX_N];
                    memset(u1, 0, N * sizeof(int));
                    uint64_t oOcc = 0;
                    for (int i = 0; i < O_cnt_global; ++i) {
                        int d = oDest[i];
                        u1[d]++;
                        oOcc |= (1ULL << d);
                    }
                    int nSp = 0;
                    for (int j = 0; j < OS_cnt_global; ++j) {
                        if (oSp[j] >= 0) {
                            int spN = allTris[oppOwnedIdxGlobal[j]].c[oSp[j]];
                            u1[spN]++;
                            oOcc |= (1ULL << spN);
                            nSp++;
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
                            if (aggMode) {
                                kMe += myUnits[u];
                                kMeMask |= (1ULL << u);
                            } else {
                                int thin = 0;
                                for (int v : adj[u]) if (u1[v] <= myUnits[v] + 1) thin++;
                                if (thin == 0 || (myUnits[u] == 1 && thin == 1 && adj[u].size() >= 4)) {
                                    kMe += myUnits[u];
                                    kMeMask |= (1ULL << u);
                                }
                            }
                        }
                    }
                    if (kOppMask | kMeMask) {
                        a1 = (a1 & ~kOppMask) | (kMeMask & oOcc & ~kOppMask);
                        a0 = (a0 & ~kMeMask) | (kOppMask & myHasMask & ~kMeMask);
                    }
                    uint64_t ow1 = 0;
                    for (int u = 0; u < N; ++u) {
                        uint64_t am = adjMask[u];
                        bool o1 = false, o0 = false;
                        if (am != 0) {
                            if ((am & ~a1) == 0 && (aggMode || myUnits[u] == 0 || ((kMeMask >> u) & 1ULL))) o1 = true;
                            else if ((am & ~a0) == 0) o0 = true;
                        }
                        if (!o1 && !o0) o1 = (a1 >> u) & 1ULL;
                        if (o1) ow1 |= (1ULL << u);
                    }

                    int64_t sc = (int64_t)kMe * (aggMode ? 5200LL : 3400LL) - (int64_t)kOpp * 5500LL;
                    sc += oppSpawnGainGlobal[nSp];
                    int totOw = 0;
                    for (int j = 0; j < OS_cnt_global; ++j) {
                        if (oSp[j] == -1) { totOw++; sc += 2200; }
                    }
                    for (int ci = 0; ci < OC_cnt_global; ++ci) {
                        const Tri& t = allTris[oppCapIdxGlobal[ci]];
                        uint64_t m = ow1 & t.mask;
                        if (m == t.mask) {
                            totOw++;
                            sc += 4500 + (t.owner == 0 ? (aggMode ? 1600 : 500) : 0);
                        } else {
                            int cnt = __builtin_popcountll(m);
                            if (cnt == 2) sc += 300;
                            else if (cnt == 1) sc += 55;
                        }
                    }
                    for (int ri = 0; ri < OR_cnt_global; ++ri) {
                        const Tri& t = allTris[oppResetIdxGlobal[ri]];
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
                        sc += (int64_t)useful * potArr[u] - (int64_t)cnt * oppMinDGlobal[u] * 85LL;
                        if (extra > 0) {
                            sc += ((int64_t)extra * potArr[u]) >> 2;
                            sc -= (int64_t)extra * extra * 65LL;
                        }
                    }
                    return sc;
                };

                for (int i = 0; i < O_cnt_global; ++i) outDest[i] = oppLocsGlobal[i];
                for (int j = 0; j < OS_cnt_global; ++j) {
                    if (O_cnt_global + j < oppTarget) {
                        const Tri& t = allTris[oppOwnedIdxGlobal[j]];
                        int bestC = 0, bestP = -999999;
                        for (int k = 0; k < 3; ++k) {
                            int c = t.c[k];
                            int pen = (adjMask[c] != 0 && (adjMask[c] & ~t.mask) == 0) ? 1800 : 0;
                            int p = potArr[c] - pen;
                            if (p > bestP) { bestP = p; bestC = k; }
                        }
                        outSp[j] = bestC;
                    } else {
                        outSp[j] = -1;
                    }
                }
                int64_t bestOppSc = evalOppMode(outDest, outSp);

                bool lockO[512] = {false};
                bool lockOS[MAX_T] = {false};

                // In aggMode, first test surrounding reachable start-of-turn P0 stacks!
                if (aggMode) {
                    int myStks[MAX_N], nStks = 0;
                    for (int u = 0; u < N; ++u) {
                        if (myUnits[u] > 0 && adj[u].size() >= 1 && adj[u].size() <= 5) myStks[nStks++] = u;
                    }
                    sort(myStks, myStks + nStks, [&](int a, int b) { return myUnits[a] > myUnits[b]; });
                    for (int si = 0; si < nStks; ++si) {
                        int tgt = myStks[si];
                        int svIdx[24], svVal[24], nSv = 0;
                        int svSpIdx[12], svSpVal[12], nSvSp = 0;
                        int tmpOList[32], nTmpO = 0;
                        int tmpSList[16], nTmpS = 0;
                        bool okAll = true;
                        for (int v : adj[tgt]) {
                            int req = myUnits[v] + 1;
                            int asg = 0;
                            for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                                if (!lockO[i] && outDest[i] == v && nTmpO < 32) {
                                    lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                                }
                            }
                            for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                                if (!lockOS[j] && outSp[j] >= 0 && allTris[oppOwnedIdxGlobal[j]].c[outSp[j]] == v && nTmpS < 16) {
                                    lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                                }
                            }
                            for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                                if (lockO[i]) continue;
                                int u = oppLocsGlobal[i];
                                if ((u == v || ((initAdjMask[u] >> v) & 1ULL)) && nSv < 24 && nTmpO < 32) {
                                    svIdx[nSv] = i; svVal[nSv] = outDest[i]; nSv++;
                                    outDest[i] = v;
                                    lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                                }
                            }
                            for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                                if (lockOS[j]) continue;
                                const Tri& ot = allTris[oppOwnedIdxGlobal[j]];
                                for (int sc = 0; sc < 3; ++sc) {
                                    if (ot.c[sc] == v && nSvSp < 12 && nTmpS < 16) {
                                        svSpIdx[nSvSp] = j; svSpVal[nSvSp] = outSp[j]; nSvSp++;
                                        outSp[j] = sc;
                                        lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                                        break;
                                    }
                                }
                            }
                            if (asg < req) { okAll = false; break; }
                        }
                        if (okAll && (nSv > 0 || nSvSp > 0)) {
                            int64_t sc = evalOppMode(outDest, outSp);
                            if (sc > bestOppSc) {
                                bestOppSc = sc;
                                continue;
                            }
                        }
                        for (int k = nSv - 1; k >= 0; --k) outDest[svIdx[k]] = svVal[k];
                        for (int k = nSvSp - 1; k >= 0; --k) outSp[svSpIdx[k]] = svSpVal[k];
                        for (int k = 0; k < nTmpO; ++k) lockO[tmpOList[k]] = false;
                        for (int k = 0; k < nTmpS; ++k) lockOS[tmpSList[k]] = false;
                    }
                }

                // Greedy 3-corner triangle capture pass for opponent
                int sortedOC[MAX_T];
                for (int ci = 0; ci < OC_cnt_global; ++ci) sortedOC[ci] = oppCapIdxGlobal[ci];
                sort(sortedOC, sortedOC + OC_cnt_global, [&](int a, int b) {
                    const Tri& ta = allTris[a]; const Tri& tb = allTris[b];
                    int bonusA = (aggMode && ta.owner == 0) ? 1200 : 0;
                    int bonusB = (aggMode && tb.owner == 0) ? 1200 : 0;
                    return (potArr[ta.h1] + potArr[ta.h2] + potArr[ta.h3] + bonusA) >
                           (potArr[tb.h1] + potArr[tb.h2] + potArr[tb.h3] + bonusB);
                });
                for (int ci = 0; ci < OC_cnt_global; ++ci) {
                    if ((ci & 7) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() >= budgetMs) break;
                    const Tri& t = allTris[sortedOC[ci]];
                    int svIdx[16], svVal[16], nSv = 0;
                    int svSpIdx[8], svSpVal[8], nSvSp = 0;
                    int tmpOList[32], nTmpO = 0;
                    int tmpSList[16], nTmpS = 0;
                    bool okAll = true;
                    for (int k = 0; k < 3; ++k) {
                        int corner = t.c[k];
                        int req = myUnits[corner] + 1;
                        if (adjMask[corner] != 0 && (adjMask[corner] & ~t.mask) == 0 && myUnits[corner] == 0) req = 0;
                        int asg = 0;
                        for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                            if (!lockO[i] && outDest[i] == corner && nTmpO < 32) {
                                lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                            }
                        }
                        for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                            if (!lockOS[j] && outSp[j] >= 0 && allTris[oppOwnedIdxGlobal[j]].c[outSp[j]] == corner && nTmpS < 16) {
                                lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                            }
                        }
                        for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                            if (lockO[i]) continue;
                            int u = oppLocsGlobal[i];
                            if ((u == corner || ((initAdjMask[u] >> corner) & 1ULL)) && nSv < 16 && nTmpO < 32) {
                                svIdx[nSv] = i; svVal[nSv] = outDest[i]; nSv++;
                                outDest[i] = corner;
                                lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                            }
                        }
                        for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                            if (lockOS[j]) continue;
                            const Tri& ot = allTris[oppOwnedIdxGlobal[j]];
                            for (int sc = 0; sc < 3; ++sc) {
                                if (ot.c[sc] == corner && nSvSp < 8 && nTmpS < 16) {
                                    svSpIdx[nSvSp] = j; svSpVal[nSvSp] = outSp[j]; nSvSp++;
                                    outSp[j] = sc;
                                    lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                                    break;
                                }
                            }
                        }
                        if (asg < req) { okAll = false; break; }
                    }
                    if (okAll && (nSv > 0 || nSvSp > 0)) {
                        int64_t sc = evalOppMode(outDest, outSp);
                        if (sc > bestOppSc) {
                            bestOppSc = sc;
                            continue;
                        }
                    }
                    for (int k = nSv - 1; k >= 0; --k) outDest[svIdx[k]] = svVal[k];
                    for (int k = nSvSp - 1; k >= 0; --k) outSp[svSpIdx[k]] = svSpVal[k];
                    for (int k = 0; k < nTmpO; ++k) lockO[tmpOList[k]] = false;
                    for (int k = 0; k < nTmpS; ++k) lockOS[tmpSList[k]] = false;
                }

                // Single fast coordinate-ascent refinement pass
                if (chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() < budgetMs) {
                    for (int i = 0; i < O_cnt_global; ++i) {
                        if ((i & 15) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() >= budgetMs) break;
                        int u = oppLocsGlobal[i];
                        int orig = outDest[i], bestOpt = orig;
                        for (int k = 0; k < numMoveOpts[u]; ++k) {
                            int cand = moveOpts[u][k];
                            if (cand == orig) continue;
                            outDest[i] = cand;
                            int64_t sc = evalOppMode(outDest, outSp);
                            if (sc > bestOppSc) { bestOppSc = sc; bestOpt = cand; }
                        }
                        outDest[i] = bestOpt;
                    }
                    for (int j = 0; j < OS_cnt_global; ++j) {
                        if ((j & 7) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() >= budgetMs) break;
                        int orig = outSp[j], bestOpt = orig;
                        for (int cand = -1; cand < 3; ++cand) {
                            if (cand == orig) continue;
                            outSp[j] = cand;
                            int64_t sc = evalOppMode(outDest, outSp);
                            if (sc > bestOppSc) { bestOppSc = sc; bestOpt = cand; }
                        }
                        outSp[j] = bestOpt;
                    }
                }
            };

            double eachBudget = (curTurn == 1 ? 8.0 : 0.60);
            // [1] B_0: Standard expansion/economy opponent
            buildHeuristicOpp(oppPotGlobal, false, eachBudget, b0DestGlobal, b0SpGlobal);
            finalizeOppResponse(oppPortfolio[1], b0DestGlobal, b0SpGlobal);

            // Populate oppPredUnits from B_0
            memcpy(oppPredUnits, oppPortfolio[1].u1, N * sizeof(int));
            oppPredHasUnitsMask = oppPortfolio[1].occ1Mask;

            // [2] B_1: Aggressive frontline/steal/surround opponent
            buildHeuristicOpp(oppAggPot, true, eachBudget, b1DestGlobal, b1SpGlobal);
            finalizeOppResponse(oppPortfolio[2], b1DestGlobal, b1SpGlobal);

            numOppResp = 3;
        }
    }

    // Precompute candidate enemy nodes u for Op 7 Coordinated Surround-Kill Macro-Move
    K_cnt = 0;
    for (int u = 0; u < N; ++u) {
        int deg = (int)adj[u].size();
        if (deg < 2 || deg > 6) continue;
        if (oppUnits[u] == 0 && oppPredUnits[u] < 2) continue;
        uint64_t ringM = adjMask[u];
        uint64_t mySupZone = ringM;
        int reqSum = 0;
        bool eachReachable = true;
        for (int v : adj[u]) {
            int rv = (oppUnits[u] > 0) ? (oppUnits[v] + 2) : (max(oppUnits[v], oppPredUnits[v]) + 1);
            reqSum += rv;
            int rCnt = myUnits[v];
            for (int nb : initAdj[v]) rCnt += myUnits[nb];
            for (int j = 0; j < S_cnt; ++j) if (allTris[ownedTriIdx[j]].hasNode(v)) rCnt++;
            if (rCnt < rv) { eachReachable = false; break; }
            mySupZone |= initAdjMask[v] | (1ULL << v);
        }
        if (!eachReachable) continue;
        int totMySup = 0;
        for (int w = 0; w < N; ++w) {
            if ((mySupZone >> w) & 1ULL) totMySup += myUnits[w];
        }
        for (int j = 0; j < S_cnt; ++j) {
            if (allTris[ownedTriIdx[j]].mask & ringM) totMySup++;
        }
        if (totMySup >= reqSum) {
            surrTargetIdx[K_cnt++] = u;
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
            if (thinNbrs == 0 || (oppUnits[u] == 1 && thinNbrs == 1 && adj[u].size() >= 3)) {
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
    for (uint64_t m = (oppPredHasUnitsMask & ~oppHasUnitsMask); m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (adjMask[u] != 0 && (adjMask[u] & ~adv0) == 0 && (adjMask[u] & ~adv0_pred) == 0) {
            oppContainedBonus += min(3, oppPredUnits[u]) * 1250;
        }
    }

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
                    // Contested frontline hold: do not penalize holding our factory!
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
        if ((pureEarMask >> u) & 1ULL) {
            score -= (int64_t)cnt * (oppUnits[u] > 0 ? 1350LL : 950LL);
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

        // 1-ply predicted surround ("Matador" bait-and-step-off) & exact conservation trap guard!
        if (adjMask[u] != 0 && (adjMask[u] & ~adv1_pred) == 0) {
            score -= (int64_t)cnt * 3600LL;
        } else if ((canBeSurr1StepMask >> u) & 1ULL) {
            bool impossibleToSurr = false;
            int reqEnemy = 0;
            for (int v : adj[u]) {
                int uv = ((killedMyMask >> v) & 1ULL) ? 0 : u0[v];
                if (uv >= oppReach[v]) {
                    impossibleToSurr = true;
                    break;
                }
                reqEnemy += uv + 1;
            }
            if (!impossibleToSurr && oppSupplyNear[u] >= reqEnemy) {
                int predMiss = __builtin_popcountll(adjMask[u] & ~adv1_pred);
                if (((degLe2Mask >> u) & 1ULL) || (adjMask[u] & ~adv1_rob) == 0 || predMiss <= 1) {
                    score -= (int64_t)cnt * 2250LL;
                } else if (adj[u].size() == 3 && oppSupplyNear[u] >= reqEnemy + 1) {
                    score -= (int64_t)cnt * 1600LL;
                } else if (oppSupplyNear[u] >= reqEnemy + 2) {
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

// ============================================================================
// DOUBLE-ORACLE SIMULTANEOUS PORTFOLIO EVALUATOR & ADVERSARIAL ORACLE (v4.0)
// ============================================================================
int64_t evalPortfolio(const int* __restrict__ dest, const int* __restrict__ spawnChoice) {
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

    int totalMyStart = M_cnt + numSpawns;

    // Shared 1-time terms: spawn economy, spawn pocket discipline, R_cnt evacuation
    int64_t sharedScore = marginalSpawnGain[numSpawns] + stayCount * 3;
    for (int j = 0; j < S_cnt; ++j) {
        int sc = spawnChoice[j];
        if (sc >= 0) {
            const Tri& t = allTris[ownedTriIdx[j]];
            int spNode = t.c[sc];
            if (adjMask[spNode] != 0 && (adjMask[spNode] & ~t.mask) == 0) {
                sharedScore -= 1800;
            } else if (((interiorOwnedMask >> spNode) & 1ULL) && (t.mask & ~interiorOwnedMask) != 0) {
                sharedScore -= 1100;
            }
            int occ = (u0[t.h1] > 0) + (u0[t.h2] > 0) + (u0[t.h3] > 0);
            sharedScore -= (occ - 1) * 320;
            if ((rTriUnionMask >> spNode) & 1ULL) sharedScore -= 240;
        }
    }

    for (int ri = 0; ri < R_cnt; ++ri) {
        int tid = resetTriIdx[ri];
        const Tri& t = allTris[tid];
        if ((myOccMask & t.mask) == 0) {
            uint64_t extRing = triNbrUnion[tid] & ~t.mask;
            int poised = 0;
            for (uint64_t pm = (myOccMask & extRing); pm && poised < 3; pm &= (pm - 1)) {
                poised += u0[__builtin_ctzll(pm)];
            }
            sharedScore += 1350 + min(3, poised) * 260;
        } else {
            int occ = (u0[t.h1] > 0) + (u0[t.h2] > 0) + (u0[t.h3] > 0);
            int totU = u0[t.h1] + u0[t.h2] + u0[t.h3];
            sharedScore -= 420 + occ * 280 + min(4, totU) * 80;
        }
    }

    // Evaluate against each opponent response in oppPortfolio[0..numOppResp-1]
    int64_t respScores[5];
    uint64_t k0MaskStatic = 0;
    uint64_t k1MaskStatic = 0;
    uint64_t owned0_rob = 0;

    for (int r = 0; r < numOppResp; ++r) {
        const OppResponse& br = oppPortfolio[r];
        uint64_t a0 = 0, a1 = 0;
        for (uint64_t m = myOccMask; m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (u0[u] > br.u1[u]) a0 |= (1ULL << u);
        }
        for (uint64_t m = br.occ1Mask; m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (br.u1[u] > u0[u]) a1 |= (1ULL << u);
        }

        int k0 = 0, k1 = 0;
        int containedBonus = 0;
        uint64_t k0Mask = 0, k1Mask = 0;

        for (uint64_t m = (myOccMask & br.canSurrMeMask); m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if ((adjMask[u] & ~a1) == 0) {
                k0 += u0[u];
                k0Mask |= (1ULL << u);
            }
        }

        if (br.isStatic) {
            for (uint64_t m = br.occ1Mask; m; m &= (m - 1)) {
                int u = __builtin_ctzll(m);
                if (adjMask[u] != 0 && (adjMask[u] & ~a0) == 0) {
                    int thinNbrs = 0;
                    for (int v : adj[u]) {
                        if (u0[v] <= br.u1[v] + 1) thinNbrs++;
                    }
                    if (thinNbrs == 0 || (br.u1[u] == 1 && thinNbrs == 1 && adj[u].size() >= 3)) {
                        k1 += br.u1[u];
                        k1Mask |= (1ULL << u);
                    } else {
                        containedBonus += 250;
                    }
                }
            }
        } else {
            for (uint64_t m = br.occ1Mask; m; m &= (m - 1)) {
                int u = __builtin_ctzll(m);
                if (adjMask[u] != 0 && (adjMask[u] & ~a0) == 0) {
                    k1 += br.u1[u];
                    k1Mask |= (1ULL << u);
                }
            }
        }

        if (k0Mask | k1Mask) {
            a0 = (a0 & ~k0Mask) | (k1Mask & myOccMask & ~k0Mask);
            a1 = (a1 & ~k1Mask) | (k0Mask & br.occ1Mask & ~k1Mask);
        }

        uint64_t ow0 = 0, ow1 = 0;
        if (br.isStatic) {
            k0MaskStatic = k0Mask;
            k1MaskStatic = k1Mask;
            uint64_t adv0_rob = 0, adv1_rob = 0;
            for (uint64_t m = (myOccMask & ~k0Mask); m; m &= (m - 1)) {
                int u = __builtin_ctzll(m);
                if (u0[u] > oppRobustUnits[u]) adv0_rob |= (1ULL << u);
            }
            for (uint64_t m = (oppRobustHasUnitsMask & ~k1Mask); m; m &= (m - 1)) {
                int u = __builtin_ctzll(m);
                int u0_eff = ((k0Mask >> u) & 1ULL) ? 0 : u0[u];
                if (oppRobustUnits[u] > u0_eff) adv1_rob |= (1ULL << u);
            }
            for (int u = 0; u < N; ++u) {
                uint64_t am = adjMask[u];
                bool o0 = false, o1 = false;
                if (am != 0) {
                    if ((am & ~a0) == 0) {
                        if (((k1Mask >> u) & 1ULL) != 0) o0 = true;
                        else if (br.u1[u] == 0) {
                            if (u0[u] > 0 || __builtin_popcountll(am) <= 2 || oppReach[u] == 0 || (am & ~adv0_rob) == 0) {
                                o0 = true;
                            }
                        }
                    } else if ((am & ~a1) == 0) {
                        o1 = true;
                    }
                }
                if (!o0 && !o1) {
                    o0 = (a0 >> u) & 1ULL;
                    o1 = (a1 >> u) & 1ULL;
                }
                if (o0) ow0 |= (1ULL << u);
                else if (o1) ow1 |= (1ULL << u);

                bool or0 = false, or1 = false;
                if (am != 0) {
                    if ((am & ~adv0_rob) == 0 && (br.u1[u] == 0 || ((k1Mask >> u) & 1ULL))) or0 = true;
                    else if ((am & ~adv1_rob) == 0) or1 = true;
                }
                if (!or0 && !or1) or0 = (adv0_rob >> u) & 1ULL;
                if (or0) owned0_rob |= (1ULL << u);
            }
        } else {
            // Exact Node.java getOwner() for simultaneous moving opponent response!
            for (int u = 0; u < N; ++u) {
                uint64_t am = adjMask[u];
                if (am != 0) {
                    if ((am & ~a0) == 0) { ow0 |= (1ULL << u); continue; }
                    if ((am & ~a1) == 0) { ow1 |= (1ULL << u); continue; }
                }
            }
            uint64_t undecided = ~(ow0 | ow1);
            ow0 |= (a0 & undecided);
            ow1 |= (a1 & undecided);
        }

        int64_t sc = containedBonus + (int64_t)k1 * 3800LL - (int64_t)k0 * 6000LL;
        int myTrisEnd = 0;
        int oppTrisEnd = 0;

        // 1. S_cnt (t.owner == 0 at start of turn)
        for (int j = 0; j < S_cnt; ++j) {
            uint64_t m = sTriMask[j];
            bool oppCaps = sTriOppCan[j] && ((ow1 & m) == m);
            if (spawnChoice[j] == -1) {
                if (!oppCaps) {
                    myTrisEnd++;
                    sc += 2200;
                } else {
                    oppTrisEnd++;
                    sc -= 4100;
                }
            } else {
                // We spawned from t -> t is now neutral (oppCan == sTriOppCan[j])!
                if (oppCaps) {
                    oppTrisEnd++;
                    sc -= 4100;
                }
            }
        }

        // 2. C_cnt (t.owner != 0 && t.meCan == 1, includes all t.owner == 1 triangles)
        for (int ci = 0; ci < C_cnt; ++ci) {
            uint64_t m = cTriMask[ci];
            int osIdx = cTriOwner1OsIdx[ci];
            if ((ow0 & m) == m) {
                myTrisEnd++;
                sc += 4750;
                if (br.isStatic) {
                    int robCnt = __builtin_popcountll(owned0_rob & m);
                    sc -= (3 - robCnt) * 400;
                }
                if (osIdx >= 0 && oppHoldMode) {
                    sc += 450;
                }
            } else {
                if (osIdx >= 0) {
                    if (br.held1[osIdx]) {
                        oppTrisEnd++;
                        sc -= 2200;
                    }
                } else if (cTriOppCan[ci] && ((ow1 & m) == m)) {
                    oppTrisEnd++;
                    sc -= 4100;
                }
                int cnt = __builtin_popcountll(ow0 & m);
                if (cnt == 2) {
                    sc += 240;
                    int miss = __builtin_ctzll(m & ~ow0);
                    if (adjMask[miss] & myOccMask) sc += 200;
                } else if (cnt == 1) {
                    sc += 55;
                }
            }
        }

        // 3. ROpp_cnt (t.owner == -1 && !t.meCan && t.oppCan == 1)
        for (int ri = 0; ri < ROpp_cnt; ++ri) {
            uint64_t m = rOppTriMask[ri];
            if ((ow1 & m) == m) {
                oppTrisEnd++;
                sc -= 4100;
            }
        }

        // 4. Exact Referee.java Instant Win / Instant Loss Terminal Conditions
        int myAlive = totalMyStart - k0;
        int oppAlive = br.totalU1 - k1;
        if (myAlive == 0 && myTrisEnd == 0) {
            sc -= 300000000LL;
        } else if (myAlive == 1 && myTrisEnd == 0 && (myScoreGlobal + myTrisEnd) < (oppScoreGlobal + oppTrisEnd)) {
            sc -= 180000000LL;
        } else if (oppTrisEnd >= domThreshold) {
            sc -= 120000000LL + (int64_t)oppTrisEnd * 20000LL;
        } else if (myTrisEnd >= domThreshold) {
            sc += 95000000LL + (int64_t)myTrisEnd * 30000LL + myAlive * 100LL;
        } else if (!br.isStatic && oppAlive == 0 && oppTrisEnd == 0) {
            sc += 150000000LL;
        } else if (!br.isStatic && oppAlive == 1 && oppTrisEnd == 0 && (oppScoreGlobal + oppTrisEnd) < (myScoreGlobal + myTrisEnd)) {
            sc += 90000000LL;
        }

        respScores[r] = sc;
    }

    // Surgical guard against spawning directly onto already-surrounded nodes (e.g. 32+1 -> 32)
    if (k0MaskStatic != 0) {
        for (int j = 0; j < S_cnt; ++j) {
            int sc = spawnChoice[j];
            if (sc >= 0) {
                int spNode = allTris[ownedTriIdx[j]].c[sc];
                if ((k0MaskStatic >> spNode) & 1ULL) {
                    sharedScore -= 9000LL;
                }
            }
        }
    }

    // Dynamic enemy ATTACK zone mask & occupied-node positional discipline (computed ONCE!)
    uint64_t activeArmedMask = 0, activePotMask = 0;
    for (int ei = 0; ei < E_cnt; ++ei) {
        const EnemyAtkTri& et = eatkTris[ei];
        if (et.ownedNow) {
            if ((owned0_rob & et.mask) != et.mask) {
                if (et.armedNow && (k1MaskStatic & et.mask) == 0) activeArmedMask |= et.zMask;
                else activePotMask |= et.zMask;
            }
        } else {
            bool weBlock = false;
            int cs[3] = {et.c0, et.c1, et.c2};
            for (int k = 0; k < 3; ++k) {
                int c = cs[k];
                int uAlive = ((k0MaskStatic >> c) & 1ULL) ? 0 : u0[c];
                if (((owned0_rob >> c) & 1ULL) && uAlive >= oppReach[c]) {
                    weBlock = true;
                    break;
                }
            }
            if (!weBlock) activePotMask |= et.zMask;
        }
    }

    uint64_t aliveOccMask = myOccMask & ~k0MaskStatic;
    for (uint64_t m = aliveOccMask; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        int cnt = u0[u];
        int need = (oppReach[u] > 0 ? max(oppUnits[u], oppRobustUnits[u]) : oppUnits[u]) + 1;
        int useful = min(cnt, need);
        int extra = cnt - useful;

        sharedScore -= (int64_t)cnt * minCapDist[u] * 85LL;
        sharedScore += (int64_t)useful * nodePotential[u];
        if (extra > 0) {
            if (oppReach[u] > 0) {
                int safeShield = max(1, oppReach[u] / 2);
                if (extra <= safeShield) {
                    sharedScore += ((int64_t)extra * nodePotential[u]) >> 1;
                    sharedScore -= (int64_t)extra * 18LL;
                } else {
                    int excess = extra - safeShield;
                    sharedScore += ((int64_t)extra * nodePotential[u]) >> 2;
                    sharedScore -= (int64_t)extra * 18LL + (int64_t)excess * excess * 40LL;
                }
            } else {
                // Peaceful backline node: penalize stacking multiple units so units push forward to the frontier!
                sharedScore -= (int64_t)extra * 320LL;
                sharedScore -= (int64_t)extra * extra * 75LL;
            }
        }

        if ((pureEarMask >> u) & 1ULL) {
            sharedScore -= (int64_t)cnt * (oppUnits[u] > 0 ? 1350LL : 950LL);
        } else if (((interiorOwnedMask >> u) & 1ULL) && oppUnits[u] == 0) {
            sharedScore -= (int64_t)cnt * 450LL;
        }

        if (cnt >= 3) {
            if ((activeArmedMask >> u) & 1ULL) {
                sharedScore -= (cnt == 3 ? 800LL : 2600LL + (int64_t)(cnt - 3) * 5000LL);
            } else if ((activePotMask >> u) & 1ULL) {
                sharedScore -= (cnt == 3 ? 450LL : 2000LL + (int64_t)(cnt - 3) * 4400LL);
            }
        }

        // Light 0.35x conservation surround tie-breaker (prevents jumping into un-sampled traps!)
        if ((canBeSurr1StepMask >> u) & 1ULL) {
            bool impossibleToSurr = false;
            int reqEnemy = 0;
            for (int v : adj[u]) {
                int uv = ((k0MaskStatic >> v) & 1ULL) ? 0 : u0[v];
                if (uv >= oppReach[v]) { impossibleToSurr = true; break; }
                reqEnemy += uv + 1;
            }
            if (!impossibleToSurr && oppSupplyNear[u] >= reqEnemy) {
                if (((degLe2Mask >> u) & 1ULL) || oppSupplyNear[u] >= reqEnemy + 2) {
                    sharedScore -= (int64_t)cnt * 750LL;
                } else {
                    sharedScore -= (int64_t)cnt * 420LL;
                }
            }
        }
    }

    if (numOppResp == 1) {
        return sharedScore + respScores[0];
    } else if (numOppResp == 3) {
        int64_t scMin = min(respScores[1], respScores[2]);
        return sharedScore + (20LL * respScores[0] + 35LL * respScores[1] + 25LL * respScores[2] + 20LL * scMin) / 100LL;
    } else if (numOppResp == 4) {
        int64_t scMin = min({respScores[1], respScores[2], respScores[3]});
        return sharedScore + (15LL * respScores[0] + 25LL * respScores[1] + 15LL * respScores[2] + 20LL * respScores[3] + 25LL * scMin) / 100LL;
    } else {
        int64_t scMin = min({respScores[1], respScores[2], respScores[3], respScores[4]});
        return sharedScore + (15LL * respScores[0] + 20LL * respScores[1] + 15LL * respScores[2] + 15LL * respScores[3] + 15LL * respScores[4] + 20LL * scMin) / 100LL;
    }
}

// Mid-Turn Adversarial Best-Response Oracle: finds simultaneous P1 action maximizing P1 payoff vs (p0Dest, p0Spawn)!
void buildAdversarialResponse(const int* p0Dest, const int* p0Spawn, double budgetMs, OppResponse& outBr) {
    auto t0 = chrono::high_resolution_clock::now();
    int u0Star[MAX_N];
    memset(u0Star, 0, N * sizeof(int));
    uint64_t myOccStar = 0;
    int totalP0 = M_cnt;
    for (int i = 0; i < M_cnt; ++i) {
        int d = p0Dest[i];
        u0Star[d]++;
        myOccStar |= (1ULL << d);
    }
    for (int j = 0; j < S_cnt; ++j) {
        int sc = p0Spawn[j];
        if (sc >= 0) {
            int spN = allTris[ownedTriIdx[j]].c[sc];
            u0Star[spN]++;
            myOccStar |= (1ULL << spN);
            totalP0++;
        }
    }

    uint64_t oppSelfSurrStar = 0;
    for (int u = 0; u < N; ++u) {
        if (adjMask[u] != 0 && (adjMask[u] & ~myOccStar) == 0) oppSelfSurrStar |= (1ULL << u);
    }

    auto evalP1Adv = [&](const int* oDest, const int* oSp) -> int64_t {
        int u1[MAX_N];
        memset(u1, 0, N * sizeof(int));
        uint64_t oOcc = 0;
        for (int i = 0; i < O_cnt_global; ++i) {
            int d = oDest[i];
            u1[d]++;
            oOcc |= (1ULL << d);
        }
        int nSp = 0;
        for (int j = 0; j < OS_cnt_global; ++j) {
            if (oSp[j] >= 0) {
                int spN = allTris[oppOwnedIdxGlobal[j]].c[oSp[j]];
                u1[spN]++;
                oOcc |= (1ULL << spN);
                nSp++;
            }
        }

        uint64_t a0 = 0, a1 = 0;
        for (uint64_t m = myOccStar; m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (u0Star[u] > u1[u]) a0 |= (1ULL << u);
        }
        for (uint64_t m = oOcc; m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (u1[u] > u0Star[u]) a1 |= (1ULL << u);
        }

        int k0 = 0, k1 = 0;
        uint64_t k0Mask = 0, k1Mask = 0;
        for (uint64_t m = myOccStar; m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if (adjMask[u] != 0 && (adjMask[u] & ~a1) == 0) {
                k0 += u0Star[u];
                k0Mask |= (1ULL << u);
            }
        }
        for (uint64_t m = (oOcc & oppSelfSurrStar); m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            if ((adjMask[u] & ~a0) == 0) {
                k1 += u1[u];
                k1Mask |= (1ULL << u);
            }
        }
        if (k0Mask | k1Mask) {
            a0 = (a0 & ~k0Mask) | (k1Mask & myOccStar & ~k0Mask);
            a1 = (a1 & ~k1Mask) | (k0Mask & oOcc & ~k1Mask);
        }

        uint64_t ow0 = 0, ow1 = 0;
        for (int u = 0; u < N; ++u) {
            uint64_t am = adjMask[u];
            if (am != 0) {
                if ((am & ~a0) == 0) { ow0 |= (1ULL << u); continue; }
                if ((am & ~a1) == 0) { ow1 |= (1ULL << u); continue; }
            }
        }
        uint64_t undecided = ~(ow0 | ow1);
        ow0 |= (a0 & undecided);
        ow1 |= (a1 & undecided);

        int64_t sc = oppSpawnGainGlobal[nSp] + (int64_t)k0 * 6000LL - (int64_t)k1 * 5600LL;
        int p0Tris = 0, p1Tris = 0;

        for (int j = 0; j < S_cnt; ++j) {
            uint64_t m = sTriMask[j];
            bool oppCaps = sTriOppCan[j] && ((ow1 & m) == m);
            if (p0Spawn[j] == -1) {
                if (!oppCaps) { p0Tris++; sc -= 2200; }
                else { p1Tris++; sc += 4500; }
            } else if (oppCaps) {
                p1Tris++;
                sc += 4300;
            }
        }
        for (int ci = 0; ci < C_cnt; ++ci) {
            uint64_t m = cTriMask[ci];
            int osIdx = cTriOwner1OsIdx[ci];
            if ((ow0 & m) == m) {
                p0Tris++;
                sc -= 4750;
            } else {
                if (osIdx >= 0) {
                    if (oSp[osIdx] == -1) { p1Tris++; sc += 2200; }
                } else if (cTriOppCan[ci] && ((ow1 & m) == m)) {
                    p1Tris++;
                    sc += 4300;
                }
            }
        }
        for (int ri = 0; ri < ROpp_cnt; ++ri) {
            if ((ow1 & rOppTriMask[ri]) == rOppTriMask[ri]) {
                p1Tris++;
                sc += 4300;
            }
        }

        if (totalP0 - k0 == 0 && p0Tris == 0) sc += 200000000LL;
        else if (p1Tris >= domThreshold) sc += 120000000LL + p1Tris * 20000LL;
        else if (p0Tris >= domThreshold) sc -= 120000000LL;

        // Realistic march tie-breaker for unused enemy units
        uint64_t aliveO = oOcc & ~k1Mask;
        for (uint64_t m = aliveO; m; m &= (m - 1)) {
            int u = __builtin_ctzll(m);
            int cnt = u1[u];
            int useful = min(cnt, u0Star[u] + 1);
            sc += ((int64_t)useful * oppPotGlobal[u]) / 3 - (int64_t)cnt * oppMinDGlobal[u] * 30LL;
        }
        return sc;
    };

    int oDest[512];
    int oSp[MAX_T];
    int64_t scB0 = evalP1Adv(b0DestGlobal, b0SpGlobal);
    int64_t scB1 = evalP1Adv(b1DestGlobal, b1SpGlobal);
    int64_t bestAdvSc;
    if (scB1 >= scB0) {
        memcpy(oDest, b1DestGlobal, O_cnt_global * sizeof(int));
        memcpy(oSp, b1SpGlobal, OS_cnt_global * sizeof(int));
        bestAdvSc = scB1;
    } else {
        memcpy(oDest, b0DestGlobal, O_cnt_global * sizeof(int));
        memcpy(oSp, b0SpGlobal, OS_cnt_global * sizeof(int));
        bestAdvSc = scB0;
    }

    bool lockO[512] = {false};
    bool lockOS[MAX_T] = {false};

    // Phase 1: Explicitly test encircling EVERY occupied node in u0Star (biggest P0 stacks first!)
    int p0Nodes[MAX_N], nP0Nodes = 0;
    for (uint64_t m = myOccStar; m; m &= (m - 1)) {
        int u = __builtin_ctzll(m);
        if (adj[u].size() >= 1 && adj[u].size() <= 5) p0Nodes[nP0Nodes++] = u;
    }
    sort(p0Nodes, p0Nodes + nP0Nodes, [&](int a, int b) { return u0Star[a] > u0Star[b]; });

    for (int pi = 0; pi < nP0Nodes; ++pi) {
        int tgt = p0Nodes[pi];
        int svIdx[32], svVal[32], nSv = 0;
        int svSpIdx[16], svSpVal[16], nSvSp = 0;
        int tmpOList[32], nTmpO = 0;
        int tmpSList[16], nTmpS = 0;
        bool okAll = true;
        for (int v : adj[tgt]) {
            int req = u0Star[v] + 1;
            int asg = 0;
            for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                if (!lockO[i] && oDest[i] == v && nTmpO < 32) {
                    lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                }
            }
            for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                if (!lockOS[j] && oSp[j] >= 0 && allTris[oppOwnedIdxGlobal[j]].c[oSp[j]] == v && nTmpS < 16) {
                    lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                }
            }
            for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                if (lockO[i]) continue;
                int u = oppLocsGlobal[i];
                if ((u == v || ((initAdjMask[u] >> v) & 1ULL)) && nSv < 30 && nTmpO < 32) {
                    svIdx[nSv] = i; svVal[nSv] = oDest[i]; nSv++;
                    oDest[i] = v;
                    lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                }
            }
            for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                if (lockOS[j]) continue;
                const Tri& ot = allTris[oppOwnedIdxGlobal[j]];
                for (int sc = 0; sc < 3; ++sc) {
                    if (ot.c[sc] == v && nSvSp < 15 && nTmpS < 16) {
                        svSpIdx[nSvSp] = j; svSpVal[nSvSp] = oSp[j]; nSvSp++;
                        oSp[j] = sc;
                        lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                        break;
                    }
                }
            }
            if (asg < req) { okAll = false; break; }
        }
        if (okAll && (nSv > 0 || nSvSp > 0)) {
            int64_t sc = evalP1Adv(oDest, oSp);
            if (sc > bestAdvSc) {
                bestAdvSc = sc;
                continue;
            }
        }
        for (int k = nSv - 1; k >= 0; --k) oDest[svIdx[k]] = svVal[k];
        for (int k = nSvSp - 1; k >= 0; --k) oSp[svSpIdx[k]] = svSpVal[k];
        for (int k = 0; k < nTmpO; ++k) lockO[tmpOList[k]] = false;
        for (int k = 0; k < nTmpS; ++k) lockOS[tmpSList[k]] = false;
    }

    // Phase 2: Explicitly test tie-blocking (`u1[c] == u0Star[c]`) P0's triangle captures in C_cnt!
    for (int ci = 0; ci < C_cnt; ++ci) {
        if ((ci & 7) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() >= budgetMs * 0.65) break;
        const Tri& t = allTris[capTriIdx[ci]];
        if ((myOccStar & t.mask) == 0) continue;
        int ord[3] = {0, 1, 2};
        if (u0Star[t.c[ord[1]]] < u0Star[t.c[ord[0]]]) swap(ord[0], ord[1]);
        if (u0Star[t.c[ord[2]]] < u0Star[t.c[ord[1]]]) swap(ord[1], ord[2]);
        if (u0Star[t.c[ord[1]]] < u0Star[t.c[ord[0]]]) swap(ord[0], ord[1]);

        for (int k = 0; k < 3; ++k) {
            int c = t.c[ord[k]];
            int req = max(1, u0Star[c]); // Tie-block (or occupy empty surrounded corner)!
            if (req > 3) continue;
            int svIdx[8], svVal[8], nSv = 0;
            int svSpIdx[8], svSpVal[8], nSvSp = 0;
            int tmpOList[8], nTmpO = 0;
            int tmpSList[8], nTmpS = 0;
            int asg = 0;
            for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                if (!lockO[i] && oDest[i] == c && nTmpO < 8) {
                    lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                }
            }
            for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                if (!lockOS[j] && oSp[j] >= 0 && allTris[oppOwnedIdxGlobal[j]].c[oSp[j]] == c && nTmpS < 8) {
                    lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                }
            }
            for (int i = 0; i < O_cnt_global && asg < req; ++i) {
                if (lockO[i]) continue;
                int u = oppLocsGlobal[i];
                if ((u == c || ((initAdjMask[u] >> c) & 1ULL)) && nSv < 8 && nTmpO < 8) {
                    svIdx[nSv] = i; svVal[nSv] = oDest[i]; nSv++;
                    oDest[i] = c;
                    lockO[i] = true; tmpOList[nTmpO++] = i; asg++;
                }
            }
            for (int j = 0; j < OS_cnt_global && asg < req; ++j) {
                if (lockOS[j]) continue;
                const Tri& ot = allTris[oppOwnedIdxGlobal[j]];
                for (int sc = 0; sc < 3; ++sc) {
                    if (ot.c[sc] == c && nSvSp < 8 && nTmpS < 8) {
                        svSpIdx[nSvSp] = j; svSpVal[nSvSp] = oSp[j]; nSvSp++;
                        oSp[j] = sc;
                        lockOS[j] = true; tmpSList[nTmpS++] = j; asg++;
                        break;
                    }
                }
            }
            if (asg >= req && (nSv > 0 || nSvSp > 0)) {
                int64_t sc = evalP1Adv(oDest, oSp);
                if (sc > bestAdvSc) {
                    bestAdvSc = sc;
                    break;
                }
            }
            for (int z = nSv - 1; z >= 0; --z) oDest[svIdx[z]] = svVal[z];
            for (int z = nSvSp - 1; z >= 0; --z) oSp[svSpIdx[z]] = svSpVal[z];
            for (int z = 0; z < nTmpO; ++z) lockO[tmpOList[z]] = false;
            for (int z = 0; z < nTmpS; ++z) lockOS[tmpSList[z]] = false;
        }
    }

    // Phase 3: Coordinate-ascent refinement directly against u0Star!
    for (int i = 0; i < O_cnt_global; ++i) {
        if ((i & 15) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() >= budgetMs) break;
        int u = oppLocsGlobal[i];
        int orig = oDest[i], bestOpt = orig;
        for (int k = 0; k < numMoveOpts[u]; ++k) {
            int cand = moveOpts[u][k];
            if (cand == orig) continue;
            oDest[i] = cand;
            int64_t sc = evalP1Adv(oDest, oSp);
            if (sc > bestAdvSc) { bestAdvSc = sc; bestOpt = cand; }
        }
        oDest[i] = bestOpt;
    }
    for (int j = 0; j < OS_cnt_global; ++j) {
        if ((j & 7) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - t0).count() >= budgetMs) break;
        int orig = oSp[j], bestOpt = orig;
        for (int cand = -1; cand < 3; ++cand) {
            if (cand == orig) continue;
            oSp[j] = cand;
            int64_t sc = evalP1Adv(oDest, oSp);
            if (sc > bestAdvSc) { bestAdvSc = sc; bestOpt = cand; }
        }
        oSp[j] = bestOpt;
    }

    finalizeOppResponse(outBr, oDest, oSp);
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

// ============================================================================
// DEDUPLICATED TOP-K 2-PLY ROLLOUT SELECTOR (Turns 2+ ExactSolver & SA!)
// ============================================================================
struct Ply2Cand {
    int64_t sc;
    uint64_t sig;
    int dest[64];
    int spawn[32];
};

inline uint64_t hashActionBoard(const int* dest, const int* spawn) {
    uint8_t cntU[MAX_N];
    memset(cntU, 0, N);
    for (int i = 0; i < M_cnt; ++i) cntU[dest[i]]++;
    uint64_t h = 14695981039346656037ULL;
    for (int u = 0; u < N; ++u) {
        if (cntU[u]) {
            h ^= (uint64_t)((u << 8) | cntU[u]);
            h *= 1099511628211ULL;
        }
    }
    for (int j = 0; j < S_cnt; ++j) {
        h ^= (uint64_t)((j << 4) | (spawn[j] + 2));
        h *= 1099511628211ULL;
    }
    return h;
}

inline void insertPly2Cand(Ply2Cand* pool, int& nPool, int maxPool, int64_t sc, int64_t minAllowed,
                           const int* dest, const int* spawn) {
    if (M_cnt > 64 || S_cnt > 32 || sc < minAllowed) return;
    uint64_t sig = hashActionBoard(dest, spawn);
    for (int k = 0; k < nPool; ++k) {
        if (pool[k].sig == sig) {
            if (sc > pool[k].sc) {
                pool[k].sc = sc;
                memcpy(pool[k].dest, dest, M_cnt * sizeof(int));
                memcpy(pool[k].spawn, spawn, S_cnt * sizeof(int));
            }
            return;
        }
    }
    if (nPool < maxPool) {
        pool[nPool].sc = sc;
        pool[nPool].sig = sig;
        memcpy(pool[nPool].dest, dest, M_cnt * sizeof(int));
        memcpy(pool[nPool].spawn, spawn, S_cnt * sizeof(int));
        nPool++;
    } else {
        int worst = 0;
        for (int k = 1; k < nPool; ++k) if (pool[k].sc < pool[worst].sc) worst = k;
        if (sc > pool[worst].sc) {
            pool[worst].sc = sc;
            pool[worst].sig = sig;
            memcpy(pool[worst].dest, dest, M_cnt * sizeof(int));
            memcpy(pool[worst].spawn, spawn, S_cnt * sizeof(int));
        }
    }
}

void selectBestWithPly2(chrono::high_resolution_clock::time_point turnStart, double limitMs,
                        Ply2Cand* rawPool, int nRaw,
                        vector<int>& bestDest, vector<int>& bestSpawn, int64_t& bestScore) {
    if (M_cnt > 44 || S_cnt > 24) return;
    double el0 = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
    if (el0 + 1.2 >= limitMs) return;

    const int64_t window = 2600LL;
    Ply2Cand cands[8];
    int nCands = 0;
    // Slot 0 is ALWAYS our 1-ply simultaneous Double-Oracle champion
    insertPly2Cand(cands, nCands, 8, bestScore, bestScore - window, bestDest.data(), bestSpawn.data());
    for (int i = 0; i < nRaw; ++i) {
        if (rawPool[i].sc >= bestScore - window) {
            insertPly2Cand(cands, nCands, 8, rawPool[i].sc, bestScore - window, rawPool[i].dest, rawPool[i].spawn);
        }
    }

    // Also probe 1-spawn corner variations & 1-unit neighbor variations around bestDest if room remains!
    if (nCands < 8 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count() + 1.6 < limitMs) {
        int tmpD[64], tmpS[32];
        memcpy(tmpD, bestDest.data(), M_cnt * sizeof(int));
        memcpy(tmpS, bestSpawn.data(), S_cnt * sizeof(int));
        for (int j = 0; j < S_cnt; ++j) {
            int orig = tmpS[j];
            for (int scOpt = -1; scOpt < 3; ++scOpt) {
                if (scOpt == orig) continue;
                tmpS[j] = scOpt;
                int64_t sc = evalPortfolio(tmpD, tmpS);
                if (sc >= bestScore - window) {
                    insertPly2Cand(cands, nCands, 8, sc, bestScore - window, tmpD, tmpS);
                }
            }
            tmpS[j] = orig;
        }
        int stepProbes = 0;
        for (int i = 0; i < M_cnt && stepProbes < 36; ++i) {
            if (i > 0 && unitLocs[i] == unitLocs[i - 1] && tmpD[i] == tmpD[i - 1]) continue;
            int u = unitLocs[i], orig = tmpD[i];
            for (int k = 0; k < numMoveOpts[u] && stepProbes < 36; ++k) {
                int cand = moveOpts[u][k];
                if (cand == orig) continue;
                stepProbes++;
                tmpD[i] = cand;
                int64_t sc = evalPortfolio(tmpD, tmpS);
                if (sc >= bestScore - window) {
                    insertPly2Cand(cands, nCands, 8, sc, bestScore - window, tmpD, tmpS);
                }
            }
            tmpD[i] = orig;
        }
    }

    if (nCands <= 1) return;
    for (int a = 1; a < nCands; ++a) {
        for (int b = a + 1; b < nCands; ++b) {
            if (cands[b].sc > cands[a].sc) swap(cands[a], cands[b]);
        }
    }

    // Snapshot Turn-T globals before simulating Turn-(T+1) continuations
    int origM = M_cnt, origS = S_cnt, origTurnNow = curTurn;
    int origUnitLocs[64], origOwnedIdx[32];
    memcpy(origUnitLocs, unitLocs, origM * sizeof(int));
    memcpy(origOwnedIdx, ownedTriIdx, origS * sizeof(int));
    int origMyU[MAX_N], origOppU[MAX_N], origOppPred[MAX_N];
    memcpy(origMyU, myUnits, sizeof(myUnits));
    memcpy(origOppU, oppUnits, sizeof(oppUnits));
    memcpy(origOppPred, oppPredUnits, sizeof(oppPredUnits));
    uint64_t origOppPredMask = oppPredHasUnitsMask;
    Tri origTris[MAX_T];
    for (int t = 0; t < T_cnt; ++t) origTris[t] = allTris[t];

    const int* oppNextU = (numOppResp >= 2) ? oppPortfolio[1].u1 : origOppU;
    int bestIdx = 0;
    int64_t bestCombined = -1000000000000000LL;
    int evaluatedCnt = 0;

    for (int ci = 0; ci < nCands; ++ci) {
        double el = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
        if (el + 0.35 >= limitMs) break;

        memset(myUnits, 0, N * sizeof(int));
        memcpy(oppUnits, oppNextU, N * sizeof(int));
        for (int t = 0; t < T_cnt; ++t) allTris[t] = origTris[t];

        for (int i = 0; i < origM; ++i) myUnits[cands[ci].dest[i]]++;
        for (int j = 0; j < origS; ++j) {
            int sc = cands[ci].spawn[j];
            if (sc >= 0) {
                Tri& tr = allTris[origOwnedIdx[j]];
                myUnits[tr.c[sc]]++;
                tr.owner = -1;
                tr.meCan = false;
            }
        }
        if (numOppResp >= 2) {
            for (int j = 0; j < OS_cnt_global; ++j) {
                if (!oppPortfolio[1].held1[j]) {
                    Tri& otr = allTris[oppOwnedIdxGlobal[j]];
                    otr.owner = -1;
                    otr.oppCan = false;
                }
            }
        }

        auto nodeOw = [&](int u) -> int {
            if (!adj[u].empty()) {
                bool all0 = true, all1 = true;
                for (int nb : adj[u]) {
                    if (myUnits[nb] <= oppUnits[nb]) all0 = false;
                    if (oppUnits[nb] <= myUnits[nb]) all1 = false;
                }
                if (all0) return 0;
                if (all1) return 1;
            }
            if (myUnits[u] > oppUnits[u]) return 0;
            if (oppUnits[u] > myUnits[u]) return 1;
            return -1;
        };

        // Exact Referee surroundNodes() once
        int kill0[MAX_N], nk0 = 0, kill1[MAX_N], nk1 = 0;
        for (int u = 0; u < N; ++u) {
            int ow = nodeOw(u);
            if (ow == 0 && oppUnits[u] > 0) kill1[nk1++] = u;
            else if (ow == 1 && myUnits[u] > 0) kill0[nk0++] = u;
        }
        for (int z = 0; z < nk0; ++z) myUnits[kill0[z]] = 0;
        for (int z = 0; z < nk1; ++z) oppUnits[kill1[z]] = 0;

        // Exact Referee fixed-point loop: updateAllowedCaptures() + captureTriangles()
        while (true) {
            bool chg = false;
            for (int t = 0; t < T_cnt; ++t) {
                Tri& tr = allTris[t];
                if (!tr.meCan && myUnits[tr.h1] == 0 && myUnits[tr.h2] == 0 && myUnits[tr.h3] == 0) tr.meCan = true;
                if (!tr.oppCan && oppUnits[tr.h1] == 0 && oppUnits[tr.h2] == 0 && oppUnits[tr.h3] == 0) tr.oppCan = true;
            }
            int ows[MAX_N];
            for (int u = 0; u < N; ++u) ows[u] = nodeOw(u);
            for (int t = 0; t < T_cnt; ++t) {
                Tri& tr = allTris[t];
                int o0 = ows[tr.h1];
                if (o0 != -1 && o0 == ows[tr.h2] && o0 == ows[tr.h3]) {
                    if (o0 == 0 && tr.owner != 0 && tr.meCan) {
                        tr.owner = 0; tr.meCan = false; chg = true;
                    } else if (o0 == 1 && tr.owner != 1 && tr.oppCan) {
                        tr.owner = 1; tr.oppCan = false; chg = true;
                    }
                }
            }
            if (!chg) break;
        }

        // Exact 25us board/robustness/surround/attack-zone precomputation for Turn T+1!
        curTurn = origTurnNow + 1;
        precomputeTurnMetrics(false);
        memcpy(oppPredUnits, oppRobustUnits, N * sizeof(int));
        oppPredHasUnitsMask = oppRobustHasUnitsMask;

        if (M_cnt > 128 || S_cnt > 64) break;
        int d2[128], sp2[64];
        for (int i = 0; i < M_cnt; ++i) d2[i] = unitLocs[i];
        for (int j = 0; j < S_cnt; ++j) {
            if (M_cnt + j < armyTargetGlobal) {
                const Tri& tr = allTris[ownedTriIdx[j]];
                int bc = 0, bp = -999999;
                for (int k = 0; k < 3; ++k) {
                    int c = tr.c[k];
                    int pocketPen = (adjMask[c] != 0 && (adjMask[c] & ~tr.mask) == 0) ? 2000 :
                                    ((((interiorOwnedMask >> c) & 1ULL) && (tr.mask & ~interiorOwnedMask) != 0) ? 1100 : 0);
                    int p = nodePotential[c] - (armedEnemyAtkZone[c] ? 800 : 0) - pocketPen;
                    if (p > bp) { bp = p; bc = k; }
                }
                sp2[j] = bc;
            } else {
                sp2[j] = -1;
            }
        }
        int64_t ply2Sc = evalState(d2, sp2);

        // Sort C_cnt by corner potential descending & run coordinated robust 3-corner capture pass
        int sortedC[MAX_T];
        for (int k = 0; k < C_cnt; ++k) sortedC[k] = capTriIdx[k];
        sort(sortedC, sortedC + C_cnt, [&](int a, int b) {
            const Tri& ta = allTris[a]; const Tri& tb = allTris[b];
            return (nodePotential[ta.h1] + nodePotential[ta.h2] + nodePotential[ta.h3]) >
                   (nodePotential[tb.h1] + nodePotential[tb.h2] + nodePotential[tb.h3]);
        });
        bool lkU[128] = {false}, lkS[64] = {false};
        for (int k = 0; k < C_cnt; ++k) {
            const Tri& tr = allTris[sortedC[k]];
            int svI[12], svV[12], nS = 0, svSI[8], svSV[8], nSS = 0;
            int tU[12], ntU = 0, tS[8], ntS = 0;
            bool ok = true;
            for (int z = 0; z < 3; ++z) {
                int corner = tr.c[z];
                int req = oppRobustUnits[corner] + 1;
                if (adjMask[corner] != 0 && (adjMask[corner] & ~tr.mask) == 0 && oppUnits[corner] == 0) req = 0;
                int asg = 0;
                for (int i = 0; i < M_cnt && asg < req; ++i) {
                    if (!lkU[i] && d2[i] == corner && ntU < 12) { lkU[i] = true; tU[ntU++] = i; asg++; }
                }
                for (int j = 0; j < S_cnt && asg < req; ++j) {
                    if (!lkS[j] && sp2[j] >= 0 && allTris[ownedTriIdx[j]].c[sp2[j]] == corner && ntS < 8) {
                        lkS[j] = true; tS[ntS++] = j; asg++;
                    }
                }
                for (int i = 0; i < M_cnt && asg < req; ++i) {
                    if (lkU[i]) continue;
                    int u = unitLocs[i];
                    if ((u == corner || ((initAdjMask[u] >> corner) & 1ULL)) && nS < 12 && ntU < 12) {
                        svI[nS] = i; svV[nS] = d2[i]; nS++;
                        d2[i] = corner; lkU[i] = true; tU[ntU++] = i; asg++;
                    }
                }
                for (int j = 0; j < S_cnt && asg < req; ++j) {
                    if (lkS[j]) continue;
                    const Tri& ot = allTris[ownedTriIdx[j]];
                    for (int sc = 0; sc < 3; ++sc) {
                        if (ot.c[sc] == corner && nSS < 8 && ntS < 8) {
                            svSI[nSS] = j; svSV[nSS] = sp2[j]; nSS++;
                            sp2[j] = sc; lkS[j] = true; tS[ntS++] = j; asg++; break;
                        }
                    }
                }
                if (asg < req) { ok = false; break; }
            }
            if (ok && (nS > 0 || nSS > 0)) {
                int64_t candSc = evalState(d2, sp2);
                if (candSc > ply2Sc) { ply2Sc = candSc; continue; }
            }
            for (int z = nS - 1; z >= 0; --z) d2[svI[z]] = svV[z];
            for (int z = nSS - 1; z >= 0; --z) sp2[svSI[z]] = svSV[z];
            for (int z = 0; z < ntU; ++z) lkU[tU[z]] = false;
            for (int z = 0; z < ntS; ++z) lkS[tS[z]] = false;
        }

        // Single fast coordinate-ascent polish on Ply 2
        for (int i = 0; i < M_cnt; ++i) {
            if ((i & 7) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count() >= limitMs) break;
            int u = unitLocs[i], orig = d2[i], bOpt = orig;
            for (int k = 0; k < numMoveOpts[u]; ++k) {
                int cand = moveOpts[u][k];
                if (cand == orig) continue;
                d2[i] = cand;
                int64_t sc = evalState(d2, sp2);
                if (sc > ply2Sc) { ply2Sc = sc; bOpt = cand; }
            }
            d2[i] = bOpt;
        }
        for (int j = 0; j < S_cnt; ++j) {
            if ((j & 3) == 0 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count() >= limitMs) break;
            int orig = sp2[j], bOpt = orig;
            for (int cand = -1; cand < 3; ++cand) {
                if (cand == orig) continue;
                sp2[j] = cand;
                int64_t sc = evalState(d2, sp2);
                if (sc > ply2Sc) { ply2Sc = sc; bOpt = cand; }
            }
            sp2[j] = bOpt;
        }

        int64_t combined = cands[ci].sc * 4LL + ply2Sc;
        if (combined > bestCombined) {
            bestCombined = combined;
            bestIdx = ci;
        }
        evaluatedCnt++;
    }

    // Restore Turn-T globals cleanly for output formatting
    M_cnt = origM; S_cnt = origS; curTurn = origTurnNow;
    memcpy(unitLocs, origUnitLocs, origM * sizeof(int));
    memcpy(ownedTriIdx, origOwnedIdx, origS * sizeof(int));
    memcpy(myUnits, origMyU, sizeof(myUnits));
    memcpy(oppUnits, origOppU, sizeof(oppUnits));
    memcpy(oppPredUnits, origOppPred, sizeof(oppPredUnits));
    oppPredHasUnitsMask = origOppPredMask;
    for (int t = 0; t < T_cnt; ++t) allTris[t] = origTris[t];

    if (evaluatedCnt >= 2 && bestIdx != 0) {
        bestScore = cands[bestIdx].sc;
        for (int i = 0; i < M_cnt; ++i) bestDest[i] = cands[bestIdx].dest[i];
        for (int j = 0; j < S_cnt; ++j) bestSpawn[j] = cands[bestIdx].spawn[j];
    }
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
    Ply2Cand topPool[8];
    int nTop = 0;

    void dfsSpawn(int sj) {
        if (timedOut) return;
        if (sj == S_cnt) {
            if ((++leaves & 255) == 0) {
                double el = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - startTp).count();
                if (el >= limitMs) { timedOut = true; return; }
            }
            int64_t sc = evalPortfolio(curDest, curSpawn);
            if (sc > bestScore) {
                bestScore = sc;
                memcpy(bestDest, curDest, M_cnt * sizeof(int));
                memcpy(bestSpawn, curSpawn, S_cnt * sizeof(int));
            }
            if (sc >= bestScore - 2600LL) {
                insertPly2Cand(topPool, nTop, 8, sc, bestScore - 2600LL, curDest, curSpawn);
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

                    // Check if wiping u rescues a truly un-walkable surrounded friendly stack
                    // MOVE runs BEFORE surroundNodes(): v can escape onto nb unless oppUnits[nb] > myUnits[nb] + myUnits[v] for ALL nb!
                    int savedBoxed = 0;
                    for (int v : adj[u]) {
                        if (myUnits[v] >= 2 && !adj[v].empty()) {
                            bool unwalkableSurr = true;
                            for (int nb : adj[v]) {
                                if (oppUnits[nb] <= myUnits[nb] + myUnits[v]) {
                                    unwalkableSurr = false;
                                    break;
                                }
                            }
                            if (unwalkableSurr) savedBoxed += min(4, myUnits[v]);
                        }
                    }

                    int effKill = oppU + savedBoxed;
                    bool worthIt = false;
                    if (oppU >= 6 && curMyTotal >= 8) worthIt = true;
                    else if (oppU == 5 && curMyTotal >= 15 && (curMyTris >= 2 || curMyTotal >= curOppTotal - 2)) worthIt = true;
                    else if (oppU >= 3 && savedBoxed >= 3 && effKill >= 6 && curMyTotal >= 16 && curMyTris >= 2) worthIt = true;
                    else if (oppU == 4 && curMyTotal >= 36 && curMyTotal >= curOppTotal + 2 && curMyTris >= 2) worthIt = true;
                    else if (oppU == 3 && curMyTotal >= 46 && curMyTotal >= curOppTotal + 12 && curMyTris >= 4) worthIt = true;
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

        double maxTimeMs = (curTurn == 1 ? 800.0 : 36.5);

        if (curTurn <= 2 && M_cnt == 3 && S_cnt == 0) {
            double openLimit = (curTurn == 1 ? 280.0 : 32.0);
            solveOpening3Units(turnStart, openLimit, bestDest, bestScore);
        } else if (comb <= 12000.0 && M_cnt < 64 && S_cnt < 64) {
            ExactSolver ex;
            ex.startTp = turnStart;
            ex.limitMs = (curTurn == 1 ? 700.0 : 27.5);
            ex.dfsMove(0);
            bestScore = ex.bestScore;
            for (int i = 0; i < M_cnt; ++i) bestDest[i] = ex.bestDest[i];
            for (int j = 0; j < S_cnt; ++j) bestSpawn[j] = ex.bestSpawn[j];
            if (!ex.timedOut) {
                selectBestWithPly2(turnStart, (curTurn == 1 ? 800.0 : 36.2), ex.topPool, ex.nTop, bestDest, bestSpawn, bestScore);
            }
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

            int64_t curScore = evalPortfolio(curDest.data(), curSpawn.data());
            bestScore = curScore;
            bestDest = curDest;
            bestSpawn = curSpawn;

            // 2 Quick Coordinate-Ascent Passes to warm-start SA
            for (int pass = 0; pass < 2; ++pass) {
                if (chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count() >= 25.0 && curTurn > 1) break;
                for (int i = 0; i < M_cnt; ++i) {
                    if ((i & 15) == 0 && curTurn > 1 && chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count() >= 25.0) break;
                    int u = unitLocs[i];
                    int orig = curDest[i];
                    int bestOpt = orig;
                    for (int k = 0; k < numMoveOpts[u]; ++k) {
                        int cand = moveOpts[u][k];
                        if (cand == orig) continue;
                        curDest[i] = cand;
                        int64_t sc = evalPortfolio(curDest.data(), curSpawn.data());
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
                        int64_t sc = evalPortfolio(curDest.data(), curSpawn.data());
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

            // 3-Stage Double-Oracle Simulated Annealing loop + Top-8 Pool for Ply-2 Rollout
            bool usePly2AfterSA = (M_cnt <= 44 && S_cnt <= 24);
            double temp = 2600.0;
            double saBudgetMs = (curTurn == 1 ? 700.0 : (usePly2AfterSA ? 28.8 : 32.5));
            double polishMaxMs = (curTurn == 1 ? 745.0 : (usePly2AfterSA ? 31.8 : maxTimeMs));
            double oracle1Ms  = (curTurn == 1 ? 220.0 : (usePly2AfterSA ? 9.6  : 10.8));
            double oracle2Ms  = (curTurn == 1 ? 450.0 : (usePly2AfterSA ? 19.2 : 21.4));
            double advBudget  = (curTurn == 1 ? 8.0   : 0.95);
            int oracleStage = 0;
            vector<int> stage1BestDest = bestDest, stage1BestSpawn = bestSpawn;
            Ply2Cand saTopPool[8];
            int nSaTop = 0;
            int iter = 0;

            int savedIdx[64], savedVal[64], nSaved = 0;
            int savedSpIdx[16], savedSpVal[16], nSavedSp = 0;

            while (true) {
                if ((iter & 31) == 0) {
                    double elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                    if (elapsed >= saBudgetMs) break;

                    // Trigger B_2 (First Adversarial Best-Response Oracle)
                    if (oracleStage == 0 && elapsed >= oracle1Ms && numOppResp == 3) {
                        oracleStage = 1;
                        stage1BestDest = bestDest;
                        stage1BestSpawn = bestSpawn;
                        buildAdversarialResponse(bestDest.data(), bestSpawn.data(), advBudget, oppPortfolio[3]);
                        numOppResp = 4;
                        bestScore = evalPortfolio(bestDest.data(), bestSpawn.data());
                        curDest = bestDest;
                        curSpawn = bestSpawn;
                        curScore = bestScore;
                        elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                        if (elapsed >= saBudgetMs) break;
                    }
                    // Trigger B_3 (Second Adversarial Best-Response Oracle)
                    if (oracleStage == 1 && elapsed >= oracle2Ms && numOppResp == 4) {
                        oracleStage = 2;
                        buildAdversarialResponse(bestDest.data(), bestSpawn.data(), advBudget, oppPortfolio[4]);
                        numOppResp = 5;
                        bestScore = evalPortfolio(bestDest.data(), bestSpawn.data());
                        int64_t s1ReEval = evalPortfolio(stage1BestDest.data(), stage1BestSpawn.data());
                        if (s1ReEval > bestScore) {
                            bestScore = s1ReEval;
                            bestDest = stage1BestDest;
                            bestSpawn = stage1BestSpawn;
                        }
                        curDest = bestDest;
                        curSpawn = bestSpawn;
                        curScore = bestScore;
                        elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                        if (elapsed >= saBudgetMs) break;
                    }

                    // Stage-aware reheated temperature schedule
                    if (oracleStage == 0) {
                        double r = clamp(elapsed / oracle1Ms, 0.0, 1.0);
                        temp = 2600.0 * pow(120.0 / 2600.0, r);
                    } else if (oracleStage == 1) {
                        double r = clamp((elapsed - oracle1Ms) / max(1.0, oracle2Ms - oracle1Ms), 0.0, 1.0);
                        temp = 1350.0 * pow(60.0 / 1350.0, r);
                    } else {
                        double span = max(1.0, saBudgetMs - oracle2Ms);
                        double r = clamp((elapsed - oracle2Ms) / span, 0.0, 1.0);
                        if (r < 0.72) temp = 850.0 * pow(10.0 / 850.0, r / 0.72);
                        else temp = 0.0;
                    }
                }
                iter++;

                nSaved = 0;
                nSavedSp = 0;
                int roll = rng.nextInt(100);

                if (roll < 39 && M_cnt > 0) {
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
                } else if (roll < 57 && S_cnt > 0) {
                    // Op 2: Single spawn change
                    int j = rng.nextInt(S_cnt);
                    int cand = rng.nextInt(4) - 1;
                    if (cand == curSpawn[j]) continue;
                    savedSpIdx[nSavedSp] = j; savedSpVal[nSavedSp] = curSpawn[j]; nSavedSp++;
                    curSpawn[j] = cand;
                } else if (roll < 77 && C_cnt > 0 && (M_cnt + S_cnt) > 0) {
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
                    int earEnemy = -1;
                    for (int k = 0; k < 3; ++k) {
                        int c = t.c[k];
                        if (adjMask[c] != 0 && (adjMask[c] & ~t.mask) == 0) {
                            earEnemy = oppUnits[c];
                            break;
                        }
                    }
                    for (int idx = 0; idx < 3; ++idx) {
                        int corner = t.c[ord[idx]];
                        int req = (reinforce ? max(oppRobustUnits[corner], oppPredUnits[corner]) : oppUnits[corner]) + 1;
                        if (adjMask[corner] != 0 && (adjMask[corner] & ~t.mask) == 0) {
                            req = 0; // Degree-2 corner inside t is auto-owned (and enemy wiped!) when the other 2 corners are owned!
                        } else if (earEnemy > 0) {
                            req = max(req, oppUnits[corner] + 2); // Ensure thinNbrs == 0 to wipe enemy on earCorner!
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
                } else if (roll < 84 && R_cnt > 0) {
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
                } else if (roll < 90 && K_cnt > 0 && (M_cnt + S_cnt) >= 2) {
                    // Op 7: Coordinated Enemy-Stack Encirclement / Surround-Kill Macro-Move!
                    int tgt = surrTargetIdx[rng.nextInt(K_cnt)];
                    bool lockU[512];
                    bool lockS[MAX_T];
                    memset(lockU, 0, M_cnt * sizeof(bool));
                    memset(lockS, 0, S_cnt * sizeof(bool));

                    int deg = (int)adj[tgt].size();
                    int ord[8], reachC[8];
                    for (int k = 0; k < deg; ++k) {
                        ord[k] = k;
                        int v = adj[tgt][k];
                        int rc = myUnits[v];
                        for (int nb : initAdj[v]) rc += myUnits[nb];
                        for (int j = 0; j < S_cnt; ++j) if (allTris[ownedTriIdx[j]].hasNode(v)) rc++;
                        reachC[k] = rc;
                    }
                    for (int a = 0; a < deg; ++a) {
                        for (int b = a + 1; b < deg; ++b) {
                            if (reachC[ord[b]] < reachC[ord[a]]) swap(ord[a], ord[b]);
                        }
                    }

                    for (int idx = 0; idx < deg; ++idx) {
                        int v = adj[tgt][ord[idx]];
                        int req = (oppUnits[tgt] > 0) ? max(oppUnits[v] + 2, oppPredUnits[v] + 1)
                                                      : (max(oppUnits[v], oppPredUnits[v]) + 1);
                        int assigned = 0;
                        for (int i = 0; i < M_cnt && assigned < req; ++i) {
                            if (!lockU[i] && curDest[i] == v) {
                                lockU[i] = true;
                                assigned++;
                            }
                        }
                        for (int j = 0; j < S_cnt && assigned < req; ++j) {
                            if (!lockS[j] && curSpawn[j] >= 0 && allTris[ownedTriIdx[j]].c[curSpawn[j]] == v) {
                                lockS[j] = true;
                                assigned++;
                            }
                        }
                        while (assigned < req) {
                            bool found = false;
                            if (M_cnt > 0) {
                                int startOff = rng.nextInt(M_cnt);
                                for (int step = 0; step < M_cnt; ++step) {
                                    int i = (startOff + step) % M_cnt;
                                    if (lockU[i]) continue;
                                    int u = unitLocs[i];
                                    if ((u == v || ((initAdjMask[u] >> v) & 1ULL)) && nSaved < 60) {
                                        if (curDest[i] != v) {
                                            savedIdx[nSaved] = i; savedVal[nSaved] = curDest[i]; nSaved++;
                                            curDest[i] = v;
                                        }
                                        lockU[i] = true;
                                        assigned++;
                                        found = true;
                                        break;
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
                                        if (ot.c[sc] == v && nSavedSp < 15) {
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
                    // Step any remaining friendly units off `tgt` onto a neighbor if possible
                    for (int i = 0; i < M_cnt && nSaved < 60; ++i) {
                        if (!lockU[i] && curDest[i] == tgt) {
                            int u = unitLocs[i];
                            int outs[MAX_N], nOut = 0;
                            for (int k = 0; k < numMoveOpts[u]; ++k) {
                                int cand = moveOpts[u][k];
                                if (cand != tgt) outs[nOut++] = cand;
                            }
                            if (nOut > 0) {
                                savedIdx[nSaved] = i; savedVal[nSaved] = curDest[i]; nSaved++;
                                curDest[i] = outs[rng.nextInt(nOut)];
                            }
                        }
                    }
                    if (nSaved == 0 && nSavedSp == 0) continue;
                } else if (roll < 95 && numOppResp > 1 && (M_cnt + S_cnt) >= 1) {
                    // Op 8: Targeted Enemy-Capture Tie-Blocker (`u0[c] == u1[c]`)!
                    const OppResponse& br = oppPortfolio[1 + rng.nextInt(numOppResp - 1)];
                    if (br.nBlockCorners == 0) continue;
                    int corner = br.blockCorners[rng.nextInt(br.nBlockCorners)];
                    int req = br.u1[corner] + (rng.nextInt(4) == 0 ? 1 : 0);
                    bool lockU[512] = {false};
                    bool lockS[MAX_T] = {false};
                    int assigned = 0;
                    for (int i = 0; i < M_cnt && assigned < req; ++i) {
                        if (curDest[i] == corner) { lockU[i] = true; assigned++; }
                    }
                    for (int j = 0; j < S_cnt && assigned < req; ++j) {
                        if (curSpawn[j] >= 0 && allTris[ownedTriIdx[j]].c[curSpawn[j]] == corner) {
                            lockS[j] = true; assigned++;
                        }
                    }
                    while (assigned < req) {
                        bool found = false;
                        if (M_cnt > 0) {
                            int startOff = rng.nextInt(M_cnt);
                            for (int step = 0; step < M_cnt; ++step) {
                                int i = (startOff + step) % M_cnt;
                                if (lockU[i]) continue;
                                int u = unitLocs[i];
                                if ((u == corner || ((initAdjMask[u] >> corner) & 1ULL)) && nSaved < 60) {
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
                    if (nSaved == 0 && nSavedSp == 0) continue;
                } else if (roll < 98 && M_cnt >= 2) {
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

                int64_t newScore = evalPortfolio(curDest.data(), curSpawn.data());
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
                    if (usePly2AfterSA && oracleStage == 2 && newScore >= bestScore - 2600LL) {
                        insertPly2Cand(saTopPool, nSaTop, 8, newScore, bestScore - 2600LL, curDest.data(), curSpawn.data());
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
                if (elapsed >= polishMaxMs) goto polish_done;
                int u = unitLocs[i];
                int orig = curDest[i];
                int bestOpt = orig;
                for (int k = 0; k < numMoveOpts[u]; ++k) {
                    int cand = moveOpts[u][k];
                    if (cand == orig) continue;
                    curDest[i] = cand;
                    int64_t sc = evalPortfolio(curDest.data(), curSpawn.data());
                    if (sc > curScore) { curScore = sc; bestOpt = cand; }
                }
                curDest[i] = bestOpt;
            }
            for (int j = 0; j < S_cnt; ++j) {
                double elapsed = chrono::duration<double, milli>(chrono::high_resolution_clock::now() - turnStart).count();
                if (elapsed >= polishMaxMs) goto polish_done;
                int orig = curSpawn[j];
                int bestOpt = orig;
                for (int cand = -1; cand < 3; ++cand) {
                    if (cand == orig) continue;
                    curSpawn[j] = cand;
                    int64_t sc = evalPortfolio(curDest.data(), curSpawn.data());
                    if (sc > curScore) { curScore = sc; bestOpt = cand; }
                }
                curSpawn[j] = bestOpt;
            }
        polish_done:
            bestDest = curDest;
            bestSpawn = curSpawn;
            bestScore = curScore;
            if (usePly2AfterSA) {
                selectBestWithPly2(turnStart, (curTurn == 1 ? 820.0 : 36.2), saTopPool, nSaTop, bestDest, bestSpawn, bestScore);
            }
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
             << " | R:" << numOppResp << " | eval:" << bestScore << " | " << usedMs << "ms\n";

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
