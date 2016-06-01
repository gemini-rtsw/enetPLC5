TOP = ..
include $(TOP)/configure/CONFIG
EPICS_HOST_ARCH=linux-x86

DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *src*))
include $(TOP)/configure/RULES_DIRS

