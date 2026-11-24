## 2026-11-24 — build host prep on chonk

- Outcome: tree cloned to `chonk:~/workspace/waveshare-mk10/kernel`. `.config` = running device config (`/proc/config.gz`, 5.4.61 #334), passed through `olddefconfig`. Copy of the raw config: `../running-5.4.61-334.config`. Env: `source ../env.sh`.
- Toolchain: Linaro GCC 11.3.1 arm-linux-gnueabihf, borrowed from `~/workspace/luckfox-lume/out/toolchain/`. Vendor used OpenWrt/Linaro GCC 6.4. releases.linaro.org no longer serves the 7.5 tarballs.
- Why not macOS: 21 paths collide on case-insensitive FS (`xt_DSCP.h`/`xt_dscp.h` etc.) and show as phantom modifications.
- Config delta vs running kernel:
  - No source in this tree, but bound on device: `FB_TFT_GC9107`, `FB_TFT_GC9307` (fb0-fb11), `RTL8710BU` (USB wifi), `CRYPTO_DEV_ATSHA204A` (i2c driver `atsha204a`). Also missing: `FB_TFT_NV3007` (was =n).
  - Dropped `GCC_PLUGINS`, `GCC_PLUGIN_ARM_SSP_PER_TASK`, `STACKPROTECTOR_PER_TASK`: `scripts/gcc-plugin.sh` probe fails (host g++ 14 vs GCC 11 plugin headers, `static_assert` error).
- Verification: `make -j32 zImage` succeeded.
- Follow-up: source the vendor drivers above (GPL request, or port fbtft gc9107/gc9307 and an out-of-tree rtl8710bu). Board DTS is not in this tree (Tina keeps it under `device/config/chips/t113/...`); the vendor may also have patched existing files, which a config diff cannot show.
