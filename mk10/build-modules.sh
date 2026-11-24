#!/bin/bash
# Build add-on modules for the vendor MK10 kernel (5.4.61 #334, vermagic "5.4.61 SMP preempt mod_unload ARMv7 p2v8").
# Output: ../out/mk10-modules-5.4.61.tgz  (lib/modules/5.4.61/extra/*.ko, untar at / then depmod -a)
set -euo pipefail
cd "$(dirname "$0")/.."
K=$PWD; W=$(cd .. && pwd); OUT=$W/out; KVER=5.4.61
source "$W/env.sh"
log() { echo "### $*"; }

log "config: running vendor config + mk10/modules.fragment"
touch .scmversion   # stop setlocalversion appending "+" to the vermagic
cp mk10/running-5.4.61-334.config .config
scripts/kconfig/merge_config.sh -m .config mk10/modules.fragment >/dev/null
make olddefconfig >/dev/null

log "guard: no builtin (=y) drift vs running kernel"
drift=$(scripts/diffconfig mk10/running-5.4.61-334.config .config \
  | grep -E ' y$|-> y' \
  | grep -vE '^-(CRYPTO_DEV_ATSHA204A|FB_TFT_GC9107|FB_TFT_GC9307|RTL8710BU) |CC_HAS_ASM_INLINE|NF_NAT_MASQUERADE|USB_ETH_RNDIS|USB_ARMLINUX|USB_BELKIN' || true)
[ -z "$drift" ] || { echo "builtin drift, refusing:"; echo "$drift"; exit 1; }

log "kernel modules (zImage too, modpost needs vmlinux)"
make -j"$(nproc)" zImage modules >"$W/build-modules.log" 2>&1 || { tail -20 "$W/build-modules.log"; exit 1; }
[ "$(cat include/config/kernel.release)" = "$KVER" ] || { echo "kernel.release != $KVER"; exit 1; }

log "out-of-tree: nf_nat_masquerade (vendor nf_nat lacks it), 8821cu, 88x2bu"
M=$W/oot/nf_nat_masquerade; mkdir -p "$M"
cp net/netfilter/nf_nat_masquerade.c "$M/"
grep -q MODULE_LICENSE "$M/nf_nat_masquerade.c" || echo 'MODULE_LICENSE("GPL");' >> "$M/nf_nat_masquerade.c"
echo "obj-m := nf_nat_masquerade.o" > "$M/Kbuild"
make -C "$K" M="$M" modules >/dev/null 2>&1
for d in 8821cu-20210916 88x2bu-20210702; do
  [ -d "$W/oot/$d" ] || rsync -a --exclude "*.ko" --exclude "*.o" "$HOME/workspace/$d" "$W/oot/"
done
for d in 8821cu-20210916 88x2bu-20210702; do
  ( cd "$W/oot/$d"
    # driver imports the VFS namespace only for >=5.10; this vendor 5.4 enforces it too
    sed -i 's/>= KERNEL_VERSION(5, 10, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)/>= KERNEL_VERSION(5, 4, 0) \&\& LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)/' os_dep/linux/os_intfs.c
    grep -q "KERNEL_VERSION(5, 4, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)" os_dep/linux/os_intfs.c || { echo "$d: import guard patch failed"; exit 1; }
    make KSRC="$K" KVER=$KVER -j"$(nproc)" modules >build.log 2>&1 || { tail -20 build.log; exit 1; }
    ! grep -q "does not import" build.log || { echo "$d: namespace import missing"; exit 1; } )
done

log "collect (skip modules the vendor already ships)"
VENDOR="fuse ipt_REJECT iptable_filter xt_LOG xt_TCPMSS xt_comment xt_limit xt_mac xt_mark xt_multiport xt_time"
WANT="tun zsmalloc zram usb_f_ncm usb_f_ecm usb_f_ecm_subset g_ncm g_ether usbnet cdc_ether cdc_ncm cdc_subset rndis_host r8152 asix ax88179_178a net1080 zaurus
      iptable_nat iptable_raw xt_nat xt_MASQUERADE xt_addrtype ip6_tables ip6table_filter ip6table_mangle ip6table_raw ip6table_nat nf_reject_ipv6 nf_log_common nf_log_ipv4 nf_log_ipv6"
rm -rf "$OUT/stage"; X=$OUT/stage/lib/modules/$KVER/extra; mkdir -p "$X"
for m in $WANT; do f=$(find . -name "$m.ko" | head -1); [ -n "$f" ] || { echo "missing $m.ko"; exit 1; }; cp "$f" "$X/"; done
cp "$W/oot/nf_nat_masquerade/nf_nat_masquerade.ko" "$W"/oot/8821cu-20210916/8821cu.ko "$W"/oot/88x2bu-20210702/88x2bu.ko "$X/"
${CROSS_COMPILE}strip --strip-debug "$X"/*.ko
for f in "$X"/*.ko; do
  vm=vermagic=$(/sbin/modinfo -F vermagic "$f")
  [ "$vm" = "vermagic=$KVER SMP preempt mod_unload ARMv7 p2v8 " ] || { echo "bad $vm in $f"; exit 1; }
done
tar -C "$OUT/stage" -czf "$OUT/mk10-modules-$KVER.tgz" lib
log "done: $OUT/mk10-modules-$KVER.tgz ($(ls "$X" | wc -l) modules, vendor-shipped skipped: $VENDOR)"
