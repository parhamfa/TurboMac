SHELL := /bin/bash

SDK := $(shell xcrun --sdk macosx --show-sdk-path)
CXX := $(shell xcrun --find clang++)
CC := $(shell xcrun --find clang)
ARCH := x86_64
MIN_VERSION := 13.0
BUILD := build
OBJ := $(BUILD)/obj
RELEASE := $(BUILD)/Release
KEXT := $(RELEASE)/TurboMac.kext
KEXT_MACOS := $(KEXT)/Contents/MacOS
PACKAGE := $(BUILD)/package

USER_CXXFLAGS := -arch $(ARCH) -isysroot "$(SDK)" -mmacosx-version-min=$(MIN_VERSION) -std=c++17 -O2 -Wall -Wextra -Werror -IShared -IDaemon
USER_CFLAGS := -arch $(ARCH) -isysroot "$(SDK)" -mmacosx-version-min=$(MIN_VERSION) -std=c11 -O2 -Wall -Wextra -Werror -IDaemon
KEXT_CXXFLAGS := -arch $(ARCH) -isysroot "$(SDK)" -mmacosx-version-min=$(MIN_VERSION) -std=gnu++17 -O2 -mkernel -fapple-kext -fno-builtin -fno-exceptions -fno-rtti -fno-common -nostdinc++ -Wall -Wextra -Werror -I"$(SDK)/System/Library/Frameworks/Kernel.framework/Headers" -IShared -ITurboMac

.PHONY: all build package sign verify test replay clean

all: package

build: $(KEXT_MACOS)/TurboMac $(RELEASE)/turbomacd $(RELEASE)/turbomacctl $(RELEASE)/turbomac-avx2-load

$(OBJ):
	mkdir -p "$@"

$(KEXT_MACOS):
	mkdir -p "$@"

$(RELEASE):
	mkdir -p "$@"

$(OBJ)/TurboMac.o: TurboMac/TurboMac.cpp TurboMac/TurboMac.h TurboMac/TurboMacUserClient.h Shared/TurboMacProtocol.h Shared/RAPLCodec.h Shared/DriverValidation.h | $(OBJ)
	$(CXX) $(KEXT_CXXFLAGS) -c "$<" -o "$@"

$(OBJ)/TurboMacUserClient.o: TurboMac/TurboMacUserClient.cpp TurboMac/TurboMacUserClient.h TurboMac/TurboMac.h Shared/TurboMacProtocol.h | $(OBJ)
	$(CXX) $(KEXT_CXXFLAGS) -c "$<" -o "$@"

$(KEXT_MACOS)/TurboMac: $(OBJ)/TurboMac.o $(OBJ)/TurboMacUserClient.o TurboMac/Info.plist | $(KEXT_MACOS)
	$(CXX) -arch $(ARCH) -isysroot "$(SDK)" -mmacosx-version-min=$(MIN_VERSION) -Wl,-kext -nostdlib -L"$(SDK)/usr/lib" -lkmodc++ -lkmod $(OBJ)/TurboMac.o $(OBJ)/TurboMacUserClient.o -o "$@"
	install -m 0644 TurboMac/Info.plist "$(KEXT)/Contents/Info.plist"

$(OBJ)/SMCReader.o: Daemon/SMCReader.c Daemon/SMCReader.h | $(OBJ)
	$(CC) $(USER_CFLAGS) -c "$<" -o "$@"

$(OBJ)/DriverClient.o: Daemon/DriverClient.cpp Daemon/DriverClient.h Shared/TurboMacProtocol.h | $(OBJ)
	$(CXX) $(USER_CXXFLAGS) -c "$<" -o "$@"

$(OBJ)/Policy.o: Daemon/Policy.cpp Daemon/Policy.h | $(OBJ)
	$(CXX) $(USER_CXXFLAGS) -c "$<" -o "$@"

$(OBJ)/Profile.o: Daemon/Profile.cpp Daemon/Profile.h | $(OBJ)
	$(CXX) $(USER_CXXFLAGS) -c "$<" -o "$@"

$(OBJ)/TemperatureReader.o: Daemon/TemperatureReader.cpp Daemon/TemperatureReader.h | $(OBJ)
	$(CXX) $(USER_CXXFLAGS) -c "$<" -o "$@"

$(OBJ)/DaemonMain.o: Daemon/main.cpp Daemon/DriverClient.h Daemon/Energy.h Daemon/Policy.h Daemon/Profile.h Daemon/SMCReader.h Daemon/TemperatureReader.h | $(OBJ)
	$(CXX) $(USER_CXXFLAGS) -c "$<" -o "$@"

$(RELEASE)/turbomacd: $(OBJ)/DaemonMain.o $(OBJ)/DriverClient.o $(OBJ)/Policy.o $(OBJ)/Profile.o $(OBJ)/TemperatureReader.o $(OBJ)/SMCReader.o | $(RELEASE)
	$(CXX) -arch $(ARCH) -isysroot "$(SDK)" -mmacosx-version-min=$(MIN_VERSION) $^ -framework IOKit -framework CoreFoundation -o "$@"

$(RELEASE)/turbomacctl: CLI/main.cpp | $(RELEASE)
	$(CXX) $(USER_CXXFLAGS) "$<" -o "$@"

$(RELEASE)/turbomac-avx2-load: Calibration/avx2_load.cpp | $(RELEASE)
	$(CXX) $(USER_CXXFLAGS) -mavx2 -mfma "$<" -o "$@"

sign: build
	codesign --force --sign - --timestamp=none "$(RELEASE)/turbomacd"
	codesign --force --sign - --timestamp=none "$(RELEASE)/turbomacctl"
	codesign --force --sign - --timestamp=none "$(RELEASE)/turbomac-avx2-load"
	codesign --force --sign - --timestamp=none "$(KEXT)"

package: sign
	rm -rf "$(PACKAGE)"
	mkdir -p "$(PACKAGE)/Library/Extensions" "$(PACKAGE)/Library/LaunchDaemons" "$(PACKAGE)/usr/local/bin" "$(PACKAGE)/usr/local/libexec"
	ditto "$(KEXT)" "$(PACKAGE)/Library/Extensions/TurboMac.kext"
	install -m 0644 Deploy/com.parham.turbomacd.plist "$(PACKAGE)/Library/LaunchDaemons/com.parham.turbomacd.plist"
	install -m 0755 "$(RELEASE)/turbomacctl" "$(PACKAGE)/usr/local/bin/turbomacctl"
	install -m 0755 "$(RELEASE)/turbomacd" "$(PACKAGE)/usr/local/libexec/turbomacd"
	install -m 0755 "$(RELEASE)/turbomac-avx2-load" "$(PACKAGE)/usr/local/libexec/turbomac-avx2-load"

verify: package
	plutil -lint "$(KEXT)/Contents/Info.plist" Deploy/com.parham.turbomacd.plist
	codesign --verify --strict --verbose=4 "$(KEXT)"
	codesign --verify --strict --verbose=4 "$(RELEASE)/turbomacd" "$(RELEASE)/turbomacctl" "$(RELEASE)/turbomac-avx2-load"
	file "$(KEXT_MACOS)/TurboMac" "$(RELEASE)/turbomacd" "$(RELEASE)/turbomacctl" "$(RELEASE)/turbomac-avx2-load" | grep -c 'x86_64' | grep -q '^4$$'
	@if [[ "$$(uname -m)" == "x86_64" ]]; then kmutil print-diagnostics -a x86_64 -z -p "$(KEXT)"; else echo "kmutil loadability diagnostics deferred to the x86_64 target host"; fi

test: $(BUILD)/tests/test_rapl $(BUILD)/tests/test_policy $(BUILD)/tests/test_temperature $(BUILD)/tests/replay_observer
	"$(BUILD)/tests/test_rapl"
	"$(BUILD)/tests/test_policy"
	"$(BUILD)/tests/test_temperature"
	python3 Tests/test_driver_contract.py

$(BUILD)/tests:
	mkdir -p "$@"

$(BUILD)/tests/test_rapl: Tests/test_rapl.cpp Daemon/Energy.h Shared/DriverValidation.h Shared/RAPLCodec.h | $(BUILD)/tests
	$(CXX) -isysroot "$(SDK)" -std=c++17 -O2 -Wall -Wextra -Werror -IShared -IDaemon "$<" -o "$@"

$(BUILD)/tests/test_policy: Tests/test_policy.cpp Daemon/Policy.cpp Daemon/Policy.h | $(BUILD)/tests
	$(CXX) -isysroot "$(SDK)" -std=c++17 -O2 -Wall -Wextra -Werror -IShared -IDaemon Tests/test_policy.cpp Daemon/Policy.cpp -o "$@"

$(BUILD)/tests/test_temperature: Tests/test_temperature.cpp Daemon/TemperatureReader.cpp Daemon/TemperatureReader.h | $(BUILD)/tests
	$(CXX) -isysroot "$(SDK)" -std=c++17 -O2 -Wall -Wextra -Werror -IDaemon Tests/test_temperature.cpp Daemon/TemperatureReader.cpp -o "$@"

$(BUILD)/tests/replay_observer: Tests/replay_observer.cpp Daemon/Policy.cpp Daemon/Policy.h | $(BUILD)/tests
	$(CXX) -isysroot "$(SDK)" -std=c++17 -O2 -Wall -Wextra -Werror -IShared -IDaemon Tests/replay_observer.cpp Daemon/Policy.cpp -o "$@"

replay: $(BUILD)/tests/replay_observer
	@test -n "$(FILES)" || (echo 'usage: make replay FILES="samples-*.jsonl"' >&2; exit 2)
	"$(BUILD)/tests/replay_observer" $(FILES)

clean:
	rm -rf "$(BUILD)"
