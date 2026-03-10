BUILD_DIR ?= build

.PHONY: all build test clean format configure-dev configure-release build-dev build-release

all: build

configure-dev:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DBASL_BUILD_TESTS=ON

configure-release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DBASL_BUILD_TESTS=ON

build: configure-dev
	cmake --build $(BUILD_DIR)

build-dev: configure-dev
	cmake --build $(BUILD_DIR)

build-release: configure-release
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

format:
	@echo "Formatting is not configured yet for the C rewrite scaffold."

clean:
	cmake -E rm -rf $(BUILD_DIR)
