#
# Makefile for the lsi_acp_i2c driver.
#

MODULE := lsi_acp_i2c

obj-$(CONFIG_I2C_AXXIA)		+= $(MODULE).o

$(MODULE)-objs			:= ai2c_mod.o \
				   ai2c_bus_axm5500.o

ccflags-$(CONFIG_I2C_AXXIA) := -DAI2C_MOD_NAME=\"ai2c\" \
				-DAI2C_MSG_TRACE_LEVEL=ai2c_trace_level \
				-DLINUX -DAI2C_CHIP_VER=9
