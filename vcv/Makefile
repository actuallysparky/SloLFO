RACK_DIR ?= ../_local/vcv-sdk/Rack-SDK

SOURCES += src/plugin.cpp src/SlowLFO.cpp
DISTRIBUTABLES += res

res/SlowLFO.svg: ../hardware/Slow-LFO-8HP-Faceplate_8x8.kicad_pcb generate_panel.py
	python3 generate_panel.py

all: res/SlowLFO.svg
dist: res/SlowLFO.svg

include $(RACK_DIR)/plugin.mk
