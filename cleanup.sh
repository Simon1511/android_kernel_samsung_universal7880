
#!/bin/bash

full=$(grep "full" rise/build.info | sed 's/full=//g')

export ARCH=arm64
export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-

make clean
make mrproper

for i in `find rise/AIK/ -name "boot.img-zImage"`; do
    rm $i
done

for i in `find rise/AIK/ -name "boot.img-dt"`; do
    rm $i
done

if [[ `which git` == *"git"* ]]; then
    git checkout -- rise/AIK/*
    git checkout -- arch/arm64/configs/rise-*
fi

# Delete some other files
if [[ ! "$full" == "y" ]]; then
    rm rise/build.log
    rm rise/build.info
    rm arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
    rm arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
    rm arch/arm64/configs/tmp_defconfig
fi
