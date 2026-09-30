RACK_DIR ?= ../_local/vcv-sdk/Rack-SDK

SOURCES += src/plugin.cpp src/SlowLFO.cpp
DISTRIBUTABLES += res LICENSE

LICENSE: ../LICENSE
	cp $< $@

panel:
	python3 generate_panel.py

all: LICENSE
dist: LICENSE

.PHONY: panel

include $(RACK_DIR)/plugin.mk
