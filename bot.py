import sys

def debug(*args):
    print(*args, file=sys.stderr, flush=True)

def main():
    house_count_str = sys.stdin.readline()
    if not house_count_str:
        return
    house_count = int(house_count_str.strip())
    houses = {}
    for _ in range(house_count):
        parts = sys.stdin.readline().split()
        hid = int(parts[0])
        hx = int(parts[1])
        hy = int(parts[2])
        houses[hid] = (hx, hy)

    debug(f"Initialized with {house_count} houses")

    turn = 0
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        turn += 1
        my_score, opp_score = [int(x) for x in line.split()]
        
        my_units = {}
        opp_units = {}
        for _ in range(house_count):
            hid, u_me, u_opp = [int(x) for x in sys.stdin.readline().split()]
            my_units[hid] = u_me
            opp_units[hid] = u_opp
            
        path_count = int(sys.stdin.readline().strip())
        adj = {}
        for _ in range(path_count):
            h1, h2 = [int(x) for x in sys.stdin.readline().split()]
            adj.setdefault(h1, []).append(h2)
            adj.setdefault(h2, []).append(h1)
            
        triangle_count = int(sys.stdin.readline().strip())
        triangles = []
        for _ in range(triangle_count):
            parts = sys.stdin.readline().split()
            h1, h2, h3 = int(parts[0]), int(parts[1]), int(parts[2])
            owner = int(parts[3])
            me_can = parts[4] == '1' or parts[4].lower() == 'true'
            opp_can = parts[5] == '1' or parts[5].lower() == 'true'
            triangles.append((h1, h2, h3, owner, me_can, opp_can))
            
        linkable_count = int(sys.stdin.readline().strip())
        linkable = []
        for _ in range(linkable_count):
            h1, h2 = [int(x) for x in sys.stdin.readline().split()]
            linkable.append((h1, h2))

        debug(f"Turn {turn}: Score {my_score} - {opp_score}, my units: {sum(my_units.values())}, triangles: {len(triangles)}")

        commands = []
        for h1, h2, h3, owner, me_can, opp_can in triangles:
            if owner == 0:
                commands.append(f"SPAWN {h1} {h2} {h3}")
                break

        if not commands:
            commands.append("WAIT")

        print(";".join(commands), flush=True)

if __name__ == "__main__":
    main()
