BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: configure test clean

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

test: configure
	cmake --build $(BUILD_DIR) --target InlayProductUnlockingTests --config $(BUILD_TYPE)
	ctest --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure

clean:
	cmake -E rm -rf $(BUILD_DIR)
