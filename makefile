# ----------------------------
# Makefile del CE C Toolchain
# ----------------------------

NAME = SNESCE
ICON =
DESCRIPTION = "Emulador SNES para TI-84 Plus CE"
COMPRESSED = YES
ARCHIVED = YES

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
