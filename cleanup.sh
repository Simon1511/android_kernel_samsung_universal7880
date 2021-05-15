
#!/bin/bash

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
