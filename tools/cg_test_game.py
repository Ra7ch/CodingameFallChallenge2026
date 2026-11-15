#!/usr/bin/env python3
import urllib.request
import json
import os
import sys
import time

sys.stdout.reconfigure(line_buffering=True)

token = open(os.path.expanduser("~/.config/codingame/token")).read().strip()
headers = {
    "Authorization": f"Bearer {token}",
    "Content-Type": "application/json",
    "User-Agent": "CodinGame-Agent/1.0"
}

def run_test_game(bot_file, opponents=None, replay_game_id=None):
    code = open(bot_file).read()
    payload = {
        "language": "C++",
        "code": code
    }
    if replay_game_id:
        payload["replay_game_id"] = int(replay_game_id)
    elif opponents:
        payload["opponents"] = [int(x) if str(x).isdigit() else x for x in opponents]
    else:
        payload["opponents"] = ["boss"]

    url = "https://www.codingame.com/api/v1/challenges/fall-challenge-2026/test-games?wait=30"
    req = urllib.request.Request(url, data=json.dumps(payload).encode("utf-8"), headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=45) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            status = data.get("status")
            tg_id = data.get("test_game_id")
            print(f"Test Game {tg_id} status: {status}")
            if status == "running":
                poll_url = f"https://www.codingame.com/api/v1/test-games/{tg_id}?wait=30"
                for _ in range(10):
                    time.sleep(3)
                    p_req = urllib.request.Request(poll_url, headers={"Authorization": f"Bearer {token}", "User-Agent": "CodinGame-Agent/1.0"})
                    with urllib.request.urlopen(p_req, timeout=45) as p_resp:
                        raw = p_resp.read().decode("utf-8")
                        try:
                            p_data = json.loads(raw)
                            if p_data.get("status") == "done":
                                return p_data
                        except Exception:
                            # Might be text log directly
                            return {"log": raw, "status": "done"}
            return data
    except urllib.error.HTTPError as e:
        print("HTTP Error:", e.code, e.read().decode("utf-8"))
        return None

if __name__ == "__main__":
    bot_file = sys.argv[1] if len(sys.argv) > 1 else "bot.cpp"
    opp = sys.argv[2] if len(sys.argv) > 2 else "boss"
    if opp.startswith("replay:"):
        gid = opp.split(":")[1]
        res = run_test_game(bot_file, replay_game_id=gid)
    else:
        res = run_test_game(bot_file, opponents=[opp])
    if res and res.get("log"):
        log = res.get("log")
        tg_id = res.get("test_game_id", "latest")
        with open(f"test_game_{tg_id}.txt", "w") as f:
            f.write(log)
        print(f"Saved full log to test_game_{tg_id}.txt")
        # Print summary lines from the log
        lines = log.split("\n")
        print("=== TEST GAME LOG SUMMARY ===")
        for l in lines[-20:]:
            print(l)
