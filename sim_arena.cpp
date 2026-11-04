#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <queue>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <cstring>

using namespace std;

struct SimNode {
    int id, x, y;
    vector<int> neighbors;
    int units[2] = {0, 0};
    int remUnits[2] = {0, 0};
};

struct SimTri {
    int h1, h2, h3; // sorted h1 < h2 < h3
    int owner = -1;
    int initOwner = -1;
    bool canCap[2] = {true, true};

    bool hasNode(int u) const { return h1 == u || h2 == u || h3 == u; }
};

struct BotProc {
    pid_t pid = -1;
    int in_fd = -1;  // write to bot stdin
    int out_fd = -1; // read from bot stdout
    string readBuf;

    bool start(const string& cmd) {
        int p_in[2], p_out[2];
        if (pipe(p_in) < 0 || pipe(p_out) < 0) return false;
        pid = fork();
        if (pid == 0) {
            dup2(p_in[0], STDIN_FILENO);
            dup2(p_out[1], STDOUT_FILENO);
            // Redirect stderr to /dev/null
            FILE* dn = fopen("/dev/null", "w");
            if (dn) dup2(fileno(dn), STDERR_FILENO);
            close(p_in[0]); close(p_in[1]);
            close(p_out[0]); close(p_out[1]);
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(1);
        }
        close(p_in[0]);
        close(p_out[1]);
        in_fd = p_in[1];
        out_fd = p_out[0];
        return true;
    }

    void send(const string& s) {
        size_t off = 0;
        while (off < s.size()) {
            ssize_t w = write(in_fd, s.data() + off, s.size() - off);
            if (w <= 0) break;
            off += w;
        }
    }

    bool recvLine(string& out, int timeoutMs, double& elapsedMs) {
        auto t0 = chrono::high_resolution_clock::now();
        while (true) {
            auto pos = readBuf.find('\n');
            if (pos != string::npos) {
                out = readBuf.substr(0, pos);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                readBuf.erase(0, pos + 1);
                auto t1 = chrono::high_resolution_clock::now();
                elapsedMs = chrono::duration<double, milli>(t1 - t0).count();
                return true;
            }
            auto now = chrono::high_resolution_clock::now();
            double used = chrono::duration<double, milli>(now - t0).count();
            int rem = max(1, timeoutMs - (int)used);
            if (used > timeoutMs) {
                elapsedMs = used;
                return false;
            }
            struct pollfd pfd = {out_fd, POLLIN, 0};
            int pr = poll(&pfd, 1, rem);
            if (pr <= 0) {
                auto t1 = chrono::high_resolution_clock::now();
                elapsedMs = chrono::duration<double, milli>(t1 - t0).count();
                if (elapsedMs > timeoutMs) return false;
                continue;
            }
            char buf[4096];
            ssize_t r = read(out_fd, buf, sizeof(buf));
            if (r <= 0) {
                auto t1 = chrono::high_resolution_clock::now();
                elapsedMs = chrono::duration<double, milli>(t1 - t0).count();
                return false;
            }
            readBuf.append(buf, r);
        }
    }

    void stop() {
        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);
        if (pid > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        in_fd = out_fd = -1;
        pid = -1;
    }
};

struct Board {
    static constexpr int TOP_BORDER = 70;
    static constexpr int BOTTOM_BORDER = 190;
    static constexpr int SIDE_BORDER = 80;
    static constexpr int WIDTH = 1920 - 2 * SIDE_BORDER;
    static constexpr int HEIGHT = 1080 - TOP_BORDER - BOTTOM_BORDER;
    static constexpr int NODE_COUNT = 50;
    static constexpr int NODE_MIN_DIST = 140;
    static constexpr int MAX_PATH_LENGTH = 400;
    static constexpr int MAX_ANGLE = 160;
    static constexpr int MIN_TRIANGLE_COUNT = 20;

    vector<SimNode> nodes;
    vector<SimTri> triangles;
    int scores[2] = {0, 0};
    bool killedSurrounded = false;

    static int dist2(const SimNode& a, const SimNode& b) {
        int dx = a.x - b.x, dy = a.y - b.y;
        return dx * dx + dy * dy;
    }
    static double dist(const SimNode& a, const SimNode& b) {
        return sqrt((double)dist2(a, b));
    }
    static bool ccw(const SimNode& a, const SimNode& b, const SimNode& c) {
        return (long long)(c.y - a.y) * (b.x - a.x) > (long long)(b.y - a.y) * (c.x - a.x);
    }
    static bool isCrossing(const SimNode& a1, const SimNode& a2, const SimNode& b1, const SimNode& b2) {
        if (a1.id == a2.id) return false;
        if (a1.id == b1.id || a2.id == b1.id || a1.id == b2.id || a2.id == b2.id) return false;
        return ccw(a1, b1, b2) != ccw(a2, b1, b2) && ccw(a1, a2, b1) != ccw(a1, a2, b2);
    }
    bool isInside(int nid, int a, int b, int c) const {
        if (nid == a || nid == b || nid == c) return false;
        bool c1 = ccw(nodes[nid], nodes[a], nodes[b]);
        bool c2 = ccw(nodes[nid], nodes[b], nodes[c]);
        bool c3 = ccw(nodes[nid], nodes[c], nodes[a]);
        bool has_neg = !c1 || !c2 || !c3;
        bool has_pos = c1 || c2 || c3;
        return !(has_neg && has_pos);
    }

    void addNeighbor(int u, int v) {
        if (find(nodes[u].neighbors.begin(), nodes[u].neighbors.end(), v) == nodes[u].neighbors.end()) {
            nodes[u].neighbors.push_back(v);
            sort(nodes[u].neighbors.begin(), nodes[u].neighbors.end());
        }
        if (find(nodes[v].neighbors.begin(), nodes[v].neighbors.end(), u) == nodes[v].neighbors.end()) {
            nodes[v].neighbors.push_back(u);
            sort(nodes[v].neighbors.begin(), nodes[v].neighbors.end());
        }
    }
    void removeNeighbor(int u, int v) {
        nodes[u].neighbors.erase(remove(nodes[u].neighbors.begin(), nodes[u].neighbors.end(), v), nodes[u].neighbors.end());
        nodes[v].neighbors.erase(remove(nodes[v].neighbors.begin(), nodes[v].neighbors.end(), u), nodes[v].neighbors.end());
    }
    bool hasEdge(int u, int v) const {
        return binary_search(nodes[u].neighbors.begin(), nodes[u].neighbors.end(), v);
    }

    vector<int> bfs(int start, int blocked = -1) const {
        int n = nodes.size();
        vector<int> d(n, n);
        d[start] = 0;
        queue<int> q;
        q.push(start);
        while (!q.empty()) {
            int cur = q.front(); q.pop();
            for (int nxt : nodes[cur].neighbors) {
                if (d[nxt] < n || nxt == blocked) continue;
                d[nxt] = d[cur] + 1;
                q.push(nxt);
            }
        }
        return d;
    }

    bool isStronglyConnected() const {
        int n = nodes.size();
        for (int blocked = 0; blocked < n; ++blocked) {
            int start = (blocked == 0 ? 1 : 0);
            auto d = bfs(start, blocked);
            int unreach = 0;
            for (int i = 0; i < n; ++i) if (d[i] == n) unreach++;
            if (unreach > 1) return false;
        }
        return true;
    }

    void updateTriangles() {
        vector<SimTri> oldT = triangles;
        triangles.clear();
        int n = nodes.size();
        for (int i = 0; i < n; ++i) {
            for (int j : nodes[i].neighbors) {
                if (j <= i) continue;
                for (int k : nodes[j].neighbors) {
                    if (k <= j || !hasEdge(i, k)) continue;
                    bool anyIn = false;
                    for (int m = 0; m < n; ++m) {
                        if (isInside(m, i, j, k)) { anyIn = true; break; }
                    }
                    if (!anyIn) {
                        SimTri nt; nt.h1 = i; nt.h2 = j; nt.h3 = k;
                        for (const auto& ot : oldT) {
                            if (ot.h1 == i && ot.h2 == j && ot.h3 == k) { nt = ot; break; }
                        }
                        triangles.push_back(nt);
                    }
                }
            }
        }
    }

    void generate(uint64_t seed) {
        mt19937_64 rng(seed);
        vector<pair<int,int>> existingEdges;
        while (true) {
            nodes.clear();
            set<int> nodeDistances;
            int tries = 0;
            while ((int)nodes.size() < NODE_COUNT && tries++ < 5000) {
                int x1 = SIDE_BORDER + (int)(rng() % WIDTH);
                int y1 = TOP_BORDER + (int)(rng() % HEIGHT);
                SimNode n1{(int)nodes.size(), x1, y1, {}, {0,0}, {0,0}};
                SimNode n2{(int)nodes.size() + 1, WIDTH - 1 - (x1 - SIDE_BORDER) + SIDE_BORDER, HEIGHT - 1 - (y1 - TOP_BORDER) + TOP_BORDER, {}, {0,0}, {0,0}};
                if (dist(n1, n2) < NODE_MIN_DIST || nodeDistances.count(dist2(n1, n2))) continue;
                bool bad = false;
                for (const auto& e : nodes) {
                    if (dist(e, n1) < NODE_MIN_DIST || nodeDistances.count(dist2(e, n1))) { bad = true; break; }
                }
                if (bad) continue;
                // Check obtuse angle only against new nodes n1, n2 for speed
                vector<SimNode> cand = nodes;
                cand.push_back(n1); cand.push_back(n2);
                int sz = cand.size();
                for (int idxNew = sz - 2; idxNew < sz && !bad; ++idxNew) {
                    for (int i1 = 0; i1 < idxNew && !bad; ++i1) {
                        double c = dist(cand[i1], cand[idxNew]);
                        if (c > 2 * MAX_PATH_LENGTH) continue;
                        for (int i2 = i1 + 1; i2 < idxNew; ++i2) {
                            double a = dist(cand[i1], cand[i2]);
                            double b = dist(cand[i2], cand[idxNew]);
                            if (a > MAX_PATH_LENGTH && b > MAX_PATH_LENGTH && c > MAX_PATH_LENGTH) continue;
                            if (a > MAX_PATH_LENGTH || b > MAX_PATH_LENGTH || c > MAX_PATH_LENGTH) continue;
                            double al = acos(clamp((b*b + c*c - a*a)/(2*b*c), -1.0, 1.0)) * 180.0 / M_PI;
                            double be = acos(clamp((a*a + c*c - b*b)/(2*a*c), -1.0, 1.0)) * 180.0 / M_PI;
                            double ga = acos(clamp((a*a + b*b - c*c)/(2*a*b), -1.0, 1.0)) * 180.0 / M_PI;
                            if (al > MAX_ANGLE || be > MAX_ANGLE || ga > MAX_ANGLE) { bad = true; break; }
                        }
                    }
                }
                if (bad) continue;
                nodeDistances.insert(dist2(n1, n2));
                for (const auto& e : nodes) nodeDistances.insert(dist2(e, n1));
                nodes.push_back(n1);
                nodes.push_back(n2);
            }
            if ((int)nodes.size() < 36) continue;

            vector<pair<int,int>> allE;
            for (int i = 0; i < (int)nodes.size(); ++i)
                for (int j = i + 1; j < (int)nodes.size(); ++j)
                    if (dist(nodes[i], nodes[j]) <= MAX_PATH_LENGTH)
                        allE.push_back({i, j});
            sort(allE.begin(), allE.end(), [&](auto& a, auto& b) {
                return dist2(nodes[a.first], nodes[a.second]) < dist2(nodes[b.first], nodes[b.second]);
            });
            existingEdges.clear();
            for (auto& e : allE) {
                bool cross = false;
                for (auto& ex : existingEdges) {
                    if (isCrossing(nodes[e.first], nodes[e.second], nodes[ex.first], nodes[ex.second])) {
                        cross = true; break;
                    }
                }
                if (!cross) {
                    addNeighbor(e.first, e.second);
                    existingEdges.push_back(e);
                }
            }
            triangles.clear();
            updateTriangles();
            if (isStronglyConnected() && (int)triangles.size() >= MIN_TRIANGLE_COUNT) break;
        }

        uniform_real_distribution<double> uni(0.1, 0.5);
        int remCnt = (int)(existingEdges.size() * uni(rng));
        set<pair<int,int>> tested;
        while (remCnt > 0 && !existingEdges.empty()) {
            remCnt -= 2;
            auto e = existingEdges[rng() % existingEdges.size()];
            if (e.first > e.second) swap(e.first, e.second);
            if (tested.count(e)) continue;
            pair<int,int> me = {e.first ^ 1, e.second ^ 1};
            if (me.first > me.second) swap(me.first, me.second);
            tested.insert(e); tested.insert(me);
            removeNeighbor(e.first, e.second);
            removeNeighbor(me.first, me.second);
            updateTriangles();
            if ((int)triangles.size() >= MIN_TRIANGLE_COUNT && isStronglyConnected()) {
                existingEdges.erase(remove(existingEdges.begin(), existingEdges.end(), e), existingEdges.end());
                existingEdges.erase(remove(existingEdges.begin(), existingEdges.end(), me), existingEdges.end());
            } else {
                addNeighbor(e.first, e.second);
                addNeighbor(me.first, me.second);
                updateTriangles();
            }
        }

        vector<int> byX(nodes.size());
        for (int i = 0; i < (int)nodes.size(); ++i) byX[i] = i;
        sort(byX.begin(), byX.end(), [&](int a, int b) { return nodes[a].x < nodes[b].x; });
        for (int i = 0; i < 3; ++i) {
            int u = byX[i];
            nodes[u].units[0] = 1;
            nodes[u ^ 1].units[1] = 1;
        }
        killedSurrounded = false;
        while (finalizeTurn(false)) ;
    }

    int getNodeOwner(int u) const {
        const auto& nd = nodes[u];
        if (!nd.neighbors.empty()) {
            bool all0 = true, all1 = true;
            for (int nb : nd.neighbors) {
                if (!(nodes[nb].units[0] > nodes[nb].units[1])) all0 = false;
                if (!(nodes[nb].units[1] > nodes[nb].units[0])) all1 = false;
            }
            if (all0) return 0;
            if (all1) return 1;
        }
        if (nd.units[0] > nd.units[1]) return 0;
        if (nd.units[1] > nd.units[0]) return 1;
        return -1;
    }

    bool finalizeTurn(bool addScore = true) {
        for (auto& nd : nodes) {
            nd.remUnits[0] = nd.units[0];
            nd.remUnits[1] = nd.units[1];
        }
        for (auto& t : triangles) {
            for (int i = 0; i < 2; ++i) {
                if (t.owner != -1) t.canCap[i] = (t.owner != i);
                else if (nodes[t.h1].units[i] == 0 && nodes[t.h2].units[i] == 0 && nodes[t.h3].units[i] == 0)
                    t.canCap[i] = true;
            }
        }
        bool surr = false;
        if (!killedSurrounded) {
            killedSurrounded = true;
            vector<bool> adv[2];
            adv[0].assign(nodes.size(), false);
            adv[1].assign(nodes.size(), false);
            for (size_t u = 0; u < nodes.size(); ++u) {
                if (nodes[u].units[0] > nodes[u].units[1]) adv[0][u] = true;
                if (nodes[u].units[1] > nodes[u].units[0]) adv[1][u] = true;
            }
            for (int p = 0; p < 2; ++p) {
                int opp = 1 - p;
                for (size_t u = 0; u < nodes.size(); ++u) {
                    if (nodes[u].units[p] > 0 && !nodes[u].neighbors.empty()) {
                        bool allOpp = true;
                        for (int nb : nodes[u].neighbors) if (!adv[opp][nb]) { allOpp = false; break; }
                        if (allOpp) {
                            nodes[u].units[p] = 0;
                            surr = true;
                        }
                    }
                }
            }
        }
        bool cap = false;
        for (auto& t : triangles) {
            int oldO = t.owner;
            if (t.canCap[0] && getNodeOwner(t.h1) == 0 && getNodeOwner(t.h2) == 0 && getNodeOwner(t.h3) == 0) t.owner = 0;
            if (t.canCap[1] && getNodeOwner(t.h1) == 1 && getNodeOwner(t.h2) == 1 && getNodeOwner(t.h3) == 1) t.owner = 1;
            if (t.owner != oldO) cap = true;
        }
        if (surr || cap) return true;

        for (auto& t : triangles) {
            if (addScore && t.owner != -1) scores[t.owner]++;
            t.initOwner = t.owner;
        }
        return false;
    }

    string getInput(bool initial, int p) const {
        int opp = 1 - p;
        ostringstream ss;
        if (initial) {
            ss << nodes.size() << "\n";
            for (const auto& nd : nodes) ss << nd.id << " " << nd.x << " " << nd.y << "\n";
        }
        ss << scores[p] << " " << scores[opp] << "\n";
        for (const auto& nd : nodes) ss << nd.id << " " << nd.units[p] << " " << nd.units[opp] << "\n";
        vector<pair<int,int>> links;
        for (const auto& nd : nodes)
            for (int nb : nd.neighbors)
                if (nb > nd.id) links.push_back({nd.id, nb});
        ss << links.size() << "\n";
        for (auto& l : links) ss << l.first << " " << l.second << "\n";
        ss << triangles.size() << "\n";
        for (const auto& t : triangles) {
            int ow = (t.owner == -1 ? -1 : (t.owner == p ? 0 : 1));
            ss << t.h1 << " " << t.h2 << " " << t.h3 << " " << ow << " " << (t.canCap[p] ? 1 : 0) << " " << (t.canCap[opp] ? 1 : 0) << "\n";
        }
        ss << "0\n"; // Silver league allowNewEdges = false
        return ss.str();
    }

    int findTri(int a, int b, int c) const {
        int arr[3] = {a, b, c};
        sort(arr, arr + 3);
        for (size_t i = 0; i < triangles.size(); ++i)
            if (triangles[i].h1 == arr[0] && triangles[i].h2 == arr[1] && triangles[i].h3 == arr[2])
                return (int)i;
        return -1;
    }

    bool triCanUse(int tid, int p, int reqU) const {
        if (tid < 0) return false;
        const auto& t = triangles[tid];
        return t.owner == p && t.initOwner == p &&
               nodes[t.h1].remUnits[p] >= reqU &&
               nodes[t.h2].remUnits[p] >= reqU &&
               nodes[t.h3].remUnits[p] >= reqU;
    }

    void triUse(int tid, int reqU) {
        auto& t = triangles[tid];
        int p = t.owner;
        for (int c : {t.h1, t.h2, t.h3}) {
            int take = min(reqU, nodes[c].units[p]);
            nodes[c].units[p] -= take;
            nodes[c].remUnits[p] -= take;
        }
        t.canCap[p] = false;
        t.owner = -1;
    }

    bool triHasNeighbor(int tid, int target) const {
        const auto& t = triangles[tid];
        return hasEdge(t.h1, target) || hasEdge(t.h2, target) || hasEdge(t.h3, target);
    }

    struct ParsedCmd {
        int type; // 0=ATTACK, 1=MOVE, 2=SPAWN
        int a, b, c, d;
    };

    bool applyTurn(const string& out0, const string& out1, int& foulPlayer) {
        vector<ParsedCmd> cmds[2];
        string outs[2] = {out0, out1};
        for (int p = 0; p < 2; ++p) {
            stringstream ss(outs[p]);
            string item;
            while (getline(ss, item, ';')) {
                while (!item.empty() && isspace(item.front())) item.erase(item.begin());
                while (!item.empty() && isspace(item.back())) item.pop_back();
                if (item.empty()) continue;
                stringstream ls(item);
                string kw; ls >> kw;
                for (char& ch : kw) ch = toupper(ch);
                if (kw == "WAIT" || kw == "MSG" || kw == "CIRCLE" || kw == "LINE" || kw == "RECT" || kw == "TEXT") continue;
                if (kw == "ATTACK") {
                    int n1, n2, n3, tgt;
                    if (!(ls >> n1 >> n2 >> n3 >> tgt)) { foulPlayer = p; return false; }
                    cmds[p].push_back({0, n1, n2, n3, tgt});
                } else if (kw == "MOVE") {
                    int fr, to, amt;
                    if (!(ls >> fr >> to >> amt)) { foulPlayer = p; return false; }
                    if (amt > 0) cmds[p].push_back({1, fr, to, amt, 0});
                } else if (kw == "SPAWN") {
                    int sp, n2, n3;
                    if (!(ls >> sp >> n2 >> n3)) { foulPlayer = p; return false; }
                    cmds[p].push_back({2, sp, n2, n3, 0});
                } else {
                    foulPlayer = p;
                    return false;
                }
            }
        }

        // Priority 0: ATTACK (sequential per player, simultaneous across players)
        size_t idxA[2] = {0, 0};
        while (true) {
            vector<pair<int, ParsedCmd>> batch;
            for (int p = 0; p < 2; ++p) {
                while (idxA[p] < cmds[p].size()) {
                    auto cm = cmds[p][idxA[p]];
                    if (cm.type != 0) { idxA[p]++; continue; }
                    idxA[p]++;
                    int tid = findTri(cm.a, cm.b, cm.c);
                    if (tid >= 0 && triCanUse(tid, p, 1) && triHasNeighbor(tid, cm.d)) {
                        batch.push_back({p, cm});
                        break;
                    }
                }
            }
            if (batch.empty()) break;
            for (auto& [p, cm] : batch) {
                int tid = findTri(cm.a, cm.b, cm.c);
                // Note: referee strict=false allows simultaneous attack even if corner was hit in same subframe!
                if (tid >= 0 && triangles[tid].owner == p) {
                    triUse(tid, 1);
                    int opp = 1 - p;
                    nodes[cm.d].units[opp] = 0;
                    nodes[cm.d].remUnits[opp] = 0;
                }
            }
        }

        // Priority 1: MOVE
        for (int p = 0; p < 2; ++p) {
            for (const auto& cm : cmds[p]) {
                if (cm.type != 1) continue;
                int fr = cm.a, to = cm.b, amt = cm.c;
                if (fr < 0 || fr >= (int)nodes.size() || to < 0 || to >= (int)nodes.size() || fr == to) continue;
                int take = min(amt, nodes[fr].remUnits[p]);
                if (take <= 0) continue;
                auto d = bfs(to);
                if (d[fr] >= (int)nodes.size()) continue;
                for (int nb : nodes[fr].neighbors) {
                    if (d[nb] < d[fr]) {
                        nodes[fr].units[p] -= take;
                        nodes[fr].remUnits[p] -= take;
                        nodes[nb].units[p] += take;
                        break;
                    }
                }
            }
        }

        // Priority 2: SPAWN
        for (int p = 0; p < 2; ++p) {
            for (const auto& cm : cmds[p]) {
                if (cm.type != 2) continue;
                int tid = findTri(cm.a, cm.b, cm.c);
                if (tid >= 0 && triCanUse(tid, p, 0)) {
                    triUse(tid, 0);
                    nodes[cm.a].units[p]++;
                }
            }
        }

        killedSurrounded = false;
        while (finalizeTurn(true)) ;
        return true;
    }
};

struct MatchResult {
    int winner; // 0, 1, or -1 (draw)
    int score0, score1;
    int turns;
    string reason;
    double maxMs0 = 0, maxMs1 = 0;
};

MatchResult runGame(const Board& initBoard, const string& cmd0, const string& cmd1) {
    Board b = initBoard;
    BotProc p0, p1;
    p0.start(cmd0);
    p1.start(cmd1);
    MatchResult res{-1, 0, 0, 0, "200 turns", 0, 0};

    for (int turn = 1; turn <= 200; ++turn) {
        int u[2] = {0, 0}, tr[2] = {0, 0};
        for (const auto& nd : b.nodes) { u[0] += nd.units[0]; u[1] += nd.units[1]; }
        for (const auto& t : b.triangles) if (t.owner != -1) tr[t.owner]++;

        // Check game end conditions (Referee.java lines 60-72)
        bool dead0 = false, dead1 = false;
        string r0, r1;
        if (u[0] == 0 && tr[0] == 0) { dead0 = true; r0 = "eliminated"; }
        else if (u[0] == 1 && tr[0] == 0 && b.scores[0] < b.scores[1]) { dead0 = true; r0 = "cant_score"; }
        else if (tr[1] >= b.triangles.size() * 0.8) { dead0 = true; r0 = "dominated"; }

        if (u[1] == 0 && tr[1] == 0) { dead1 = true; r1 = "eliminated"; }
        else if (u[1] == 1 && tr[1] == 0 && b.scores[1] < b.scores[0]) { dead1 = true; r1 = "cant_score"; }
        else if (tr[0] >= b.triangles.size() * 0.8) { dead1 = true; r1 = "dominated"; }

        if (dead0 || dead1) {
            if (dead0) b.scores[0] = -1;
            if (dead1) b.scores[1] = -1;
            res.turns = turn;
            res.reason = dead0 && dead1 ? "both_dead" : (dead0 ? "P1_" + r0 : "P0_" + r1);
            break;
        }

        string in0 = b.getInput(turn == 1, 0);
        string in1 = b.getInput(turn == 1, 1);
        int limitMs = (turn == 1 ? 1000 : 100); // generous pipe Slack locally, we report exact maxMs!

        p0.send(in0);
        double ms0 = 0, ms1 = 0;
        string out0, out1;
        bool ok0 = p0.recvLine(out0, limitMs, ms0);
        p1.send(in1);
        bool ok1 = p1.recvLine(out1, limitMs, ms1);

        res.maxMs0 = max(res.maxMs0, turn == 1 ? 0.0 : ms0);
        res.maxMs1 = max(res.maxMs1, turn == 1 ? 0.0 : ms1);

        if (!ok0 || !ok1) {
            if (!ok0) b.scores[0] = -1;
            if (!ok1) b.scores[1] = -1;
            res.turns = turn;
            res.reason = !ok0 ? "P0_timeout" : "P1_timeout";
            break;
        }

        int foul = -1;
        if (!b.applyTurn(out0, out1, foul)) {
            b.scores[foul] = -1;
            res.turns = turn;
            res.reason = (foul == 0 ? "P0_foul" : "P1_foul");
            break;
        }
        res.turns = turn;
    }

    p0.stop();
    p1.stop();
    res.score0 = b.scores[0];
    res.score1 = b.scores[1];
    res.winner = (res.score0 > res.score1 ? 0 : (res.score1 > res.score0 ? 1 : -1));
    return res;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        cerr << "Usage: ./sim_arena <botA_cmd> <botB_cmd> [num_seeds=10]\n";
        return 1;
    }
    string botA = argv[1];
    string botB = argv[2];
    int numSeeds = (argc >= 4 ? stoi(argv[3]) : 10);

    int winsA = 0, winsB = 0, draws = 0;
    double worstMsA = 0, worstMsB = 0;

    for (int s = 1; s <= numSeeds; ++s) {
        Board initB;
        initB.generate(100000ULL + s * 13337ULL);

        // Game 1: A as P0, B as P1
        auto r1 = runGame(initB, botA, botB);
        worstMsA = max(worstMsA, r1.maxMs0);
        worstMsB = max(worstMsB, r1.maxMs1);
        if (r1.winner == 0) winsA++; else if (r1.winner == 1) winsB++; else draws++;
        cout << "Seed " << s << "a (A=P0): " << r1.score0 << " vs " << r1.score1
             << " (T" << r1.turns << " " << r1.reason << ") maxMs=[" << r1.maxMs0 << ", " << r1.maxMs1 << "]\n";

        // Game 2: B as P0, A as P1
        auto r2 = runGame(initB, botB, botA);
        worstMsB = max(worstMsB, r2.maxMs0);
        worstMsA = max(worstMsA, r2.maxMs1);
        if (r2.winner == 1) winsA++; else if (r2.winner == 0) winsB++; else draws++;
        cout << "Seed " << s << "b (A=P1): " << r2.score1 << " vs " << r2.score0
             << " (T" << r2.turns << " " << r2.reason << ") maxMs=[" << r2.maxMs1 << ", " << r2.maxMs0 << "]\n";
    }

    int total = winsA + winsB + draws;
    cout << "\n=== SUMMARY (" << total << " games) ===\n";
    cout << "Bot A (" << botA << "): " << winsA << "W / " << draws << "D / " << winsB << "L ("
         << (100.0 * (winsA + 0.5 * draws) / total) << "%), maxTurnMs=" << worstMsA << " ms\n";
    cout << "Bot B (" << botB << "): " << winsB << "W / " << draws << "D / " << winsA << "L, maxTurnMs=" << worstMsB << " ms\n";
    return 0;
}
