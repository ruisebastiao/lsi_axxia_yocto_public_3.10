#
# Makefile for RapidIO interconnect services
#
#obj-y += rio.o rio-access.o rio-driver.o rio-scan.o rio-net.o rio-locks.o rio-sysfs.o rio-quirks.o rio-dio.o rio-destid.o rio-domain.o rio-hotplug.o rio-multicast.o rio-job.o

obj-y += rio.o rio-access.o rio-driver.o rio-net2.o rio-locks.o rio-sysfs.o rio-quirks.o rio-dio.o rio-destid.o rio-hotplug.o rio-route.o

obj-$(CONFIG_RAPIDIO)		+= switches/
obj-$(CONFIG_RAPIDIO)		+= devices/

subdir-ccflags-$(CONFIG_RAPIDIO_DEBUG) := -DDEBUG
