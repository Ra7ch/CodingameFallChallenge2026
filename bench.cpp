
#include <iostream>
#include <vector>
#include <chrono>

using namespace std;

int main() {
    auto t0 = chrono::high_resolution_clock::now();
    int totalCap = 0;
    int demand[64] = {0};
    bool owned[64] = {false};

    for (int iter = 0; iter < 5000; ++iter) {
        // Flat array
        demand[10]++; demand[20]++; demand[30]++; demand[40]++;
        for (int i = 0; i < 50; ++i) {
            owned[i] = (demand[i] > 0);
        }
        for (int t = 0; t < 50; ++t) {
            if (owned[t] && owned[(t+1)%50] && owned[(t+2)%50]) totalCap++;
        }
        demand[10]--; demand[20]--; demand[30]--; demand[40]--;
    }
    auto t1 = chrono::high_resolution_clock::now();
    double ms = chrono::duration<double, milli>(t1 - t0).count();
    cout << "Time for 5000 iterations: " << ms << " ms, totalCap=" << totalCap << endl;
    return 0;
}
