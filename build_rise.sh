#!/bin/bash

# Set to y to see output of make in terminal
debug=n
dtbpath=arch/arm64/boot/dtb.img
imagepath=arch/arm64/boot/Image
aikpath=rise/AIK

buildDate=$(date '+%Y%m%d')

riseVer=v1.X

# Colors
RED='\033[0;31m'
NC='\033[0m'

BUILD_BOOT() {
    variant=$1
    dev=$2

    export ARCH=arm64
    export SUBARCH=arm64
    export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-
    export ANDROID_MAJOR_VERSION=p
    
    if [ -f rise/build.log ]; then
        rm rise/build.log
    fi
    
    if [ -f rise/build.info ]; then
        rm rise/build.info
    fi

    echo "full=n" >> rise/build.info
    echo "variant=$variant" > rise/build.info
    echo "device=$dev" >> rise/build.info

    if [[ "$variant" == "AOSP 10.0" ]]; then
        if [[ "$dev" == "a5" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a5y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
            cat arch/arm64/configs/rise-a5y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
        elif [[ "$dev" == "a7" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a7y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
            cat arch/arm64/configs/rise-a7y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
        fi
        cat arch/arm64/configs/lineage_defconfig >> arch/arm64/configs/tmp_defconfig
    elif [[ "$variant" == "OneUI 10.0" ]]; then
        if [[ "$dev" == "a5" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a5y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
            cat arch/arm64/configs/rise-a5y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
        elif [[ "$dev" == "a7" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a7y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
            cat arch/arm64/configs/rise-a7y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
        fi
        cat arch/arm64/configs/oneui_defconfig >> arch/arm64/configs/tmp_defconfig
    elif [[ "$variant" == "Treble 10.0" ]]; then
        if [[ "$dev" == "a5" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a5y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
            cat arch/arm64/configs/rise-a5y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
        elif [[ "$dev" == "a7" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a7y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
            cat arch/arm64/configs/rise-a7y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
        fi
        cat arch/arm64/configs/treble_defconfig >> arch/arm64/configs/tmp_defconfig
    fi

    make tmp_defconfig &> rise/build.log

    clear
    echo "Target: $variant for $dev"
    echo "Building..."

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi
    
    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    if [[ "$dev" == "a5" ]]; then
        rm arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
    elif [[ "$dev" == "a7" ]]; then
        rm arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
    fi
    
    rm arch/arm64/configs/tmp_defconfig

    echo "Packing boot.img..."

    if [[ "$variant" == "AOSP 10.0" ]]; then
        cp $imagepath $aikpath/Lineage/split_img/boot.img-zImage
        cp $dtbpath $aikpath/Lineage/split_img/boot.img-dt
        chmod +x $aikpath/Lineage/repackimg.sh
        $aikpath/Lineage/repackimg.sh
    elif [[ "$variant" == "OneUI 10.0" ]]; then
        cp $imagepath $aikpath/OneUI/split_img/boot.img-zImage
        cp $dtbpath $aikpath/OneUI/split_img/boot.img-dt
        chmod +x $aikpath/OneUI/repackimg.sh
        $aikpath/OneUI/repackimg.sh
    elif [[ "$variant" == "Treble 10.0" ]]; then
        cp $imagepath $aikpath/Treble/split_img/boot.img-zImage
        cp $dtbpath $aikpath/Treble/split_img/boot.img-dt
        chmod +x $aikpath/Treble/repackimg.sh
        $aikpath/Treble/repackimg.sh
    fi

    clear
    printf "\nOutput file is rise/boot_$dev.img\n\n"
}

BUILD_ALL() {
    export ARCH=arm64
    export SUBARCH=arm64
    export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-
    export ANDROID_MAJOR_VERSION=p
    
    if [ -f rise/build.log ]; then
        rm rise/build.log
    fi
    
    if [ -f rise/build.info ]; then
        rm rise/build.info
    fi

    echo "Building..."

    echo "device=a5" > rise/build.info
    echo "full=y" >> rise/build.info

    # A5 Lineage Q
    cat arch/arm64/boot/dts/exynos7880-a5y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
    cat arch/arm64/configs/rise-a5y17lte_defconfig >> arch/arm64/configs/tmp_defconfig
    cat arch/arm64/configs/lineage_defconfig >> arch/arm64/configs/tmp_defconfig
    make tmp_defconfig &> rise/build.log

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    cp $imagepath $aikpath/Lineage/split_img/boot.img-zImage
    cp $dtbpath $aikpath/Lineage/split_img/boot.img-dt
    chmod +x $aikpath/Lineage/repackimg.sh
    $aikpath/Lineage/repackimg.sh

    ./cleanup.sh > /dev/null 2>&1

    if [[ "$debug" == "n" ]]; then
        clear
    fi

    echo "Building..."

    # A5 OneUI Q
    cat arch/arm64/configs/rise-a5y17lte_defconfig > arch/arm64/configs/tmp_defconfig
    cat arch/arm64/configs/oneui_defconfig >> arch/arm64/configs/tmp_defconfig
    make tmp_defconfig &> rise/build.log

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    cp $imagepath $aikpath/OneUI/split_img/boot.img-zImage
    cp $dtbpath $aikpath/OneUI/split_img/boot.img-dt
    chmod +x $aikpath/OneUI/repackimg.sh
    $aikpath/OneUI/repackimg.sh

    ./cleanup.sh > /dev/null 2>&1

    if [[ "$debug" == "n" ]]; then
        clear
    fi

    echo "Building..."

    # A5 Treble Q
    cat arch/arm64/boot/dts/exynos7880-a5y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
    cat arch/arm64/configs/rise-a5y17lte_defconfig > arch/arm64/configs/tmp_defconfig
    cat arch/arm64/configs/treble_defconfig >> arch/arm64/configs/tmp_defconfig
    make tmp_defconfig &> rise/build.log

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    cp $imagepath $aikpath/Treble/split_img/boot.img-zImage
    cp $dtbpath $aikpath/Treble/split_img/boot.img-dt
    chmod +x $aikpath/Treble/repackimg.sh
    $aikpath/Treble/repackimg.sh

    ./cleanup.sh > /dev/null 2>&1

    if [[ "$debug" == "n" ]]; then
        clear
    fi

    echo "Building..."

    echo "device=a7" > rise/build.info
    echo "full=y" >> rise/build.info

    # A7 Lineage Q
    cat arch/arm64/boot/dts/exynos7880-a7y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
    cat arch/arm64/configs/rise-a7y17lte_defconfig > arch/arm64/configs/tmp_defconfig
    cat arch/arm64/configs/lineage_defconfig >> arch/arm64/configs/tmp_defconfig
    make tmp_defconfig &> rise/build.log

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    cp $imagepath $aikpath/Lineage/split_img/boot.img-zImage
    cp $dtbpath $aikpath/Lineage/split_img/boot.img-dt
    chmod +x $aikpath/Lineage/repackimg.sh
    $aikpath/Lineage/repackimg.sh

    ./cleanup.sh > /dev/null 2>&1

    if [[ "$debug" == "n" ]]; then
        clear
    fi

    echo "Building..."

    # A7 OneUI Q
    cat arch/arm64/configs/rise-a7y17lte_defconfig > arch/arm64/configs/tmp_defconfig
    cat arch/arm64/configs/oneui_defconfig >> arch/arm64/configs/tmp_defconfig
    make tmp_defconfig &> rise/build.log

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    cp $imagepath $aikpath/OneUI/split_img/boot.img-zImage
    cp $dtbpath $aikpath/OneUI/split_img/boot.img-dt
    chmod +x $aikpath/OneUI/repackimg.sh
    $aikpath/OneUI/repackimg.sh

    ./cleanup.sh > /dev/null 2>&1

    if [[ "$debug" == "n" ]]; then
        clear
    fi

    echo "Building..."

    # A7 Treble Q
    cat arch/arm64/boot/dts/exynos7880-a7y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
    cat arch/arm64/configs/rise-a7y17lte_defconfig > arch/arm64/configs/tmp_defconfig
    cat arch/arm64/configs/treble_defconfig >> arch/arm64/configs/tmp_defconfig
    make tmp_defconfig &> rise/build.log

    if [[ "$debug" == "n" ]]; then
        make -j64 &> rise/build.log
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    cp $imagepath $aikpath/Treble/split_img/boot.img-zImage
    cp $dtbpath $aikpath/Treble/split_img/boot.img-dt
    chmod +x $aikpath/Treble/repackimg.sh
    $aikpath/Treble/repackimg.sh

    ./cleanup.sh > /dev/null 2>&1

    clear

    rm arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
    rm arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
    
    rm arch/arm64/configs/tmp_defconfig

    echo "Creating flashable zip..."

    ./rise/zip/zip.sh $riseVer $buildDate

    clear
    printf "\nOutput zip is rise/zip/riseKernel-10.0-$riseVer-$buildDate-a5y17lte.zip\n\n"
}

clear
echo "Select build variant:"

select opt in "AOSP 10.0" "Treble 10.0" "OneUI 10.0" "All"
do

    clear
    echo "Selected: $opt"

    case $opt in
    "AOSP 10.0")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "AOSP 10.0" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "AOSP 10.0" "a7"
	else
	    echo "Unknown device: $device"
	fi
	break ;;

    "Treble 10.0")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "Treble 10.0" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "Treble 10.0" "a7"
	else
	    echo "Unknown device: $device"
	fi
	break ;;

    "OneUI 10.0")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "OneUI 10.0" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "OneUI 10.0" "a7"
	else
	    echo "Unknown device: $device"
	fi
	break ;;

    "All")
        BUILD_ALL
	break
    esac
done

echo "Build finished"

read -p "Clean build directory? [Y/n] " clean
if [[ "$clean" = "Y" || "$clean" = "y" ]]; then
    ./cleanup.sh > /dev/null 2>&1
elif [[ "$clean" = "N" || "$clean" = "n" ]]; then
    exit
else
    printf "Wrong entry '$clean'. Did you mean 'Y'?\nIf so, run cleanup.sh manually\n"
fi

