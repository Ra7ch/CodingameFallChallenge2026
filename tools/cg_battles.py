#!/usr/bin/env python3
import urllib.request
import json
import os
import sys

token = open(os.path.expanduser("~/.config/codingame/token")).read().strip()
headers = {"Authorization": f"Bearer {token}", "User-Agent": "CodinGame-Agent/1.0"}

def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    url = f"https://www.codingame.com/api/v1/challenges/fall-challenge-2026/battles?limit={limit}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode())
        battles = data.get("battles", [])
        print(f"=== RECENT BATTLES ({len(battles)}) ===")
        wins = 0
        losses = 0
        draws = 0
        for b in battles:
            gid = b.get("game_id")
            players = b.get("players", [])
            done = b.get("done")
            my_pos = b.get("my_position")
            opp = [p for p in players if not p.get("me")]
            opp_name = opp[0].get("nickname") if opp else "unknown"
            opp_agent = opp[0].get("agent_id") if opp else ""
            if not done:
                res = "RUNNING..."
            elif my_pos == 0:
                res = f"WIN vs {opp_name} (agent {opp_agent})"
                wins += 1
            elif my_pos == 1:
                res = f"LOSS vs {opp_name} (agent {opp_agent})"
                losses += 1
            else:
                res = f"DRAW vs {opp_name}"
                draws += 1
            print(f"Game {gid:9d} | {res:<45} (done={done})")
        print(f"Summary of finished: {wins}W - {draws}D - {losses}L")

if __name__ == "__main__":
    main()
