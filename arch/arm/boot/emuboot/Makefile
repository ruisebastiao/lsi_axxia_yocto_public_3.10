# Build an image for emulation.

AS		= $(CROSS_COMPILE)gcc -c
LD		= $(CROSS_COMPILE)ld
OBJCOPY         = $(CROSS_COMPILE)objcopy

DTC = ../../../../scripts/dtc/dtc
DTS = ../dts/axm55xx.dts
ZIMAGE = ../zImage

all: clean linux.img

clean:
	rm -f linux.img emuboot.o zImage.emu axm55xx.dtb

linux.img: emuboot.o emuboot.lds zImage.emu
	$(LD) -o linux.tmp --script=emuboot.lds
	$(OBJCOPY) -O binary -R .note -R .comment -S linux.tmp $@
	rm -f linux.tmp

zImage.emu: $(ZIMAGE) axm55xx.dtb
	python pack.py $(ZIMAGE) $< > $@

axm55xx.dtb: $(DTS)
	$(DTC) -O dtb -o $@ $<

emuboot.o: emuboot.S
	$(AS) -o $@ $<
