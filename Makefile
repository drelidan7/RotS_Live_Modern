BUILD_DIR := build
SRC_DIR := src
CMAKE := cmake
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_CXX_COMPILER=g++
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt

.PHONY: help configure setup build test run smoke-account format clean

help:
	@printf "Available targets:\n"
	@printf "  make configure      Configure the CMake build in %s\n" "$(BUILD_DIR)"
	@printf "  make setup          Create runtime directories and bootstrap files\n"
	@printf "  make build          Build the ageland server binary\n"
	@printf "  make test           Run the C++ unit tests\n"
	@printf "  make smoke-account  Build the game/proxy and run the account smoke flow\n"
	@printf "  make format         Run clang-format via the CMake target\n"
	@printf "  make run            Build and start the server in the foreground\n"
	@printf "  make clean          Clean the configured CMake build tree\n"

$(CMAKE_CACHE):
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS)

configure: $(CMAKE_CACHE)

setup: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target setup

build: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland -j16

# rots_platform_linkcheck / rots_core_linkcheck / rots_entity_linkcheck / rots_persist_linkcheck /
# rots_world_linkcheck must be built explicitly: the PlatformLayerAcyclicity / CoreLayerAcyclicity /
# EntityLayerAcyclicity / PersistLayerAcyclicity / WorldLayerAcyclicity CTests execute their binaries,
# and this recipe builds named targets (not `all`), so omitting either leaves
# its test "Not Run" (as the i386 battery caught for the platform check).
# The explicit reconfigure matters when a NEW top-level target was added since
# the tree was generated: `cmake --build` regenerates the build system mid-run,
# but GNU make has already loaded the stale top-level Makefile and fails with
# "No rule to make target" for the new goal (one-time, but it breaks CI/battery
# runs on pre-existing trees — as the header-split finalization battery caught
# for rots_core_linkcheck, entity-seed for rots_entity_linkcheck,
# persist-split PS Task 4 for rots_persist_linkcheck, and world-seed Task 5
# for rots_world_linkcheck).
test: $(CMAKE_CACHE)
	+$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland ageland_tests rots_platform_linkcheck rots_core_linkcheck rots_entity_linkcheck rots_persist_linkcheck rots_world_linkcheck -j16
	# cd + bare ctest, NOT `ctest --test-dir`: --test-dir needs CMake >= 3.20, and the
	# i386 container ships ctest 3.18, which silently ignores the flag, looks for tests
	# in the repo root, and reports "No tests were found!!!" with exit code 0.
	cd $(BUILD_DIR) && ctest --output-on-failure

run: build
	./bin/ageland -p 3791

smoke-account: setup build
	cargo build -p proxy
	python3 tools/account_smoke.py

format: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target format

clean:
	@if [ ! -f "$(CMAKE_CACHE)" ]; then \
		printf "No configured CMake build tree found in %s\n" "$(BUILD_DIR)"; \
	else \
		$(CMAKE) --build $(BUILD_DIR) --target clean; \
	fi
