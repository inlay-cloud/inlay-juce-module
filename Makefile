BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: configure test emulator-tests emulator clean

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

test: configure
	cmake --build $(BUILD_DIR) --target UnitTestsRunner EmulatorTests --config $(BUILD_TYPE)
	ctest --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure

emulator-tests: configure
	cmake --build $(BUILD_DIR) --target EmulatorTests --config $(BUILD_TYPE)
	ctest --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure -R '^EmulatorTests$$'

emulator: configure
	cmake --build $(BUILD_DIR) --target HeadlessEmulator --config $(BUILD_TYPE)
	$(BUILD_DIR)/tests/HeadlessEmulator_artefacts/$(BUILD_TYPE)/inlay-juce-emulator --run-dir $(RUN_DIR)

clean:
	cmake -E rm -rf $(BUILD_DIR)
