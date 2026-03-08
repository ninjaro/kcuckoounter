SHELL := /usr/bin/env bash
.SHELLFLAGS := -Eeuo pipefail -c

SCRIPT_DIR := $(CURDIR)/scripts
CLI := $(SCRIPT_DIR)/cli.sh

.PHONY: help \
	build build-kde build-nonkde build-android build-all \
	test test-kde test-nonkde test-all \
	run run-kde run-nonkde run-android-emulator run-android-device \
	run-mem run-mem-kde run-mem-nonkde \
	leaks leaks-kde leaks-nonkde \
	deps deps-desktop deps-android deps-all \
	format format-check naming-check check \
	android-env android-deps-emulator android-deps-device android-deps-build \
	android-build android-run-emulator android-run-device

help:
	$(CLI) help

build: build-all

build-kde:
	$(CLI) build kde

build-nonkde:
	$(CLI) build nonkde

build-android:
	$(CLI) build android

build-all:
	$(CLI) build all

test: test-all

test-kde:
	$(CLI) test kde

test-nonkde:
	$(CLI) test nonkde

test-all:
	$(CLI) test all

run: run-kde

run-kde:
	$(CLI) run kde

run-nonkde:
	$(CLI) run nonkde

run-android-emulator:
	$(CLI) run android-emulator

run-android-device:
	$(CLI) run android-device

run-mem: run-mem-kde

run-mem-kde:
	$(CLI) run-mem kde

run-mem-nonkde:
	$(CLI) run-mem nonkde

leaks: leaks-nonkde

leaks-kde:
	$(CLI) leaks kde

leaks-nonkde:
	$(CLI) leaks nonkde

deps: deps-desktop

deps-desktop:
	$(CLI) deps desktop

deps-android:
	$(CLI) deps android

deps-all:
	$(CLI) deps all

format:
	$(CLI) format

format-check:
	$(CLI) format-check

naming-check:
	$(CLI) naming-check changed

android-env:
	$(CLI) android env

android-deps-emulator:
	$(CLI) android deps emulator

android-deps-device:
	$(CLI) android deps device

android-deps-build:
	$(CLI) android deps build

android-build:
	$(CLI) android build

android-run-emulator:
	$(CLI) android run-emulator

android-run-device:
	$(CLI) android run-device

check:
	$(CLI) check
