#!/usr/bin/env python3
import urllib.request
import json
import os
import sys

token = open(os.path.expanduser("~/.config/codingame/token")).read().strip()
headers = {"Authorization": f"Bearer {token}", "User-Agent": "CodinGame-Agent/1.0"}

def get_progress():
    url = "https://www.codingame.com/api/v1/challenges/fall-challenge-2026/progress"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode())

def get_leaderboard(limit=50, offset=0):
    url = f"https://www.codingame.com/api/v1/challenges/fall-challenge-2026/leaderboard?limit={limit}&offset={offset}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode())

def main():
    prog = get_progress()
    print("=== MY PROGRESS ===")
    print(f"Rank: #{prog.get('rank')} / {prog.get('total')} (League Rank: #{prog.get('league_rank')} / {prog.get('league_total')})")
    print(f"Score: {prog.get('score')}")
    print(f"League: {prog.get('league')}")
    print(f"Submission: {prog.get('submission_id')} ({prog.get('language')}), In-Progress: {prog.get('in_progress')}, Eval: {prog.get('percentage')}%")
    print(f"Next league opens at: {prog.get('next_league_opens_at')}")

    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    offset = int(sys.argv[2]) if len(sys.argv) > 2 else 0

    lb = get_leaderboard(limit=limit, offset=offset)
    print(f"\n=== LEADERBOARD (Top {limit}, offset {offset}) ===")
    for u in lb.get('rows', []):
        print(f"#{u.get('rank'):3d} | {u.get('nickname', ''):<20} | Score: {u.get('score'):5.2f} | League: {u.get('league'):<6} | Agent: {u.get('agent_id')} | Lang: {u.get('language')}")

if __name__ == "__main__":
    main()
