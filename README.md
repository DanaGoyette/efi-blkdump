# efi-blkdump

`efi-blkdump` is a UEFI application for inspecting block devices and exporting their contents. It lists `EFI_BLOCK_IO_PROTOCOL` handles and can save a selected LBA range or the entire device to a file. It can also export the child devices of a selected handle as separate files on an EFI-writable filesystem. Source devices are only read, never modified.

## Motivation

My Surface Pro 11 has an EFI block device with 91 partitions that's not
exposed once Windows or Linux boots, and I was curious what was on it.
I wrote this tool so I could create images of the device and its partitions,
and then use standard Linux filesystem utilities to explore the image.

## Project layout

File                | Usage
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
   Alternately, set `EDK2_DIR` to another checkout directory or symlink; when unset, the build wrapper assumes `edk2/` beside `build.sh`.
2. Use the build wrapper:

```sh
./build.sh X64 RELEASE
./build.sh AARCH64 RELEASE
./build.sh ALL
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

The VS Code tasks copy the binaries to `Artifacts/` with architecture and build mode in their names:

```text
BlkDump-x64-debug.efi
BlkDump-x64-release.efi
BlkDump-aarch64-debug.efi
BlkDump-aarch64-release.efi
```

## Run from the UEFI shell

1. Copy the resulting `BlkDump.efi` to a FAT filesystem.
2. Boot to the UEFI shell.
3. List available block devices:

```text
fs0:\> BlkDump.efi -l
  0: whole disk, media id 0x726F6E73, block size 4096, io align 8, last LBA 0x3FFF, 67108864 bytes
    path: VenHw(7CCE9C94-983F-4D0A-8143-B6C05545B223)
  1: logical partition, media id 0x726F6E73, block size 4096, io align 0, last LBA 0x83, 540672 bytes
    path: VenHw(7CCE9C94-983F-4D0A-8143-B6C05545B223)/HD(1,GPT,B7A12F2D-9578-6B04-3CBD-04A0A49BE489)
...
 91: logical partition, media id 0x726F6E73, block size 4096, io align 0, last LBA 0x7F, 524288 bytes
    path: VenHw(7CCE9C94-983F-4D0A-8143-B6C05545B223)/HD(91,GPT,F2B35C04-A3C3-4059-998D-B22B98ECA3AE)
```

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
device paths extend the selected `blk0` path. The parent handle itself and
unrelated block devices are skipped. The output directory must already exist.

### Options

Parameter         |Short| Usage
------------------|-----|-------------------------------------
`--version`       |`-v` | print the embedded build timestamp and exit.
`--list`          |`-l` | enumerate block devices and exit.
`--break`         |`-b` | print the list output one screen at a time; press `q` to stop.
`--device <index>`|`-d` | select a device by the right-aligned number shown by `-l`.
`--start <lba>`   |`-s` | starting LBA; decimal or `0x` hexadecimal is accepted.
`--count <blocks>`|`-n` | number of blocks to read; defaults to 128.
`--output <path>` |`-o` | filesystem path for the output file. Regular file only.
`--all-devices <directory>`|| with `-d`, dump each child handle as `blkN.bin`.
`--overwrite`     |     | replace existing output files without prompting.
`--no-overwrite`  |     | skip existing output files without reading their source handles.

## Safety notes

- The utility deliberately excludes any ability to write directly to block devices.
- The chosen output path must be a regular file, not a directory or a block device.
- Reads are currently limited 1 block at a time, to work around a device's read size limits issues

## Development note

The implementation was largely generated by GitHub Copilot in Visual Studio Code under my direction. 
I defined the goals, guided the design and implementation, 
reviewed the code, and tested the resulting application.
