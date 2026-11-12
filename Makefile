# HUSH — top-level build.
#   make            PSP EBOOT.PBP (build/psp/EBOOT.PBP)
#   make host       host audio renderer + test suite
#   make test       run the host test suite (quick)
#   make test-full  include the 30-minute audio stability test
#   make autotest   build the PPSSPP validation EBOOT (build/autotest/EBOOT.PBP)
#   make release    full release package (see tools/release.sh)
#   make clean

PSPDEV ?= /usr/local/pspdev
export PSPDEV
export PATH := $(PATH):$(PSPDEV)/bin

.PHONY: all psp host test test-full autotest autotest-campaign autotest-sweep release clean assets

all: psp

psp:
	@$(MAKE) --no-print-directory -f tools/psp.mk BUILD=build/psp

autotest:
	@$(MAKE) --no-print-directory -f tools/psp.mk BUILD=build/autotest EXTRA_DEFS=-DHUSH_AUTOTEST

autotest-campaign:
	@$(MAKE) --no-print-directory -f tools/psp.mk BUILD=build/autotest-campaign EXTRA_DEFS=-DHUSH_AUTOTEST=2

autotest-sweep:
	@$(MAKE) --no-print-directory -f tools/psp.mk BUILD=build/autotest-sweep EXTRA_DEFS=-DHUSH_AUTOTEST=3

host:
	@$(MAKE) --no-print-directory -f tools/host.mk build/host/hush-render build/host/hush-tests

test: host
	./build/host/hush-tests --quick

test-full: host
	./build/host/hush-tests

assets:
	python3 tools/asset-generation/gen_assets.py

release: psp autotest host
	sh tools/release.sh

clean:
	rm -rf build
