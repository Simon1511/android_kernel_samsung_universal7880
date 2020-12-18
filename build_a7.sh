
#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-
export ANDROID_MAJOR_VERSION=p

make rise-a7y17lte_defconfig
make -j64

