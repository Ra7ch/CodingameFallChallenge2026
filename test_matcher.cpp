#include <iostream>
#include <vector>
#include <queue>
#include <map>

using namespace std;

const int INF = 1e9;
const int MAX_HOUSES = 55;

int houseCount = 44;
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

struct BipartiteMatcher {
    int numUnits;
    vector<int> slotHouse;
    vector<vector<int>> unitAdj;
    vector<int> matchUnit;
    vector<int> matchSlot;
    vector<bool> visited;

    BipartiteMatcher(int nUnits) : numUnits(nUnits) {
        matchSlot.assign(numUnits, -1);
    }

    int addSlot(int house, const vector<int>& unitPositions) {
        int slotId = slotHouse.size();
        slotHouse.push_back(house);
        matchUnit.push_back(-1);

        if (unitAdj.size() < (size_t)numUnits) {
            unitAdj.resize(numUnits);
        }
        for (int u = 0; u < numUnits; ++u) {
            int uPos = unitPositions[u];
            if (uPos == house || distMatrix[uPos][house] <= 1) {
                unitAdj[u].push_back(slotId);
            }
        }
        return slotId;
    }

    bool dfsAugment(int u) {
        for (int slot : unitAdj[u]) {
            if (visited[slot]) continue;
            visited[slot] = true;
            if (matchUnit[slot] < 0 || dfsAugment(matchUnit[slot])) {
                matchUnit[slot] = u;
                matchSlot[u] = slot;
                return true;
            }
        }
        return false;
    }

    bool tryAddDemand(const vector<int>& newHouses, const vector<int>& unitPositions) {
        auto oldSlotHouse = slotHouse;
        auto oldMatchUnit = matchUnit;
        auto oldMatchSlot = matchSlot;
        auto oldUnitAdj = unitAdj;

        vector<int> newSlotIds;
        for (int h : newHouses) {
            newSlotIds.push_back(addSlot(h, unitPositions));
        }

        bool allMatched = true;
        for (int slot : newSlotIds) {
            if (matchUnit[slot] == -1) {
                visited.assign(slotHouse.size(), false);
                bool matched = false;
                for (int u = 0; u < numUnits; ++u) {
                    if (matchSlot[u] == -1) {
                        if (dfsAugment(u)) {
                            matched = true;
                            break;
                        }
                    }
                }
                if (!matched) {
                    allMatched = false;
                    break;
                }
            }
        }

        if (allMatched) {
            return true;
        } else {
            slotHouse = oldSlotHouse;
            matchUnit = oldMatchUnit;
            matchSlot = oldMatchSlot;
            unitAdj = oldUnitAdj;
            return false;
        }
    }
};

int main() {
    adj.assign(houseCount, vector<int>());
    // Edges from game 916510456
    vector<pair<int, int>> edges = {
        {0,8},{0,9},{0,15},{0,37},{0,38},{1,8},{1,9},{1,14},{1,36},{1,39},{2,10},{2,20},{2,28},{3,11},{3,21},{3,29},{4,23},{4,27},{5,22},{5,26},{6,23},{6,39},{6,43},{7,22},{7,38},{7,42},{8,27},{8,37},{8,39},{9,26},{9,36},{9,38},{10,20},{10,24},{10,28},{11,21},{11,25},{11,29},{12,16},{12,22},{12,41},{13,17},{13,23},{13,40},{14,36},{14,39},{15,37},{15,38},{16,22},{17,23},{18,25},{18,33},{18,43},{19,24},{19,32},{19,42},{20,24},{20,34},{21,25},{21,35},{22,38},{23,39},{26,31},{27,30},{28,30},{28,34},{29,31},{29,35},{32,41},{32,42},{33,40},{33,43},{34,42},{35,43}
    };
    for (auto& e : edges) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    computeDistances();

    vector<int> unitLocs = {1, 1, 1, 8, 8, 39, 31};
    int M = unitLocs.size();
    BipartiteMatcher matcher(M);

    // Try adding (1, 8, 39) with oppUnits[8] = 2 (so house 8 needs 3 slots, house 1 needs 1 slot, house 39 needs 1 slot)
    vector<int> demand = {1, 8, 8, 8, 39};
    bool res = matcher.tryAddDemand(demand, unitLocs);
    cout << "tryAddDemand for {1, 8, 8, 8, 39}: " << (res ? "SUCCESS" : "FAILED") << endl;
    for (int u = 0; u < M; ++u) {
        int sl = matcher.matchSlot[u];
        cout << "Unit " << u << " at " << unitLocs[u] << " -> slot " << sl;
        if (sl != -1) cout << " (house " << matcher.slotHouse[sl] << ")";
        cout << endl;
    }

    return 0;
}
