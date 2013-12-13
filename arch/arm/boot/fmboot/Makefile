# Build an image for Fast Models

AS		= $(CROSS_COMPILE)gcc -c
LD		= $(CROSS_COMPILE)ld

DTC = $(obj)/../../../../scripts/dtc/dtc
DTS = $(obj)/../dts/axm55xxsim.dts
ZIMAGE = $(obj)/../zImage

clean:
	rm -f $(obj)/../linux.img $(obj)/fmboot.o $(obj)/zImage.fm $(obj)/axm55xxsim.dtb

arch/arm/boot/linux.img: $(obj)/fmboot.o $(srctree)/$(obj)/fmboot.lds $(obj)/zImage.fm
	cd $(obj) && $(LD) -o ../linux.img --script=$(srctree)/$(obj)/fmboot.lds
	tar jcf $(obj)/../linux.img.tar.bz2 $(obj)/../linux.img
	rm -rf $(obj)/../linux.img

$(obj)/zImage.fm: $(ZIMAGE) $(obj)/axm55xxsim.dtb
	python $(srctree)/$(obj)/pack.py $(ZIMAGE) $(obj)/axm55xxsim.dtb > $@

$(obj)/axm55xxsim.dtb: $(DTS)
	$(DTC) -O dtb -o $@ $<

$(obj)/fmboot.o: $(obj)/fmboot.S
	$(AS) -o $@ $<
