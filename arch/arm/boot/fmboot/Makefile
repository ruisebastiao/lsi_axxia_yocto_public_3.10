# Build an image for Fast Models

AS		= $(CROSS_COMPILE)gcc -c
LD		= $(CROSS_COMPILE)ld

DTC = ../../../../scripts/dtc/dtc
DTS = ../dts/axm-sim.dts
ZIMAGE = ../zImage

all: clean linux.fm

clean:
	rm -f linux.fm fmboot.o zImage.fm axm-sim.dtb

linux.fm: fmboot.o fmboot.lds zImage.fm
	$(LD) -o $@ --script=fmboot.lds

zImage.fm: $(ZIMAGE) axm-sim.dtb
	python pack.py $(ZIMAGE) axm-sim.dtb > $@

axm-sim.dtb: $(DTS)
	$(DTC) -O dtb -o $@ $<

fmboot.o: fmboot.S
	$(AS) -o $@ $<
