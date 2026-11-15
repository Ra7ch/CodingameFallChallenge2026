import urllib.request, json, os, subprocess

token = open(os.path.expanduser('~/.config/codingame/token')).read().strip()
headers = {'Authorization': f'Bearer {token}'}

# Get game 916527964 log
req = urllib.request.Request('https://www.codingame.com/api/v1/games/916527964', headers=headers)
with urllib.request.urlopen(req) as resp:
    log = resp.read().decode('utf-8')

lines = log.splitlines()
opts = {}
for l in lines[1:5]:
    if '=' in l:
        k, v = l.strip().split('=', 1)
        opts[k] = v

nodes_raw = opts['nodes'].split('_')
nodes = [list(map(int, p.split(','))) for p in nodes_raw]
edges_raw = opts['edges'].split('_')
edges = [list(map(int, p.split(','))) for p in edges_raw if p]

print(f"Loaded {len(nodes)} nodes, {len(edges)} edges")

# Let's inspect turn 10 agent 0 stderr from log
for i, l in enumerate(lines):
    if 'stderr: Turn 10' in l:
        print("Log Turn 10:")
        for k in range(i-2, i+6):
            print(lines[k])
        break
