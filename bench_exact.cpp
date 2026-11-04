#include <iostream>
#include <vector>
#include <chrono>
using namespace std;

struct Point { int id, x, y; };
struct Triangle {
    int id, h1, h2, h3, owner;
    bool meCan, oppCan;
    bool hasNode(int u) const { return h1 == u || h2 == u || h3 == u; }
    bool isNeighbor(int u, const vector<vector<int>>& adj) const {
        for (int c : {h1, h2, h3}) {
            for (int v : adj[c]) {
                if (v == u) return true;
            }
        }
        return false;
    }
};

int houseCount = 44;
vector<vector<int>> adj(houseCount);

int main() {
    // Exact edges from game
    vector<pair<int,int>> edges = {
        {0,3},{0,9},{0,11},{0,23},{0,26},{0,36},{0,43},{1,2},{1,8},{1,10},{1,22},{1,27},{1,37},{1,42},
        {2,37},{2,41},{3,36},{3,40},{4,6},{4,12},{4,25},{4,34},{5,7},{5,13},{5,24},{5,35},{6,32},
        {7,33},{8,15},{8,22},{8,25},{8,31},{8,42},{9,14},{9,23},{9,24},{9,30},{9,43},{10,20},{10,37},
        {10,42},{11,21},{11,36},{11,43},{12,32},{13,33},{14,24},{14,29},{14,30},{15,25},{15,28},{15,31},
        {16,19},{16,31},{16,38},{16,42},{17,18},{17,30},{17,39},{17,43},{18,28},{18,30},{18,39},{19,29},
        {19,31},{19,38},{20,37},{20,38},{20,42},{21,36},{21,39},{21,43},{22,25},{22,27},{22,32},{23,24},
        {23,26},{23,33},{24,29},{24,35},{25,28},{25,34},{26,33},{26,40},{27,32},{27,41},{28,34},{28,39},
        {29,35},{29,38},{30,31},{30,43},{31,42}
    };
    for (auto const& e : edges) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }

    vector<Triangle> triangles;
    int triId = 0;
    for (int u = 0; u < houseCount; ++u) {
        for (int v : adj[u]) {
            if (v > u) {
                for (int w : adj[v]) {
                    if (w > v) {
                        for (int x : adj[w]) {
                            if (x == u) {
                                triangles.push_back({triId++, u, v, w, -1, true, true});
                            }
                        }
                    }
                }
            }
        }
    }

    // Set owner = 0 for our 2 triangles
    for (auto& t : triangles) {
        if ((t.hasNode(0) && t.hasNode(9) && t.hasNode(23)) ||
            (t.hasNode(0) && t.hasNode(23) && t.hasNode(26)) ||
            (t.hasNode(23) && t.hasNode(26) && t.hasNode(33))) {
            t.owner = 0;
        }
    }

    vector<int> oppUnits(houseCount, 0);
    oppUnits[22] = 1; oppUnits[1] = 1; oppUnits[8] = 1; oppUnits[27] = 1;

    vector<int> unitLocs = {0, 9, 23, 26};
    vector<int> d0Choices = {unitLocs[0]}; for (int v : adj[unitLocs[0]]) d0Choices.push_back(v);
    vector<int> d1Choices = {unitLocs[1]}; for (int v : adj[unitLocs[1]]) d1Choices.push_back(v);
    vector<int> d2Choices = {unitLocs[2]}; for (int v : adj[unitLocs[2]]) d2Choices.push_back(v);
    vector<int> d3Choices = {unitLocs[3]}; for (int v : adj[unitLocs[3]]) d3Choices.push_back(v);

    struct SpawnChoice { int triId, corner; };
    vector<SpawnChoice> spawnChoices = {{-1, -1}};
    for (const auto& t : triangles) {
        if (t.owner == 0) {
            spawnChoices.push_back({t.id, t.h1});
            spawnChoices.push_back({t.id, t.h2});
            spawnChoices.push_back({t.id, t.h3});
        }
    }

    cout << "Triangles: " << triangles.size() << endl;
    cout << "spawnChoices: " << spawnChoices.size() 
         << " d0: " << d0Choices.size() 
         << " d1: " << d1Choices.size() 
         << " d2: " << d2Choices.size() 
         << " d3: " << d3Choices.size() << endl;

    auto isInEnemyKillZone = [&](int u) {
        for (const auto& t : triangles) {
            if (t.owner == 1 && oppUnits[t.h1] >= 1 && oppUnits[t.h2] >= 1 && oppUnits[t.h3] >= 1) {
                if (t.isNeighbor(u, adj) || t.hasNode(u)) return true;
            }
        }
        return false;
    };

    auto t0 = chrono::high_resolution_clock::now();
    int demand[64] = {0};
    bool owned[64];
    double bestScore = -1e9;
    int iters = 0;

    for (const auto& sp : spawnChoices) {
        int s = sp.corner;
        if (s != -1) demand[s]++;
        for (int d0 : d0Choices) {
            demand[d0]++;
            for (int d1 : d1Choices) {
                demand[d1]++;
                for (int d2 : d2Choices) {
                    demand[d2]++;
                    for (int d3 : d3Choices) {
                        demand[d3]++;
                        iters++;
                        bool capacityOk = true;
                        for (int node : {s, d0, d1, d2, d3}) {
                            if (node != -1) {
                                if (demand[node] > 2 && oppUnits[node] == 0) {
                                    capacityOk = false;
                                    break;
                                }
                                if (isInEnemyKillZone(node) && oppUnits[node] >= demand[node]) {
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
                            double sc = cap * 100000.0;
                            if (sc > bestScore) bestScore = sc;
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
    auto t1 = chrono::high_resolution_clock::now();
    double ms = chrono::duration<double, milli>(t1 - t0).count();
    cout << "iters: " << iters << ", time: " << ms << " ms, bestScore: " << bestScore << endl;
    return 0;
}
