with open("bot.cpp") as f:
    code = f.read()

# Replace Phase 4 with the new TargetCorner matching
phase4_old_start = "// PHASE 4: SMART SURPLUS UNITS DISPERSAL"
phase4_old_end = "// Build MOVE actions"

idx1 = code.find(phase4_old_start)
idx2 = code.find(phase4_old_end)

if idx1 != -1 and idx2 != -1:
    new_phase4 = """// PHASE 4: SMART SURPLUS UNITS ADVANCEMENT
            vector<int> surplusUnits;
            for (int u = 0; u < M; ++u) {
                if (!unitAssigned[u]) {
                    surplusUnits.push_back(u);
                }
            }

            if (!surplusUnits.empty()) {
                struct TargetCorner {
                    int house;
                    int priority;
                };
                vector<TargetCorner> neededCorners;

                // 1. Defend / Reinforce contested frontline houses
                for (int h = 0; h < houseCount; ++h) {
                    if (oppUnits[h] > 0 && plannedUnits[h] <= oppUnits[h]) {
                        neededCorners.push_back({h, 5000});
                    }
                }

                // 2. Candidate uncaptured triangles
                struct TriScore {
                    int triId;
                    double score;
                };
                vector<TriScore> triScores;
                for (const auto& t : triangles) {
                    if (!t.meCan && t.owner != 0) continue;
                    if (committedTriangles.count(t.id)) continue;

                    double dSum = 0;
                    for (int c : {t.h1, t.h2, t.h3}) {
                        int minD = INF;
                        for (int su : surplusUnits) {
                            minD = min(minD, distMatrix[unitLocs[su]][c]);
                        }
                        dSum += (minD < INF ? minD : 20);
                    }

                    double score = 1000.0 / (1.0 + dSum * 2.0);
                    if (t.owner == 1) score *= 2.0;
                    triScores.push_back({t.id, score});
                }

                sort(triScores.begin(), triScores.end(), [](const TriScore& a, const TriScore& b) {
                    return a.score > b.score;
                });

                // Add corners of top target triangles that still need units
                for (const auto& ts : triScores) {
                    const auto& t = triangles[ts.triId];
                    for (int c : {t.h1, t.h2, t.h3}) {
                        int req = max(1, oppUnits[c] + 1);
                        if (plannedUnits[c] < req) {
                            neededCorners.push_back({c, (int)ts.score});
                        }
                    }
                }

                // Match surplus units to needed corners (min distance)
                vector<bool> cornerAssigned(neededCorners.size(), false);
                for (int su : surplusUnits) {
                    int from = unitLocs[su];
                    int bestC = -1;
                    int minD = INF;

                    for (size_t i = 0; i < neededCorners.size(); ++i) {
                        if (cornerAssigned[i]) continue;
                        int h = neededCorners[i].house;
                        if (isInEnemyKillZone(h, triangles, oppUnits) && plannedUnits[h] >= 1) continue;

                        int d = distMatrix[from][h];
                        if (d < minD) {
                            minD = d;
                            bestC = i;
                        }
                    }

                    if (bestC != -1) {
                        cornerAssigned[bestC] = true;
                        int target = neededCorners[bestC].house;
                        plannedUnits[target]++;

                        if (from != target) {
                            int nextStep = from;
                            for (int v : adj[from]) {
                                if (distMatrix[v][target] < distMatrix[nextStep][target]) {
                                    nextStep = v;
                                }
                            }
                            if (nextStep != from) {
                                moveCount[{from, nextStep}]++;
                            }
                        }
                    }
                }
            }
        }

        """
    code = code[:idx1] + new_phase4 + code[idx2:]
    with open("bot.cpp", "w") as f:
        f.write(code)
    print("Replaced Phase 4 successfully")
else:
    print(f"Indices not found: {idx1}, {idx2}")
