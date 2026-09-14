# Convenience front-end over the CMake build. Everything here is a thin
# wrapper; the actual build rules live in CMakeLists.txt.
#
#   make              build the game, the tests and the bundled Stockfish
#   make run          build and start the game
#   make test         build and run the rule/UCI test suite
#   make release      optimised build in build-release/
#   make package      release build + zip via CPack
#   make stockfish    build only the engine
#   make no-engine    configure without the Stockfish build
#   make clean        remove the build directories
#
# Variables (override on the command line, e.g. make BUILD=out GEN=Ninja):
#   BUILD    build directory                     (default: build)
#   CONFIG   Debug / Release                     (default: Debug)
#   GEN      CMake generator, empty = CMake's default
#   ARCH     Stockfish ARCH: native, x86-64-avx2 (default: native)
#   JOBS     parallel jobs                       (default: all cores)

BUILD   ?= build
CONFIG  ?= Debug
GEN     ?=
ARCH    ?= native
JOBS    ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(CONFIG) -DMISCHESS_STOCKFISH_ARCH=$(ARCH)
ifneq ($(GEN),)
CMAKE_FLAGS += -G "$(GEN)"
endif

# multi-config generators (Visual Studio, Xcode) put binaries in a
# per-config subdirectory; single-config ones (Ninja, Makefiles) do not
EXE_DIR = $(firstword $(wildcard $(BUILD)/$(CONFIG)) $(BUILD))
ifeq ($(OS),Windows_NT)
EXT = .exe
else
EXT =
endif

.PHONY: all configure build run test release package stockfish no-engine clean submodules help

all: build

help:
	@sed -n '2,20p' Makefile

# fetch raylib and Stockfish sources on a fresh clone
submodules:
	git submodule update --init

$(BUILD)/CMakeCache.txt: CMakeLists.txt cmake/Stockfish.cmake | submodules
	cmake -B $(BUILD) $(CMAKE_FLAGS)

configure: $(BUILD)/CMakeCache.txt

build: configure
	cmake --build $(BUILD) --config $(CONFIG) -j $(JOBS)

run: build
	cd $(EXE_DIR) && ./mischess$(EXT)

test: configure
	cmake --build $(BUILD) --config $(CONFIG) -j $(JOBS) --target chess_tests
	$(EXE_DIR)/chess_tests$(EXT)

stockfish: configure
	cmake --build $(BUILD) --config $(CONFIG) -j $(JOBS) --target stockfish

# a separate directory so a release build never clobbers the debug one
release:
	$(MAKE) build BUILD=build-release CONFIG=Release

package: release
	cd build-release && cpack -C Release

no-engine:
	cmake -B $(BUILD) $(CMAKE_FLAGS) -DMISCHESS_BUILD_STOCKFISH=OFF

clean:
	rm -rf $(BUILD) build-release
