CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
SRC      := src/main.cpp src/cpu.cpp src/display.cpp src/asm.cpp src/text.cpp
HDR      := src/ember.hpp

build/ember: $(SRC) $(HDR)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

# Те саме з ASan/UBSan: так ловляться виходи за межі масиву.
asan: $(SRC) $(HDR)
	mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -o build/ember-asan $(SRC)

test: build/ember
	sh tests/run.sh build/ember

clean:
	rm -rf build

.PHONY: asan test clean
