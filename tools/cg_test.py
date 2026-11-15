#!/usr/bin/env python3
import json
import os
import sys
import time
import urllib.request
import urllib.error

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 cg_test.py <opponent_id_or_name> <tag> [bot_file=bot.cpp]")
        sys.exit(1)

    opp_arg = sys.argv[1]
    tag = sys.argv[2]
    bot_file = sys.argv[3] if len(sys.argv) >= 4 else "bot.cpp"

    token_path = os.path.expanduser("~/.config/codingame/token")
    token = open(token_path).read().strip()
    code = open(bot_file).read()

    if opp_arg.startswith("replay:"):
        payload = {
            "language": "C++",
            "code": code,
            "replay_game_id": int(opp_arg.split(":", 1)[1])
        }
    else:
        opp = int(opp_arg) if opp_arg.isdigit() else opp_arg
        payload = {
            "language": "C++",
            "code": code,
            "opponents": [opp]
        }

    body = json.dumps(payload).encode("utf-8")

    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
        "User-Agent": "CodinGame-Agent/1.0"
    }

    req = urllib.request.Request(
        "https://www.codingame.com/api/v1/challenges/fall-challenge-2026/test-games?wait=30",
        data=body,
        headers=headers,
        method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=45) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace")
        print(f"HTTP {e.code}: {err_body}")
        sys.exit(1)

    while data.get("status") not in ("done", "failed") and "test_game_id" in data:
        tgid = data["test_game_id"]
        poll_req = urllib.request.Request(
            f"https://www.codingame.com/api/v1/test-games/{tgid}?wait=30",
            headers={"Authorization": f"Bearer {token}", "User-Agent": "CodinGame-Agent/1.0"}
        )
        with urllib.request.urlopen(poll_req, timeout=45) as resp:
            raw = resp.read().decode("utf-8")
            sraw = raw.strip()
            if sraw.startswith("{"):
                try:
                    data = json.loads(sraw)
                except Exception as e:
                    time.sleep(1)
            elif sraw.startswith("status:"):
                first_line = sraw.splitlines()[0]
                status_val = first_line.split(":", 1)[1].strip()
                data = {"status": status_val, "log": raw}
            else:
                time.sleep(1)

    json.dump(data, open(f"{tag}.json", "w"), indent=2)
    log = data.get("log", "")
    open(f"{tag}.log", "w").write(log)

    print("Status:", data.get("status"))
    print("Limits:", json.dumps(data.get("limits", {})))
    if data.get("status") != "done":
        print("Error/Hint:", data.get("detail"), data.get("hint"), data.get("error"))
        return

    lines = log.strip().splitlines()
    print("--- HEADER ---")
    for l in lines[:6]:
        print(l)
    print("--- STDERR HIGHLIGHTS ---")
    stderr_lines = [l for l in lines if "U:" in l or "timeout" in l.lower() or "error" in l.lower()]
    for l in stderr_lines[:10]:
        print(l)
    if len(stderr_lines) > 20:
        print("...")
    for l in stderr_lines[-10:]:
        if l not in stderr_lines[:10]:
            print(l)
    print("--- TAIL ---")
    for l in lines[-16:]:
        print(l)

if __name__ == "__main__":
    main()
