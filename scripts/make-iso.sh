#!/usr/bin/env bash
# Source-only delivery: this script has not been executed during generation.
# Prepare a hybrid BIOS/x86_64 UEFI ISO using locally supplied Limine v8.7.0.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
cd -- "$project_dir"

fail() {
    printf 'make-iso: %s\n' "$*" >&2
    exit 1
}

[[ -f Makefile && -f kernel/arch/x86_64/linker.ld && -f limine.conf && -f LICENSE ]] \
    || fail 'Project root markers are missing.'
[[ ! -L build ]] || fail 'Refusing a build directory symlink.'
mkdir -p -- build
[[ $(CDPATH= cd -- build && pwd -P) == "$project_dir/build" ]] \
    || fail 'Build path is outside the project.'
build_links=$(find build -type l -print -quit) || fail 'Cannot inspect the build directory.'
[[ -z "$build_links" ]] || fail 'Refusing symlinks inside the build directory.'

build_dir="$project_dir/build"
stage="$build_dir/iso_root"
kernel="$build_dir/utamo-kernel.elf"
iso="$build_dir/utamo-os-0.0.1.iso"
iso_tmp="$build_dir/utamo-os-0.0.1.iso.tmp"
vendor="$project_dir/third_party/limine/vendor"
expected_limine_commit=aad3edd370955449717a334f0289dee10e2c5f01

[[ -f "$kernel" ]] || fail 'Kernel ELF missing. Run make kernel first.'
command -v xorriso >/dev/null 2>&1 || fail 'xorriso is required.'
[[ -x "$vendor/limine" ]] \
    || fail 'Build the Limine host helper in third_party/limine/vendor; see docs/development-environment.md.'
for asset in limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin BOOTX64.EFI LICENSE; do
    [[ -f "$vendor/$asset" ]] || fail "Missing Limine asset: $asset"
done

# Require the reviewed upstream release; no network request or implicit download.
command -v git >/dev/null 2>&1 || fail 'git is required to verify the local Limine release.'
[[ -d "$vendor/.git" ]] || fail 'Expected a local Limine v8.7.0-binary Git checkout.'
vendor_head=$(git -C "$vendor" rev-parse HEAD)
vendor_release=$(git -C "$vendor" rev-parse 'v8.7.0-binary^{commit}') \
    || fail 'Pinned Limine tag v8.7.0-binary is unavailable.'
[[ "$vendor_head" == "$vendor_release" ]] || fail 'Limine checkout must be exactly v8.7.0-binary.'
[[ "$vendor_head" == "$expected_limine_commit" ]] || fail 'Limine release commit does not match the reviewed upstream revision.'
git -C "$vendor" diff --no-ext-diff --quiet HEAD -- \
    || fail 'Tracked Limine checkout files were modified.'

# All destructive/output paths below are fixed children of the verified build.
[[ "$stage" == "$project_dir/build/iso_root" ]] || fail 'Invalid staging path.'
rm -rf -- "$stage"
mkdir -p -- "$stage/boot/limine" "$stage/EFI/BOOT"
cp -- "$kernel" "$stage/boot/utamo-kernel.elf"
cp -- limine.conf "$stage/boot/limine/limine.conf"
cp -- "$vendor/limine-bios.sys" "$stage/boot/limine/"
cp -- "$vendor/limine-bios-cd.bin" "$stage/boot/limine/"
cp -- "$vendor/limine-uefi-cd.bin" "$stage/boot/limine/"
cp -- "$vendor/BOOTX64.EFI" "$stage/EFI/BOOT/"
cp -- "$vendor/LICENSE" "$stage/boot/limine/LICENSE"
cp -- LICENSE "$stage/UTAMO-LICENSE"

trap 'rm -f -- "$iso_tmp"' EXIT
xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$stage" -o "$iso_tmp"

# Operates on this ISO file only. Never pass a disk/device to this command.
"$vendor/limine" bios-install "$iso_tmp"
mv -f -- "$iso_tmp" "$iso"
printf 'ISO criada: %s\nValidacao de boot ainda depende de execucao no ambiente pessoal.\n' "$iso"
