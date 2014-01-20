#
# Makefile for the linux kernel.
#
obj-$(CONFIG_AXXIA_RIO)                 += axxia-rio.o axxia-rio-hotplug.o axxia-rio-irq.o axxia-rio-sysfs.o axxia-rio-ds.o

ifeq ($(CONFIG_AXXIA_RIO_DEBUG),y)
CFLAGS_axxia-rio.o := -DDEBUG
endif
