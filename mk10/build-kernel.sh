#!/bin/bash
# Build the MK10 kernel with the reconstructed drivers and zram builtin, plus the matching
# module set, and repack boot.img (p4). Unlike build-modules.sh this changes the kernel image,
# so it replaces that script's "no builtin drift" guard with an explicit allowlist.
# Output (../out): mk10-boot-p4.img (dd over mmcblk0p4), mk10-modules-5.4.61.tgz (untar into the
# rootfs, then depmod), zImage, config. Needs ../vendor-boot-p4.img (raw pull of the vendor p4).
set -euo pipefail
cd "$(dirname "$0")/.."
K=$PWD; W=$(cd .. && pwd); OUT=$W/out; KVER=5.4.61
source "$W/env.sh"
log() { echo "### $*"; }

log "config: running vendor config + modules.fragment (=m) + kernel-builtin.fragment (=y)"
touch .scmversion   # stop setlocalversion appending "+" to the vermagic
cp mk10/running-5.4.61-334.config .config
scripts/kconfig/merge_config.sh -m .config mk10/modules.fragment mk10/kernel-builtin.fragment >/dev/null 2>&1
make olddefconfig >/dev/null

log "guard: builtin drift vs the running kernel must be exactly the expected set"
# expected: the drivers swapped in, zram (+ swap, lz4, deps), masquerade now builtin so no
# out-of-tree copy, the cdc_subset sub-options, compiler symbols. Anything else stops the build.
ALLOW='^[-+ ]?(CRYPTO_DEV_ATSHA204A|FB_TFT_GC9107|FB_TFT_GC9307|RTL8710BU|CRYPTO_DEV_ATMEL_SHA204A|CRYPTO_DEV_ATMEL_I2C|CRYPTO_LZ4|LZ4_COMPRESS|LZ4_DECOMPRESS|HW_RANDOM|SWAP|ZSMALLOC|ZRAM|CC_HAS_ASM_INLINE|NF_NAT_MASQUERADE|USB_ETH_RNDIS|USB_ARMLINUX|USB_BELKIN) '
drift=$(scripts/diffconfig mk10/running-5.4.61-334.config .config | grep -E ' y$|-> y' | grep -vE "$ALLOW" || true)
[ -z "$drift" ] || { echo "unexpected builtin drift, refusing:"; echo "$drift"; exit 1; }
scripts/diffconfig mk10/running-5.4.61-334.config .config | grep -E 'y$|-> y' | sed 's/^/  drift: /'

log "kernel: zImage + modules"
make -j"$(nproc)" zImage modules >"$W/build-kernel.log" 2>&1 || { tail -30 "$W/build-kernel.log"; exit 1; }
[ "$(cat include/config/kernel.release)" = "$KVER" ] || { echo "kernel.release != $KVER"; exit 1; }
Z=arch/arm/boot/zImage; ls -l "$Z"

log "out-of-tree: 8821cu, 88x2bu (nf_nat_masquerade is builtin now)"
for d in 8821cu-20210916 88x2bu-20210702; do
  [ -d "$W/oot/$d" ] || rsync -a --exclude "*.ko" --exclude "*.o" "$HOME/workspace/$d" "$W/oot/"
done
for d in 8821cu-20210916 88x2bu-20210702; do
  ( cd "$W/oot/$d"
    make clean >/dev/null 2>&1 || true
    # driver imports the VFS namespace only for >=5.10; this vendor 5.4 enforces it too
    sed -i 's/>= KERNEL_VERSION(5, 10, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)/>= KERNEL_VERSION(5, 4, 0) \&\& LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)/' os_dep/linux/os_intfs.c
    grep -q "KERNEL_VERSION(5, 4, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)" os_dep/linux/os_intfs.c || { echo "$d: import guard patch failed"; exit 1; }
    make KSRC="$K" KVER=$KVER -j"$(nproc)" modules >build.log 2>&1 || { tail -20 build.log; exit 1; }
    ! grep -q "does not import" build.log || { echo "$d: namespace import missing"; exit 1; } )
done

log "collect: every in-tree =m module (they were built against the builtin config above), plus the two oot"
rm -rf "$OUT/stage"; X=$OUT/stage/lib/modules/$KVER/extra; mkdir -p "$X"
while read -r m; do m="${m%.ko}"; m="${m%.o}"; cp "$m.ko" "$X/"; done < modules.order
cp "$W"/oot/8821cu-20210916/8821cu.ko "$W"/oot/88x2bu-20210702/88x2bu.ko "$X/"
${CROSS_COMPILE}strip --strip-debug "$X"/*.ko
for f in "$X"/*.ko; do
  vm=vermagic=$(/sbin/modinfo -F vermagic "$f")
  [ "$vm" = "vermagic=$KVER SMP preempt mod_unload ARMv7 p2v8 " ] || { echo "bad $vm in $f"; exit 1; }
done
for f in "$X"/*.ko; do case $(basename "$f") in fb_gc9107*|zram*|zsmalloc*|atmel*) echo "module that must be builtin: $f"; exit 1;; esac; done
cp modules.builtin modules.builtin.modinfo "$OUT/stage/lib/modules/$KVER/"
tar -C "$OUT/stage" -czf "$OUT/mk10-modules-$KVER.tgz" lib

log "boot image: vendor p4 with the kernel swapped"
[ -f "$W/vendor-boot-p4.img" ] || { echo "missing $W/vendor-boot-p4.img"; exit 1; }
python3 mk10/mkbootimg.py "$W/vendor-boot-p4.img" "$Z" "$OUT/mk10-boot-p4.img" --max-bytes 6451200
cp "$Z" "$OUT/zImage"; cp .config "$OUT/config-$KVER"
( cd "$OUT" && sha256sum mk10-boot-p4.img mk10-modules-$KVER.tgz zImage )
log "done: $OUT/mk10-boot-p4.img, $OUT/mk10-modules-$KVER.tgz ($(ls "$X" | wc -l) modules)"
