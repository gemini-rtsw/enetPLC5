TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#=============================

# PLC5 Support thusfar only works on 32-bit linux
EPICS_HOST_ARCH=linux-x86

# Install deviceSupport.dbd into <top>/dbd
DBD += plc5Support.dbd

# Build the IOC support library
LIBRARY_IOC += plc5Support
# Compile and add the code to the support library
plc5Support_SRCS += iocCommands.c
plc5Support_SRCS += devManager.c
plc5Support_SRCS += pls.c
plc5Support_SRCS += snap_db.c
plc5Support_SRCS += enetPlc5Pkg-linux.c
plc5Support_SRCS += drvOrnlPLC5-linux.c
plc5Support_SRCS += devOrnlPLC5-linux.c

# ornlSupport (just the Linux stuff)
plc5Support_SRCS += sys_pkg.c
plc5Support_SRCS += thread_pkg.c
plc5Support_SRCS += avl.c

# Link locally-provided code into the support library,
# rather than directly into the IOC application.
plc5Support_LIBS += $(EPICS_BASE_IOC_LIBS)


#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

