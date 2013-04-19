# Build an image for Fast Models

AS		= $(CROSS_COMPILE)gcc -c
LD		= $(CROSS_COMPILE)ld

DTC = ../../../../scripts/dtc/dtc
DTS = ../dts/axm55xxsim.dts
ZIMAGE = ../zImage

all: clean linux.img

clean:
	rm -f linux.img fmboot.o zImage.fm axm55xxsim.dtb

linux.img: fmboot.o fmboot.lds zImage.fm
	$(LD) -o $@ --script=fmboot.lds

zImage.fm: $(ZIMAGE) axm55xxsim.dtb
	python pack.py $(ZIMAGE) axm55xxsim.dtb > $@

axm55xxsim.dtb: $(DTS)
	$(DTC) -O dtb -o $@ $<

fmboot.o: fmboot.S
	$(AS) -o $@ $<
