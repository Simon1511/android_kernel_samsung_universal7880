
#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-

make rise-a5y17lte_defconfig
make -j64

