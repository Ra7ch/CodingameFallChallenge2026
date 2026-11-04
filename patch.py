with open("bot.cpp") as f:
    code = f.read()

# Add debug logging inside candidate committing
old = """                if (matcher.tryAddDemand(newHouses, unitLocs)) {
                    committedTriangles.insert(cand.triId);
                    committedHouseDemand = tempDemand;
                }"""

new = """                bool success = matcher.tryAddDemand(newHouses, unitLocs);
                cerr << "  Cand (" << t.h1 << "," << t.h2 << "," << t.h3 << ") score=" << cand.score << " newH=" << newHouses.size() << " -> " << (success ? "COMMITTED" : "REJECTED") << "\\n";
                if (success) {
                    committedTriangles.insert(cand.triId);
                    committedHouseDemand = tempDemand;
                }"""

if old in code:
    code = code.replace(old, new)
    with open("bot.cpp", "w") as f:
        f.write(code)
    print("Patched successfully")
else:
    print("Could not find pattern to patch")
