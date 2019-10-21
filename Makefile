CXXFLAGS = -std=c++17 -Wall -pthread

# check if DEBUG=1 is set on the command line
ifeq ($(DEBUG),1)
  CXXFLAGS := $(CXXFLAGS) -O0 -g
else
  CXXFLAGS := $(CXXFLAGS) -O3 -march=native -flto
endif

CXX = g++

kirin: competitor.o
	$(CXX) $(CXXFLAGS) -o kirin kirin.o competitor.o


competitor.o: competitor.cpp
	$(CXX) $(CXXFLAGS) -c competitor.cpp

clean:
	rm -f competitor.o kirin
