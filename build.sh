#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# Use the checkout at ./edk2 by default; it may be a directory or a symlink.
edk2_dir=${EDK2_DIR:-"$script_dir/edk2"}
architecture=${1:-ALL}
build_target=${2:-RELEASE}

if [[ "$architecture" == ALL ]]; then
  "$0" X64 RELEASE
  "$0" AARCH64 RELEASE
  exit 0
fi

if [[ ! -d "$edk2_dir" || ! -f "$edk2_dir/edksetup.sh" ]]; then
  printf 'EDK2_DIR must point to an EDK II checkout directory (or symlink); by default this is %s.\n' "$script_dir/edk2" >&2
  exit 2
fi

case "$architecture:$build_target" in
  X64:DEBUG|X64:RELEASE)
    toolchain=GCC
    output_architecture=x64
    ;;
  AARCH64:DEBUG|AARCH64:RELEASE)
    toolchain=GCCNOLTO
    output_architecture=aarch64
    ;;
  *)
    printf 'Usage: %s [X64|AARCH64] [DEBUG|RELEASE] | ALL\n' "$0" >&2
    exit 2
    ;;
esac

if [[ "$architecture" == AARCH64 ]]; then
  export GCCNOLTO_AARCH64_PREFIX="${GCCNOLTO_AARCH64_PREFIX:-aarch64-linux-gnu-}"
fi

make -C "$edk2_dir/BaseTools"
unset WORKSPACE EDK_TOOLS_PATH CONF_PATH PACKAGES_PATH
# edksetup.sh establishes the EDK II build environment in this shell.
cd "$edk2_dir"
set --
set +u
source "$edk2_dir/edksetup.sh"
set -u
export PACKAGES_PATH="$script_dir"

build -p BlkDumpPkg/BlkDumpPkg.dsc \
  -a "$architecture" \
  -t "$toolchain" \
  -b "$build_target"

output="$edk2_dir/Build/BlkDumpPkg/${build_target}_${toolchain}/${architecture}/BlkDump/BlkDump/OUTPUT/BlkDump.efi"
if [[ "$build_target" == DEBUG ]]; then
  artifact="$script_dir/Artifacts/blkdump-${output_architecture,,}-debug.efi"
else
  artifact="$script_dir/Artifacts/blkdump-${output_architecture,,}.efi"
fi
mkdir -p "$script_dir/Artifacts"
cp "$output" "$artifact"
printf 'Wrote %s\n' "$artifact"