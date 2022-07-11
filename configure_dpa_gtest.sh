#!/bin/sh

#BUILD_DIR=/hpc/mtl_scrap/users/alexm/DPA
#PKG_CONFIG_PATH=/usr/local/gtest/lib64/pkgconfig \
#meson setup \
#      --cross-file cross-gcc-riscv64.txt \
#      -Denable-gtest=true -Dbuild.pkg_config_path=/usr/local/gtest/lib64/pkgconfig \
#      -Dwith-flexio=/labhome/amikheev/workspace/NVME/apu/flexio-sdk-new/inst_feb2022 \
#      $BUILD_DIR/meson_build_cross

meson setup \
      --cross-file cross-gcc-riscv64.txt \
      -Denable-gtest=true \
      -Dwith-flexio=/labhome/amikheev/workspace/NVME/apu/flexio-sdk-new/inst_feb2022 \
      $*
