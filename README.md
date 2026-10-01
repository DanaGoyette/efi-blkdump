# efi-blkdump

`efi-blkdump` is a UEFI application for inspecting block devices and exporting their contents. It lists `EFI_BLOCK_IO_PROTOCOL` handles and can save a selected LBA range or the entire device to a file. It can also export the child devices of a selected handle as separate files on an EFI-writable filesystem. Source devices are only read, never modified.

## Motivation

My Surface Pro 11 has an EFI block device with 91 partitions that's not
exposed once Windows or Linux boots, and I was curious what was on it.
I wrote this tool so I could create images of the device and its partitions,
and then use standard Linux filesystem utilities to explore the image.

## Project layout

File                        | Usage
----------------------------|------------------
`BlkDump/BlkDump.c`         | application logic
`BlkDump/BlkDump.inf`       | EDK2 module metadata
`BlkDumpPkg/BlkDumpPkg.dec` | package metadata
`BlkDumpPkg/BlkDumpPkg.dsc` | platform description for AARCH64 and X64

## Build

This project is intended for an EDK II environment with the standard `build` tool available.

1. Check out or install an EDK II tree. Put it in this repository as
   `edk2/`, either as a directory or as a symlink to the checkout.
   This path is ignored by Git.
   Alternately, set `EDK2_DIR` to another checkout directory or symlink;
   when unset, the build wrapper assumes `edk2/` beside `build.sh`.
2. Use the build wrapper:

```sh
# Build both release binaries (also the default with no arguments)
./build.sh ALL

# Build a single architecture/configuration explicitly
./build.sh X64 DEBUG
./build.sh AARCH64 RELEASE
```

To use a checkout elsewhere, set `EDK2_DIR` to its directory or symlink before
running the wrapper, for example `export EDK2_DIR=/path/to/edk2`.

The wrapper sets `PACKAGES_PATH` to this repository, which is already the package root because it contains `BlkDumpPkg/`. There is no need to move the source into another subdirectory. To run the commands manually, source the EDK II environment and set `PACKAGES_PATH` to this repository:

```sh
export EDK2_DIR=/path/to/edk2
cd "$EDK2_DIR"
make -C BaseTools
unset WORKSPACE EDK_TOOLS_PATH CONF_PATH PACKAGES_PATH
source edksetup.sh
export PACKAGES_PATH=/path/to/efi-blkdump
build -p BlkDumpPkg/BlkDumpPkg.dsc -a X64 -t GCC -b RELEASE
GCCNOLTO_AARCH64_PREFIX=aarch64-linux-gnu- build -p BlkDumpPkg/BlkDumpPkg.dsc -a AARCH64 -t GCCNOLTO -b RELEASE
```

The output EFI binary will be created under a `Build/BlkDumpPkg/.../` directory. On an x86-64 host, the AARCH64 build requires an `aarch64-linux-gnu-gcc` cross compiler and its binutils in `PATH`. Set `GCCNOLTO_AARCH64_PREFIX` so EDK II selects that compiler; the host CPU does not need to match the target architecture. The EDK II target name is `AARCH64`, while the copied artifacts use `aarch64`.

The builds are copied to `Artifacts/` with short architecture names. Explicit debug builds keep a `-debug` suffix:

```text
BlkDump-x64.efi
BlkDump-aarch64.efi
BlkDump-x64-debug.efi
BlkDump-aarch64-debug.efi
```

## Run from the UEFI shell

1. Copy the resulting `BlkDump.efi` to a FAT filesystem.
2. Boot to the UEFI shell.
3. List available block devices:

Use `--list` to show a listing grouped by parent device, with human-readable sizes.

```text
fs0:\> BlkDump-x64.efi -l
Devices:
   0: whole disk, RW, 100 GiB, block size 512 B
      path: PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)/NVMe(0x1,...)
         2: partition, RW, 1 GiB
            path: HD(1,GPT,...)
         3: partition, RW, 2 GiB
            path: HD(2,GPT,...)
         4: partition, RW, 8 GiB
            path: HD(3,GPT,...)
         5: partition, RW, 88.9 GiB
            path: HD(4,GPT,...)
   1: whole disk, RW, 794.3 GiB, block size 512 B
      path: PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)/NVMe(0x2,...)
         6: partition, RW, 794.3 GiB
            path: HD(1,GPT,...)
   7: whole disk, RW, 3.5 TiB, block size 512 B
      path: PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/NVMe(0x1,...)
         8: partition, RW, 127 MiB
            path: HD(1,GPT,...)
         ...
```

Use `--list --verbose` to show full device paths and raw media details,
including media ID, block size, I/O alignment, last LBA, and exact byte count.

```text
fs0:\> BlkDump-x64.efi -l -v
Devices:
   0: whole disk, RW, media id 0x0, block size 512
      io align 4, last LBA 0xC7FFFFF, 107374182400 bytes
      path: PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)/NVMe(0x1,...)
   1: whole disk, RW, media id 0x1900000000, block size 512
      io align 4, last LBA 0x63481AAF, 852822941696 bytes
      path: PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)/NVMe(0x2,...)
   2: partition, RW, media id 0xC600000000, block size 512
      io align 0, last LBA 0x2197FF, 1127219200 bytes
      path: PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)/NVMe(0x1,...)/HD(1,GPT,...)
   ...
   6: partition, RW, media id 0x1600000000, block size 512
      io align 0, last LBA 0x634817DD, 852822572032 bytes
      path: PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)/NVMe(0x2,...)/HD(1,GPT,...)
   7: whole disk, RW, media id 0xC600000000, block size 512
      io align 4, last LBA 0x1BF1F72AF, 3840755982336 bytes
      path: PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/NVMe(0x1,...)
   8: partition, RW, media id 0x37E00000000, block size 512
      io align 0, last LBA 0x3F7FF, 133169152 bytes
      path: PciRoot(0x0)/Pci(0x2,0x1)/Pci(0x0,0x0)/Pci(0x0,0x0)/Pci(0x0,0x0)/NVMe(0x1,...)/HD(1,GPT,...)
```

In either listing mode, if the firmware marks a device as removable,
it will be noted on the end of the line that shows the device index and type.

Note: the numbers shown by `--list` are the application’s own selectors for `EFI_BLOCK_IO_PROTOCOL` handles. They are independent from the UEFI shell’s `blkN` numbering, and the two numbering schemes may not match.

4. Select a device and run a dump.

To dump a range:

```text
fs0:\> BlkDump.efi -d 0 -s 0 -n 128 -o fs0:\dump.bin
```

To dump the whole device or partition:
```text
fs0:\> BlkDump.efi -d 0 -a -o fs0:\device.bin
fs0:\> BlkDump.efi -d 1 -a -o fs0:\partition1.bin
```

To dump every child block handle of a selected parent into separate files:

```text
fs0:\> BlkDump.efi -d 0 --all-devices fs0:\dumps
```

This creates files such as `blk3.bin` and `blk4.bin` for handles whose UEFI
device paths extend the selected device index. The parent handle itself and
unrelated block devices are skipped. The output directory must already exist.

### Options

Parameter             |Short| Usage
----------------------|-----|-------------------------------------
`--version`           |     | print the embedded build timestamp and exit.
`--list`              |`-l` | enumerate block devices and exit.
`--verbose`           |`-v` | with `--list`, show raw media details and full device paths.
`--break`             |`-b` | print the list output one page at a time; press q to stop.
`--device <index>`    |`-d` | select a device by the right-aligned number shown by `-l`.
`--start <lba>`       |`-s` | starting LBA; decimal or `0x` hexadecimal is accepted.
`--blocks <blocks>`   |`-n` | number of blocks to read; defaults to 128.
`--chunk <blocks>`    |     | maximum number of blocks to read per ReadBlocks() call; defaults to 1.
`--output <path>`     |`-o` | filesystem path for the output file. Regular file only.
`--all-devices <dir>` |     | with `-d`, dump each child handle as `blkN.bin`.
`--overwrite`         |     | replace existing output files without prompting.
`--no-overwrite`      |     | skip existing output files without reading their source handles.

## Safety notes

- The utility deliberately excludes any ability to write directly to block devices.
- The chosen output path must be a regular file, not a directory or a block device.
- The default read chunk size is 1 block for maximum compatibility with firmware
  that rejects otherwise-valid multi-block reads.

## Development note

The project was initially developed with substantial assistance from
GitHub Copilot in Visual Studio Code under my direction. I defined the
requirements, guided the design and implementation, reviewed generated
code, performed manual refactoring and maintenance, and tested the
resulting application across multiple systems.
