@echo ***
@echo Device 0 ReadBlocks tests
@echo ***

@echo ===
@echo Middle of device
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x1000 -n 16 -c 1 -o mid-c1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x1000 -n 16 -c 2 -o mid-c2.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x1000 -n 16 -c 4 -o mid-c4.bin

@echo ===
@echo Last 16 blocks
@echo 0x3FF0 .. 0x3FFF
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF0 -n 16 -c 1 -o end16-c1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF0 -n 16 -c 2 -o end16-c2.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF0 -n 16 -c 4 -o end16-c4.bin

@echo ===
@echo Last 8 blocks
@echo 0x3FF8 .. 0x3FFF
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF8 -n 8 -c 1 -o end8-c1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF8 -n 8 -c 2 -o end8-c2.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF8 -n 8 -c 4 -o end8-c4.bin

@echo ===
@echo Last 4 blocks
@echo 0x3FFC .. 0x3FFF
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFC -n 4 -c 1 -o end4-c1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFC -n 4 -c 2 -o end4-c2.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFC -n 4 -c 4 -o end4-c4.bin

@echo ===
@echo Last 2 blocks
@echo 0x3FFE .. 0x3FFF
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFE -n 2 -c 1 -o end2-c1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFE -n 2 -c 2 -o end2-c2.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFE -n 2 -c 4 -o end2-c4.bin

@echo ===
@echo Last block
@echo 0x3FFF
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFF -n 1 -c 1 -o end1-c1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFF -n 1 -c 2 -o end1-c2.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFF -n 1 -c 4 -o end1-c4.bin

@echo ===
@echo Done.