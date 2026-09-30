
@echo ***
@echo Device 0 end-condition tests
@echo ***

@echo ===
@echo Known-good reads
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x0    -n 1  -o lba0.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x1    -n 1  -o lba1.bin
BlkDump-aarch64.efi --overwrite -d 0 -s 0x1000 -n 16 -o middle16.bin

@echo ===
@echo Last valid block
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFF -n 1  -o last1.bin

@echo ===
@echo Range ending exactly on the last valid block
@echo 0x3FF0 .. 0x3FFF
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF0 -n 16 -o last16.bin

@echo ===
@echo Starts at the last valid block, extends past end
@echo 0x3FFF .. 0x400E
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FFF -n 16 -o overrun16.bin

@echo ===
@echo First invalid block
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x4000 -n 1  -o pastend1.bin

@echo ===
@echo Larger invalid range
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x4000 -n 16 -o pastend16.bin

@echo ===
@echo Ends 16 blocks beyond advertised end
@echo ===

BlkDump-aarch64.efi --overwrite -d 0 -s 0x3FF0 -n 32 -o overrun32.bin

@echo ===
@echo Done.
