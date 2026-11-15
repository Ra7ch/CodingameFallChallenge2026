CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall

all: bot

bot: bot.cpp
	$(CXX) $(CXXFLAGS) bot.cpp -o bot

sim: sim_arena.cpp
	$(CXX) $(CXXFLAGS) sim_arena.cpp -o sim_arena

clean:
	rm -f bot sim_arena *.o

.PHONY: all clean sim
