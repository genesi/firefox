#!/usr/bin/make -f

# These are used for cross-compiling and for saving the configure script
# from having to guess our platform (since we know it already)
DEB_HOST_GNU_TYPE	:= $(shell dpkg-architecture -qDEB_HOST_GNU_TYPE)
DEB_BUILD_GNU_TYPE	:= $(shell dpkg-architecture -qDEB_BUILD_GNU_TYPE)
DEB_HOST_ARCH		:= $(shell dpkg-architecture -qDEB_HOST_ARCH)
DEB_HOST_GNU_CPU	:= $(shell dpkg-architecture -qDEB_HOST_GNU_CPU)
DEB_HOST_GNU_SYSTEM	:= $(shell dpkg-architecture -qDEB_HOST_GNU_SYSTEM)

DISTRIB_VERSION_MAJOR 	:= $(shell lsb_release -s -r | cut -d '.' -f 1)
DISTRIB_VERSION_MINOR 	:= $(shell lsb_release -s -r | cut -d '.' -f 2)
DISTRIB_CODENAME	:= $(shell lsb_release -s -c)

include $(CURDIR)/debian/config/branch.mk

# Various build defaults
# 1 = Build crashreporter (if supported)
MOZ_ENABLE_BREAKPAD	?= 1
# 1 = Enable official build (required for enabling the crashreporter)
MOZ_BUILD_OFFICIAL	?= 0
# 1 = Build without jemalloc suitable for valgrind debugging
MOZ_VALGRIND		?= 0
# 1 = Profile guided build
MOZ_BUILD_PGO		?= 0
# 1 = Build and run the testsuite
MOZ_WANT_UNIT_TESTS	?= 0
# 1 = Turn on debugging bits and disable optimizations
MOZ_DEBUG		?= 0
# 1 = Disable optimizations
MOZ_NO_OPTIMIZE		?= 0

# The package name
MOZ_PKG_NAME		:= $(shell dpkg-parsechangelog | sed -n 's/^Source: *\(.*\)$$/\1/ p')
# The binary name to use (derived from the package name by default)
MOZ_APP_NAME		?= $(MOZ_PKG_NAME)

# Define other variables used throughout the build
MOZ_DEFAULT_APP_NAME	?= $(MOZ_PKG_BASENAME)
MOZ_APP_BASENAME	?= $(shell echo $(MOZ_APP_NAME) | sed -n 's/\-.\|\<./\U&/g p')
MOZ_DEFAULT_APP_BASENAME ?= $(shell echo $(MOZ_DEFAULT_APP_NAME) | sed -n 's/\-.\|\<./\U&/g p')

MOZ_FORCE_UNOFFICIAL_BRANDING = 0

ifeq (1,$(MOZ_VALGRIND))
MOZ_FORCE_UNOFFICIAL_BRANDING = 1
endif

ifneq (,$(findstring noopt,$(DEB_BUILD_OPTIONS)))
MOZ_BUILD_PGO = 0
MOZ_NO_OPTIMIZE	= 1
MOZ_FORCE_UNOFFICIAL_BRANDING = 1
endif

ifneq (,$(findstring debug,$(DEB_BUILD_OPTIONS)))
MOZ_NO_OPTIMIZE = 1
MOZ_DEBUG = 1
MOZ_FORCE_UNOFFICIAL_BRANDING = 1
endif

ifneq ($(MOZ_APP_NAME)$(MOZ_APP_BASENAME),$(MOZ_DEFAULT_APP_NAME)$(MOZ_DEFAULT_APP_BASENAME))
# If we change MOZ_APP_NAME or MOZ_APP_BASENAME, don't use official branding
MOZ_FORCE_UNOFFICIAL_BRANDING = 1
endif
