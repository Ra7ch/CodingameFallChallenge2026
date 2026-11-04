with open("bot.cpp") as f:
    code = f.read()

# Fix 1: ATTACK target validation
old_attack_loop = """                for (int u = 0; u < houseCount; ++u) {
                    if (oppUnits[u] >= 2 && t.isNeighbor(u, adj)) {"""

new_attack_loop = """                for (int u = 0; u < houseCount; ++u) {
                    if (t.hasNode(u)) continue; // NEVER attack our own triangle corner!
                    if (oppUnits[u] >= 2 && t.isNeighbor(u, adj)) {"""

code = code.replace(old_attack_loop, new_attack_loop)

# Fix 2: Anti-surround evacuation check before building actions
old_build_actions = """        // Build MOVE actions
        for (auto const& [edge, cnt] : moveCount) {"""

new_build_actions = """        // PHASE 5: ANTI-SURROUND EVACUATION
        // Check every house where our units are planned to end up.
        // If all neighbors have strictly more opponent units, units on this house will be wiped out!
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
                    // Evacuate units from u to the safest neighbor
                    int bestSafeNeighbor = -1;
                    int minOpp = INF;
                    for (int v : adj[u]) {
                        if (oppUnits[v] < minOpp) {
                            minOpp = oppUnits[v];
                            bestSafeNeighbor = v;
                        }
                    }
                    if (bestSafeNeighbor != -1) {
                        cerr << "Anti-Surround rescue: Evacuating house " << u << " -> " << bestSafeNeighbor << "\\n";
                        // Find any moves targeting u or staying at u and redirect to bestSafeNeighbor
                        int unitsAtU = plannedUnits[u];
                        moveCount[{u, bestSafeNeighbor}] += unitsAtU;
                        plannedUnits[u] = 0;
                        plannedUnits[bestSafeNeighbor] += unitsAtU;
                    }
                }
            }
        }

        // Build MOVE actions
        for (auto const& [edge, cnt] : moveCount) {"""

code = code.replace(old_build_actions, new_build_actions)

with open("bot.cpp", "w") as f:
    f.write(code)

print("Applied fixes successfully")
