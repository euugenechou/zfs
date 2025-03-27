#!/usr/bin/env bash

set -euxo pipefail

PKGS=(
	alien
	autoconf
	automake
	build-essential
	debhelper-compat
	dh-autoreconf
	dh-dkms
	dh-python
	dkms
	fakeroot
	gawk
	git
	libaio-dev
	libattr1-dev
	libblkid-dev
	libcurl4-openssl-dev
	libelf-dev
	libffi-dev
	libpam0g-dev
	libssl-dev
	libtirpc-dev
	libtool
	libudev-dev
	libunwind-dev
	linux-headers-generic
	parallel
	po-debconf
	python3
	python3-all-dev
	python3-cffi
	python3-dev
	python3-packaging
	python3-setuptools
	python3-sphinx
	uuid-dev
	zlib1g-dev
)

sudo apt install "${PKGS[@]}"
