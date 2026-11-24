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
