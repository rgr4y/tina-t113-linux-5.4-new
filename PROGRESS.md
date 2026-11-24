## 2026-11-24 — build host prep on chonk

- Outcome: tree cloned to `chonk:~/workspace/waveshare-mk10/kernel`. `.config` = running device config (`/proc/config.gz`, 5.4.61 #334), passed through `olddefconfig`. Copy of the raw config: `../running-5.4.61-334.config`. Env: `source ../env.sh`.
- Toolchain: Linaro GCC 11.3.1 arm-linux-gnueabihf, borrowed from `~/workspace/luckfox-lume/out/toolchain/`. Vendor used OpenWrt/Linaro GCC 6.4. releases.linaro.org no longer serves the 7.5 tarballs.
- Why not macOS: 21 paths collide on case-insensitive FS (`xt_DSCP.h`/`xt_dscp.h` etc.) and show as phantom modifications.
- Config delta vs running kernel:
  - No source in this tree, but bound on device: `FB_TFT_GC9107`, `FB_TFT_GC9307` (fb0-fb11), `RTL8710BU` (USB wifi), `CRYPTO_DEV_ATSHA204A` (i2c driver `atsha204a`). Also missing: `FB_TFT_NV3007` (was =n).
  - Dropped `GCC_PLUGINS`, `GCC_PLUGIN_ARM_SSP_PER_TASK`, `STACKPROTECTOR_PER_TASK`: `scripts/gcc-plugin.sh` probe fails (host g++ 14 vs GCC 11 plugin headers, `static_assert` error).
- Verification: `make -j32 zImage` succeeded.
- Follow-up: source the vendor drivers above (GPL request, or port fbtft gc9107/gc9307 and an out-of-tree rtl8710bu). Board DTS is not in this tree (Tina keeps it under `device/config/chips/t113/...`); the vendor may also have patched existing files, which a config diff cannot show.

## 2026-11-24 — MK10 device tree captured

- Outcome: device trees from 10.0.1.48 saved and decompiled into `docs/mk10-dts/`.
  - `boot_package-kernel.dtb/.dts`: board DTB (`model = "MK10"`) as stored in the boot_package (toc1) at sector 32800 of mmcblk0, byte offset 18120704, 52165 bytes. This is the one to build from.
  - `boot_package-uboot.dtb/.dts`: the DTB U-Boot uses for itself, from the same package.
  - `live-sysfs.dtb/.dts`: `/sys/firmware/fdt` after U-Boot fixups. Differs from the boot_package DTB only in bootargs, `/memreserve/`, and `dram_para[NN]` props (needs `dtc -f`, the `[` in names is invalid).
- The boot image (mmcblk0p4) is Android format with no DTB inside. Allwinner loads the DTB from boot_package.
- Panels: `compatible = "syk,gc9107"` / `"syk,gc9307"` on spi0/spi1 with `dc-gpios`, 75 MHz, 60 fps, buswidth 8.
- Raw dumps kept outside the repo in `~/workspace/waveshare-mk10/dts/`: `boot-mmcblk0p4.img` (vendor zImage + ramdisk), `mmcblk0-head-41464s.bin` (everything before p1: boot0, boot_package).
- Decompiled DTS has raw phandles. It compiles standalone, it is not yet a proper `sun8iw20p1.dtsi` include.

## 2026-11-24 — add-on modules for the vendor kernel

- Outcome: `mk10/build-modules.sh` builds 35 modules for the running vendor kernel (`5.4.61 SMP preempt mod_unload ARMv7 p2v8`) into `~/workspace/waveshare-mk10/out/mk10-modules-5.4.61.tgz` (`lib/modules/5.4.61/extra/`). Inputs: `mk10/running-5.4.61-334.config` + `mk10/modules.fragment`.
  - USB gadget: `usb_f_ncm`, `usb_f_ecm`, `usb_f_ecm_subset` (+ legacy `g_ncm`/`g_ether`). Built via `USB_G_NCM=m`/`USB_ETH=m` because `USB_CONFIGFS_NCM/ECM` are bools that would force `=y`. RNDIS is already builtin.
  - USB host ethernet: `usbnet cdc_ether cdc_ncm cdc_subset rndis_host r8152 asix ax88179_178a` (+ `net1080 zaurus` pulled in by cdc_subset).
  - Tailscale: `tun`, `iptable_nat iptable_raw xt_nat xt_MASQUERADE xt_addrtype`, `ip6_tables ip6table_{filter,mangle,raw,nat} nf_reject_ipv6`, `nf_log_*`. Policy routing (`IP_MULTIPLE_TABLES`, `IPV6_MULTIPLE_TABLES`) is already builtin.
  - `nf_nat_masquerade.ko` (out-of-tree, `oot/nf_nat_masquerade`): vendor `nf_nat=y` was built without `NF_NAT_MASQUERADE`, so `xt_MASQUERADE` needs the helpers as a separate module.
  - zram: `zsmalloc`, `zram`. Block device only — vendor kernel has `CONFIG_SWAP=n` (core mm, not modular), so no zram swap without a new kernel.
  - Realtek wifi: `8821cu`, `88x2bu` (morrownr copies in `oot/`). The script widens their `MODULE_IMPORT_NS(VFS_internal...)` guard from >=5.10 to >=5.4; this vendor 5.4 enforces symbol namespaces.
- Root causes hit:
  - vermagic `5.4.61+`: setlocalversion appends `+` for a non-tag tree. Script touches `.scmversion`.
  - `GCC_PLUGINS` probe failed with GCC 11 headers (fixed in previous commit). Without per-task SSP, modules reference `__stack_chk_guard`, which the vendor kernel lacks.
- Guard: the script fails if the merged config changes any builtin `=y` vs the running config (allow-list: bools that only affect module code).
- Verification on 10.0.1.48 (loaded from /tmp, then all unloaded, nothing installed): 27 modules insmod clean, no unknown symbols, taint only `O`. `ip tuntap` creates tun; zram0 sized 32M (lzo-rle); MASQUERADE rule added/removed in v4 and v6 nat; `ncm`/`ecm` function dirs created in configfs gadget g1 and removed. Static check: every undefined symbol in every module resolves to our vmlinux exports, and all function symbols exist in device `/proc/kallsyms` (data symbols not visible: `KALLSYMS_ALL=n`).
- Not done: modules not installed into the device rootfs; no NCM link tested against a host; Realtek dongles not plugged in.
