
#include <iostream>
#include <vector>
#include <chrono>

using namespace std;

struct Triangle {
    int id, h1, h2, h3, owner;
    bool meCan;
};

int main() {
    int houseCount = 44;
    vector<vector<int>> adj(houseCount);
    // Add some realistic edges
    for (int i = 0; i < 44; ++i) {
        for (int k = 1; k <= 5; ++k) adj[i].push_back((i + k) % 44);
    }
    vector<Triangle> triangles;
    for (int i = 0; i < 60; ++i) {
        triangles.push_back({i, i%44, (i+1)%44, (i+2)%44, (i<2?0:-1), true});
    }
    vector<int> oppUnits(44, 0);
    oppUnits[10] = 1; oppUnits[20] = 1; oppUnits[30] = 1;

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

    auto t0 = chrono::high_resolution_clock::now();
    int demand[64] = {0};
    bool owned[64];
    int bestScore = -1;
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
                                if (owned[t.h1] && owned[t.h2] && owned[t.h3]) cap++;
                            }
                            if (cap > bestScore) bestScore = cap;
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
    cout << "Iterations: " << iters << ", Time: " << ms << " ms, bestCap: " << bestScore << endl;
    return 0;
}
