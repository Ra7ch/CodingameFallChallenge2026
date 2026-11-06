#!/usr/bin/env python3
import urllib.request
import json
import os
import sys

token = open(os.path.expanduser("~/.config/codingame/token")).read().strip()
headers = {
    "Authorization": f"Bearer {token}",
    "Content-Type": "application/json",
    "User-Agent": "CodinGame-Agent/1.0"
}

def submit(bot_file="bot.cpp"):
    code = open(bot_file).read()
    payload = {
        "language": "C++",
        "code": code
    }
    req = urllib.request.Request(
        "https://www.codingame.com/api/v1/challenges/fall-challenge-2026/submissions",
        data=json.dumps(payload).encode("utf-8"),
        headers=headers,
        method="POST"
    )
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        print("Submission response:", json.dumps(data, indent=2))
        return data

if __name__ == "__main__":
    bot_file = sys.argv[1] if len(sys.argv) > 1 else "bot.cpp"
    submit(bot_file)
