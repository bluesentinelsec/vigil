BUILD_DIR ?= build

.PHONY: all build test clean format configure-dev configure-release build-dev build-release perf coverage

all: build

configure-dev:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DVIGIL_BUILD_TESTS=ON

configure-release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DVIGIL_BUILD_TESTS=ON

build: configure-dev
	cmake --build $(BUILD_DIR)

build-dev: configure-dev
	cmake --build $(BUILD_DIR)

build-release: configure-release
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

perf: build-release
	python3 scripts/run_benchmarks.py \
		--vigil-bin $(BUILD_DIR)/vigil \
		--manifest benchmarks/manifest.json \
		--output $(BUILD_DIR)/benchmarks.json

coverage:
	cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DVIGIL_BUILD_TESTS=ON -DVIGIL_ENABLE_COVERAGE=ON
	cmake --build build-coverage
	ctest --test-dir build-coverage --output-on-failure
	python3 scripts/check_test_surface.py
	scripts/generate_coverage_reports.sh build-coverage coverage-artifacts
	python3 scripts/check_coverage.py \
		--summary coverage-artifacts/coverage-summary.json \
		--details coverage-artifacts/coverage.json \
		--thresholds coverage/thresholds.json \
		--base-ref origin/main

format:
	scripts/run_clang_format.sh --in-place

clean:
	cmake -E rm -rf $(BUILD_DIR)
