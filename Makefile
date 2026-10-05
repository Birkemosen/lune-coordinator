# -----------------------------------------------------------------------------
# Lune Touch / Mini coordinator workspace
# -----------------------------------------------------------------------------
CONFIG ?= configurations/lune-touch-7.yaml
BUILD_NAME ?= lune-touch
# Standalone checkout (this repo) keeps configs at ./configurations.
# Monorepo layout uses ROOT_DIR=../.. and DEVICE_DIR=devices/lune-touch.
ifeq ($(wildcard $(CURDIR)/configurations/lune-touch-7.yaml),)
ROOT_DIR ?= ../..
DEVICE_DIR ?= devices/lune-touch
SECRETS_TARGET ?= ../../../secrets.yaml
else
ROOT_DIR ?= .
DEVICE_DIR ?= .
SECRETS_TARGET ?= ../../lune/secrets.yaml
endif
ROOT_ABS := $(abspath $(ROOT_DIR))
CONFIG_PATH ?= $(ROOT_ABS)/$(DEVICE_DIR)/$(CONFIG)
# Prefer the venv's Python modules over generated console launchers. Those
# launchers embed an absolute interpreter path and can become stale after the
# workspace is moved or cloned elsewhere. Also accept a sibling lune/.venv313
# when this repo is checked out next to Birkemosen/lune.
LUNE_SIBLING := $(abspath $(CURDIR)/../lune)
PYTHON ?= $(firstword $(wildcard \
	$(ROOT_ABS)/.venv313/bin/python \
	$(ROOT_ABS)/.venv/bin/python \
	$(CURDIR)/.venv313/bin/python \
	$(CURDIR)/.venv/bin/python \
	$(LUNE_SIBLING)/.venv313/bin/python \
	$(LUNE_SIBLING)/.venv/bin/python \
) python3)
ESPHOME ?= $(PYTHON) -m esphome
PIO ?= $(PYTHON) -m platformio
ESPTOOL ?= $(PYTHON) -m esptool
PLATFORMIO_CORE_DIR ?= $(ROOT_ABS)/.cache/platformio
CXX ?= clang++
BUILD_ROOT ?= $(ROOT_ABS)/$(DEVICE_DIR)/configurations/.esphome/build/$(BUILD_NAME)
FIRMWARE_BIN ?= $(BUILD_ROOT)/.pio/build/$(BUILD_NAME)/firmware.bin
PARTITION_BIN ?= $(BUILD_ROOT)/.pio/build/$(BUILD_NAME)/partitions.bin
OTA_SLOT_BYTES ?= 6553600
OTA_WARN_PERCENT ?= 85
HOST ?=
PORT ?=
AUTO_PORT := $(strip $(shell ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* 2>/dev/null | head -n 1))
SERIAL_PORT := $(if $(PORT),$(PORT),$(AUTO_PORT))
SECRETS_LINK ?= configurations/secrets.yaml
DESIGN_DIR ?= $(firstword $(wildcard \
	$(abspath $(CURDIR)/../lune-design-system) \
	$(abspath $(ROOT_ABS)/../lune-design-system) \
))
LDS_DIR ?= $(DESIGN_DIR)
LDS_CONSUMER ?= $(CURDIR)

FORECAST_COMPONENT_DIR := components/forecast
FORECAST_TEST_DIR := tests/forecast
FORECAST_SRCS := $(FORECAST_COMPONENT_DIR)/forecast_model.cpp \
                 $(FORECAST_TEST_DIR)/test_forecast_model.cpp
FORECAST_OUT ?= /tmp/test_lune_touch_forecast_model
FORECAST_TIMELINE_OUT ?= /tmp/test_lune_touch_forecast_timeline

COORDINATOR_COMPONENT_DIR := components/lune_touch_coordinator
COORDINATOR_TEST_DIR := tests/coordinator
COORDINATOR_SRCS := $(COORDINATOR_COMPONENT_DIR)/coordinator_model.cpp \
                    $(COORDINATOR_TEST_DIR)/test_coordinator_model.cpp
COORDINATOR_OUT ?= /tmp/test_lune_touch_coordinator_model
ASGARD_URL_OUT ?= /tmp/test_lune_touch_asgard_url
ASGARD_ADAPTER_OUT ?= /tmp/test_lune_touch_asgard_adapter
ODIN_PLAN_OUT ?= /tmp/test_lune_touch_odin_plan
PLAN_SOURCE_OUT ?= /tmp/test_lune_touch_plan_source
CIRCULATION_PUMP_OUT ?= /tmp/test_lune_touch_circulation_pump
SIMULATION_OUT ?= /tmp/test_lune_touch_end_to_end_simulation
V6_ZONES_OUT ?= /tmp/test_lune_touch_v6_zones_parse
FLOW_TRIM_OUT ?= /tmp/test_lune_touch_flow_trim

.PHONY: help check ensure-secrets-link config compile build build-verify bump-build prepare-release release release-deploy upload ota-size-check ota deploy install-registry-partition monitor \
        config-mini build-mini deploy-mini ota-mini release-mini release-deploy-mini dashboard touch-ui dashboard-build \
        design-tokens design-verify \
        test test-forecast test-coordinator test-asgard-url test-asgard-adapter test-odin-plan test-circulation-pump test-simulation test-dashboard test-display test-v6-zones test-flow-trim clean

help:
	@echo "Lune Touch coordinator tasks"
	@echo "  make config           Validate Lune Touch ESPHome YAML"
	@echo "  make build            Bump build number and compile (v0.1.0-N)"
	@echo "  make build-verify     Compile without touching version.yaml"
	@echo "  make release          Compile a release binary without build suffix (VERSION=v1.2.3 optional)"
	@echo "  make release-deploy   Release compile + upload"
	@echo "  make config-mini      Validate Lune Mini ESPHome YAML"
	@echo "  make build-mini       Bump build number and compile Lune Mini"
	@echo "  make deploy-mini HOST=192.168.x.x"
	@echo "  make ota-mini HOST=192.168.x.x"
	@echo "  make release-mini     Compile a Lune Mini release without build suffix"
	@echo "  make ota-size-check   Verify firmware fits the OTA slot"
	@echo "  make ota HOST=192.168.x.x"
	@echo "  make deploy           Bump build + compile + upload Lune Touch (USB or HOST OTA)"
	@echo "  make deploy PORT=/dev/cu.usbmodemXXXX"
	@echo "  make deploy HOST=192.168.x.x"
	@echo "  make install-registry-partition PORT=/dev/cu.usbmodemXXXX  One-time 64 KiB Touch registry partition install"
	@echo "  make monitor          Open serial monitor on PORT"
	@echo "  make monitor PORT=/dev/cu.usbmodemXXXX"
	@echo "  make touch-ui         Build Lune Touch web UI (Design System 2)"
	@echo "  make test             Run host tests"
	@echo "  make design-tokens    Install LVGL theme, C++ tokens and brand marks from ../lune-design-system"
	@echo "  make design-verify    Check committed LDS artifacts (LVGL)"
	@echo "  make test-forecast    Run forecast preload-model tests"
	@echo "  make test-coordinator Run coordinator model tests"
	@echo ""
	@echo "Examples:"
	@echo "  make build"
	@echo "  make release"
	@echo "  make release VERSION=v1.0.0"
	@echo "  make release-deploy HOST=192.168.x.x"

ensure-secrets-link:
	@if [ -e "$(ROOT_ABS)/secrets.yaml" ]; then \
		if [ ! -e "$(SECRETS_LINK)" ] || [ -L "$(SECRETS_LINK)" ]; then \
			ln -sfn "$(SECRETS_TARGET)" "$(SECRETS_LINK)"; \
		fi; \
	elif [ -e "$(LUNE_SIBLING)/secrets.yaml" ]; then \
		if [ ! -e "$(SECRETS_LINK)" ] || [ -L "$(SECRETS_LINK)" ]; then \
			ln -sfn "$(SECRETS_TARGET)" "$(SECRETS_LINK)"; \
		fi; \
	fi

check: ensure-secrets-link
	@$(ESPHOME) version >/dev/null 2>&1 || { \
		echo "ESPHome CLI not found (python: $(PYTHON))."; \
		echo "Use sibling lune/.venv313, or: python3 -m venv .venv313 && ./.venv313/bin/pip install 'esphome>=2026.8.0' platformio"; \
		exit 1; \
	}
	@$(PIO) --version >/dev/null 2>&1 || { \
		echo "PlatformIO CLI not found (python: $(PYTHON))."; \
		echo "Install in venv: $(PYTHON) -m pip install platformio"; \
		exit 1; \
	}

config: check touch-ui
	cd $(ROOT_ABS) && $(ESPHOME) config $(CONFIG_PATH)

VERSION_YAML := $(CURDIR)/version.yaml
STAMP_VERSION := $(PYTHON) $(CURDIR)/stamp_version.py $(VERSION_YAML)

compile: check design-verify touch-ui
	cd $(ROOT_ABS) && $(ESPHOME) compile --only-generate $(CONFIG_PATH)
	perl -0pi -e 's/" ".join\(cmd\)/" ".join(map(str, cmd))/g' $(BUILD_ROOT)/post_build.py
	PLATFORMIO_CORE_DIR=$(PLATFORMIO_CORE_DIR) $(PIO) run -d $(BUILD_ROOT)
	$(MAKE) ota-size-check

build: check
	$(MAKE) bump-build
	$(MAKE) compile

build-verify: check
	$(MAKE) compile

bump-build:
	@$(STAMP_VERSION) bump

prepare-release:
	@$(STAMP_VERSION) release $(VERSION)

release: check
	$(MAKE) prepare-release
	$(MAKE) compile

release-deploy: check
	$(MAKE) prepare-release
	$(MAKE) compile
	$(MAKE) upload

config-mini:
	$(MAKE) CONFIG=configurations/lune-mini.yaml BUILD_NAME=lune-mini config

build-mini:
	$(MAKE) CONFIG=configurations/lune-mini.yaml BUILD_NAME=lune-mini build

deploy-mini:
	$(MAKE) CONFIG=configurations/lune-mini.yaml BUILD_NAME=lune-mini deploy

ota-mini:
	$(MAKE) CONFIG=configurations/lune-mini.yaml BUILD_NAME=lune-mini ota

release-mini:
	$(MAKE) CONFIG=configurations/lune-mini.yaml BUILD_NAME=lune-mini release

release-deploy-mini:
	$(MAKE) CONFIG=configurations/lune-mini.yaml BUILD_NAME=lune-mini release-deploy

ota-size-check:
	@if [ ! -f "$(FIRMWARE_BIN)" ]; then \
		echo "Firmware binary not found: $(FIRMWARE_BIN)"; \
		exit 1; \
	fi
	@size=$$(wc -c < "$(FIRMWARE_BIN)"); \
	warn=$$(( $(OTA_SLOT_BYTES) * $(OTA_WARN_PERCENT) / 100 )); \
	pct=$$(awk "BEGIN { printf \"%.1f\", ($$size / $(OTA_SLOT_BYTES)) * 100 }"); \
	echo "$(BUILD_NAME) OTA size: $$size / $(OTA_SLOT_BYTES) bytes ($$pct%)"; \
	if [ $$size -gt $(OTA_SLOT_BYTES) ]; then \
		echo "ERROR: firmware.bin exceeds Lune Touch OTA slot"; \
		exit 1; \
	fi; \
	if [ $$size -gt $$warn ]; then \
		echo "WARNING: firmware.bin exceeds $(OTA_WARN_PERCENT)% of the OTA slot"; \
	fi

ota: check
	$(MAKE) build
	@if [ -z "$(HOST)" ]; then \
		echo "No OTA host set. Re-run with HOST=192.168.x.x or HOST=lune-touch.local"; \
		exit 1; \
	fi
	$(ESPHOME) upload $(CONFIG_PATH) --file $(FIRMWARE_BIN) --device $(HOST)

deploy: check
	$(MAKE) build
	$(MAKE) upload

upload:
	@if [ -n "$(PORT)" ]; then \
		PLATFORMIO_CORE_DIR=$(PLATFORMIO_CORE_DIR) $(PIO) run -d $(BUILD_ROOT) -t upload --upload-port $(PORT); \
	elif [ -n "$(HOST)" ]; then \
		$(ESPHOME) upload $(CONFIG_PATH) --file $(FIRMWARE_BIN) --device $(HOST); \
	else \
		AUTO_PORT=$$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -n 1); \
		if [ -z "$$AUTO_PORT" ]; then \
			echo "No serial upload port found. Re-run with PORT=/dev/cu.usbmodemXXXX or HOST=192.168.x.x"; \
			exit 1; \
		fi; \
		echo "Uploading to $$AUTO_PORT"; \
		PLATFORMIO_CORE_DIR=$(PLATFORMIO_CORE_DIR) $(PIO) run -d $(BUILD_ROOT) -t upload --upload-port $$AUTO_PORT; \
	fi

# Writes only the partition table at 0x8000. It preserves the existing WiFi
# NVS and application slots; run it once over USB before flashing firmware
# that uses the dedicated touchreg partition.
install-registry-partition: compile
	@if [ -z "$(SERIAL_PORT)" ]; then \
		echo "No serial port detected. Use PORT=/dev/cu.usbmodemXXXX"; \
		exit 1; \
	fi
	$(ESPTOOL) --chip esp32s3 --port $(SERIAL_PORT) write-flash 0x8000 $(PARTITION_BIN)

monitor: check
	@if [ -z "$(SERIAL_PORT)" ]; then \
		echo "No serial port detected. Use PORT=/dev/cu.usbmodemXXXX"; \
		exit 1; \
	fi
	@echo "Using serial port: $(SERIAL_PORT)"
	PLATFORMIO_CORE_DIR=$(PLATFORMIO_CORE_DIR) $(PIO) device monitor --port $(SERIAL_PORT)

test-forecast:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I $(FORECAST_COMPONENT_DIR) $(FORECAST_SRCS) -o $(FORECAST_OUT) -lm
	$(FORECAST_OUT)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I $(COORDINATOR_COMPONENT_DIR) $(FORECAST_TEST_DIR)/test_forecast_timeline.cpp -o $(FORECAST_TIMELINE_OUT)
	$(FORECAST_TIMELINE_OUT)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I $(COORDINATOR_COMPONENT_DIR) $(FORECAST_TEST_DIR)/test_slab_charge.cpp -o $(FORECAST_TIMELINE_OUT)-charge
	$(FORECAST_TIMELINE_OUT)-charge
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I $(COORDINATOR_COMPONENT_DIR) $(FORECAST_TEST_DIR)/test_house_demand.cpp -o $(FORECAST_TIMELINE_OUT)-house
	$(FORECAST_TIMELINE_OUT)-house

test-coordinator:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I $(COORDINATOR_COMPONENT_DIR) $(COORDINATOR_SRCS) -o $(COORDINATOR_OUT)
	$(COORDINATOR_OUT)

test-asgard-url:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/asgard_url/test_asgard_url.cpp -o $(ASGARD_URL_OUT)
	$(ASGARD_URL_OUT)

test-asgard-adapter:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/asgard_adapter/test_asgard_adapter.cpp -o $(ASGARD_ADAPTER_OUT)
	$(ASGARD_ADAPTER_OUT)

test-odin-plan:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/odin_plan/test_odin_plan.cpp -o $(ODIN_PLAN_OUT)
	$(ODIN_PLAN_OUT)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/odin_plan/test_plan_source.cpp -o $(PLAN_SOURCE_OUT)
	cd $(CURDIR) && $(PLAN_SOURCE_OUT)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/odin_plan/test_odin_comfort.cpp -o $(ODIN_PLAN_OUT)-comfort
	$(ODIN_PLAN_OUT)-comfort

test-v6-zones:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/v6_zones/test_v6_zones_parse.cpp -o $(V6_ZONES_OUT)
	$(V6_ZONES_OUT)

test-flow-trim:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/flow_trim/test_flow_trim.cpp -o $(FLOW_TRIM_OUT)
	$(FLOW_TRIM_OUT)

test-circulation-pump:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator tests/circulation_pump/test_circulation_pump.cpp -o $(CIRCULATION_PUMP_OUT)
	$(CIRCULATION_PUMP_OUT)

test-simulation:
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I components/lune_touch_coordinator -I components/forecast -I ../lune/lune-v6/components/lv6_dashboard components/lune_touch_coordinator/coordinator_model.cpp components/forecast/forecast_model.cpp tests/simulation/test_end_to_end_simulation.cpp -o $(SIMULATION_OUT) -lm
	$(SIMULATION_OUT)

test: design-verify test-forecast test-coordinator test-asgard-url test-asgard-adapter test-odin-plan test-v6-zones test-flow-trim test-circulation-pump test-simulation test-dashboard test-display

test-dashboard:
	sh tests/dashboard/test_phase8_overview.sh

test-display:
	sh tests/display/test_phase8_display.sh

dashboard: touch-ui

touch-ui:
	@if [ ! -f "$(ROOT_ABS)/web/design-system/tools/lds_build.py" ]; then \
	  echo "Design system missing at web/design-system"; \
	  exit 1; \
	fi
	cd $(ROOT_ABS) && $(PYTHON) web/design-system/tools/lds_build.py web/design-system/config/touch.json
	cd $(ROOT_ABS) && $(PYTHON) web/touch-ui/build_ui.py --preview
	@echo "touch-ui: $$(gzip -9 -c $(ROOT_ABS)/web/touch-ui/dist/lune-ui.css | wc -c | tr -d ' ') B css.gz + $$(gzip -9 -c $(ROOT_ABS)/web/touch-ui/dist/ui.js | wc -c | tr -d ' ') B js.gz + $$(wc -c < $(ROOT_ABS)/web/touch-ui/dist/en/index.html.gz | tr -d ' ') B en.gz + $$(wc -c < $(ROOT_ABS)/web/touch-ui/dist/da/index.html.gz | tr -d ' ') B da.gz"

# Legacy alias
dashboard-build: touch-ui

design-tokens:
	@if [ ! -f "$(LDS_DIR)/tools/lds_display.py" ]; then \
	  echo "lune-design-system checkout not found at $(LDS_DIR)"; \
	  exit 1; \
	fi
	$(PYTHON) $(LDS_DIR)/tools/lds_display.py --install $(LDS_CONSUMER)

design-verify:
	@if [ ! -f "$(LDS_DIR)/tools/lds_display.py" ]; then \
	  echo "WARNING: lune-design-system checkout not found at $(LDS_DIR), skipping"; \
	else \
	  $(PYTHON) $(LDS_DIR)/tools/lds_display.py --install $(LDS_CONSUMER) --check; \
	fi

clean:
	rm -f $(FORECAST_OUT) $(COORDINATOR_OUT)
	rm -rf configurations/.esphome/build
	rm -rf $(ROOT_ABS)/web/touch-ui/dist
