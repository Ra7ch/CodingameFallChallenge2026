with open("bot.cpp") as f:
    code = f.read()

old_attack = """                for (int u = 0; u < houseCount; ++u) {
                    if (t.hasNode(u)) continue; // NEVER attack our own triangle corner!
                    if (oppUnits[u] >= 2 && t.isNeighbor(u, adj)) {
                        int val = oppUnits[u] * 2;
                        // Huge bonus if target is a corner of an enemy triangle
                        for (const auto& ot : triangles) {
                            if (ot.owner == 1 && ot.hasNode(u)) {
                                val += 8; // Breaking enemy triangle is top priority!
                            }
                        }
                        if (val > maxTargetVal && val >= 6) {
                            maxTargetVal = val;
                            bestTarget = u;
                        }
                    }
                }"""

new_attack = """                for (int u = 0; u < houseCount; ++u) {
                    if (t.hasNode(u)) continue; // NEVER attack our own triangle corner!
                    if (oppUnits[u] >= 2 && t.isNeighbor(u, adj)) {
                        int enemyTrianglesBroken = 0;
                        for (const auto& ot : triangles) {
                            if (ot.owner == 1 && ot.hasNode(u)) {
                                enemyTrianglesBroken++;
                            }
                        }
                        // Only attack if killing >= 3 units, or killing 2 units AND breaking >= 2 enemy triangles
                        bool worthIt = (oppUnits[u] >= 3) || (oppUnits[u] >= 2 && enemyTrianglesBroken >= 2);
                        if (worthIt) {
                            int val = oppUnits[u] * 3 + enemyTrianglesBroken * 10;
                            if (val > maxTargetVal) {
                                maxTargetVal = val;
                                bestTarget = u;
                            }
                        }
                    }
                }"""

code = code.replace(old_attack, new_attack, 1)

with open("bot.cpp", "w") as f:
    f.write(code)

print("Updated attack logic")
