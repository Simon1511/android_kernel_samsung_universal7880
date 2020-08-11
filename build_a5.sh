
#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-
export ANDROID_MAJOR_VERSION=p

make exynos7880-a5y17lte_eur_defconfig
make -j64

