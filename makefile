MCFLAGS += -target x86_64-pc-win32-coff

CFLAGS += -Wno-incompatible-pointer-types-discards-qualifiers -Wno-int-conversion
CFLAGS += -Wall -Wextra -O2 -fstack-usage
CFLAGS += -mno-stack-arg-probe -fno-stack-check -fno-stack-protector -fshort-wchar -mno-red-zone

INCS += -I ./gnu-efi/inc

LDFLAGS += -filealign:16 -subsystem:efi_application -nodefaultlib -dll -entry:efi_main

OVMF_FD ?= /usr/share/ovmf/OVMF.fd


all: efi image

efi:
	clang $(MCFLAGS) $(CFLAGS) $(INCS) -c forth.c -o forth.o
	llvm-size forth.o
	llvm-objdump -s -d forth.o > forth.dis
	lld-link $(LDFLAGS) forth.o -out:BOOTX64.EFI

image: efi
	mkdir -pv root
	mkdir -pv input/EFI/BOOT
	cp -rvf gnu-efi input/EFI/BOOT/
	cp -vf genimage.cfg input/EFI/BOOT/
	cp -vf forth.c input/EFI/BOOT/
	cp -vf makefile input/EFI/BOOT/
	cp -vf BOOTX64.EFI  input/EFI/BOOT/
	genimage


qemu: image
	qemu-system-x86_64 \
		-M q35 \
		-m 256 \
		-cpu qemu64 \
		-vga cirrus \
		-device virtio-rng-pci \
		-drive if=pflash,format=raw,unit=0,file=$(OVMF_FD),readonly=on \
		-hda images/disk.img



clean:
	rm -rfv *.su *.out *.exe *.efi *.EFI *.o *.lib *.map ./tmp/* ./input/* ./images/*
