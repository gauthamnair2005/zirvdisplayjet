# ZirvDisplayJet — DisplayJet display driver for Zirvium kernel
#
# This driver is compiled as part of the Zirvium kernel build.
# The source files are included via the kernel's top-level Makefile.
#
# Targets:
#   make -C /path/to/zirvium  (the kernel build compiles this driver)
#
.PHONY: all clean

all:
	@echo "ZirvDisplayJet is compiled as part of the Zirvium kernel."
	@echo "Run 'make' from the kernel root directory."

clean:
	@echo "Clean is handled by the kernel's top-level Makefile."
