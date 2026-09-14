CWD := $(shell pwd)
BAUDRATE := 115200

export BOARD := esp32_devkitc/esp32/procpu
export DTC_OVERLAY_FILE := $(CWD)/app/boards/esp32_devkitc.overlay
export SECRETS_FILES := $(CWD)/app/secrets.conf \
						$(CWD)/app/smtp.secrets.conf

target:
	west build -b $(BOARD) -s app -p auto -- -DEXTRA_CONF_FILE="$(foreach secconf,$(SECRETS_FILES),;$(secconf))"

doctor:
	west build -b $(BOARD) -s app -p auto -- -DEXTRA_CONF_FILE="$(foreach secconf,$(SECRETS_FILES),;$(secconf))" -DZEPHYR_SCA_VARIANT=dtdoctor

flash:
	west flash --esp-device /dev/ttyUSB0

mon:
	minicom -D /dev/ttyUSB0 -b $(BAUDRATE)

menuconfig:
	west build -t menuconfig

clean:
	rm -rf build/

show:
	@echo "Current Directory: $(CWD)"
	@echo "Board: $(BOARD)"
	@echo "Baudrate: $(BAUDRATE)"
	@echo "DTC Overlay File: $(DTC_OVERLAY_FILE)"
	@echo "secrets files: $(foreach secconf,$(SECRETS_FILES),;$(secconf))"
	@echo "-----------------------------------"
