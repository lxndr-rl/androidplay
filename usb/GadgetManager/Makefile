CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -Iinclude
all: build/gadget_manager build/test_gadget_manager
build/gadget_manager: src/main.cpp src/gadget_manager.cpp include/gadget_manager.hpp
	@mkdir -p build && $(CXX) $(CXXFLAGS) src/main.cpp src/gadget_manager.cpp -o $@
build/test_gadget_manager: tests/test_gadget_manager.cpp src/gadget_manager.cpp include/gadget_manager.hpp
	@mkdir -p build && $(CXX) $(CXXFLAGS) tests/test_gadget_manager.cpp src/gadget_manager.cpp -o $@
test: all
	build/test_gadget_manager
clean:
	rm -rf build
.PHONY: all test clean
