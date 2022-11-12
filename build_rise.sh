#!/bin/bash

# Set to y to see output of make in terminal
debug=n
dtbpath=arch/arm64/boot/dtb.img
imagepath=arch/arm64/boot/Image
aikpath=rise/AIK

buildDate=$(date '+%Y%m%d')

riseVer=v1.10

deviceArray=(a5 a7)

# Set ARCH, toolchain path and android version
export ARCH=arm64
export SUBARCH=arm64
export ANDROID_MAJOR_VERSION=p

# Colors
RED='\033[0;31m'
NC='\033[0m'

if [ ! -d ../toolchain/ ]; then
    read -p "Toolchain not found! Download now? [Y/n] " tc

    rm -rf ../toolchain/

    if [[ "$tc" == "Y" || "$tc" == "y" ]]; then
        if [[ `which git` == *"git"* ]]; then
            git clone https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9 -b lineage-19.0 ../toolchain
            export CROSS_COMPILE=$PWD/../toolchain/bin/aarch64-linux-android-
        else
            echo "git is not installed, can't download toolchain!"
            exit
        fi
    elif [[ "$tc" == "N" || "$tc" == "n" ]]; then
        echo "WARN: Builds will fail without a toolchain!"
        export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-
    else
        echo "Wrong input: $tc"
        exit
    fi
else
    export CROSS_COMPILE=$PWD/../toolchain/bin/aarch64-linux-android-
fi

SET_LOCALVERSION() {
    sed -i 's|CONFIG_LOCALVERSION=""|CONFIG_LOCALVERSION="-riseKernel-'$1'.0-'$riseVer'"|g' arch/arm64/configs/rise-$2y17lte_defconfig
}

BUILD_BOOT() {
    variant=$1
    dev=$2
    full=$3
    
    if [ -f rise/build.log ]; then
        rm rise/build.log
    fi
    
    if [ -f rise/build.info ]; then
        rm rise/build.info
    fi

    if [ -f arch/arm64/configs/tmp_defconfig ]; then
        rm arch/arm64/configs/tmp_defconfig
    fi

    if [[ "$full" == "y" ]]; then
        echo "full=y" > rise/build.info
    else
        echo "full=n" > rise/build.info
    fi

    echo "variant=$variant" >> rise/build.info
    echo "device=$dev" >> rise/build.info

    # Set android version shown in localversion
    if echo "$variant" | grep -q "10"; then
       androidVer="10"
    elif echo "$variant" | grep -q "11"; then
       androidVer="11"
    elif echo "$variant" | grep -q "12"; then
       androidVer="12"
    elif echo "$variant" | grep -q "13"; then
       androidVer="13"
    fi

    SET_LOCALVERSION $androidVer $dev

    if [[ "$variant" == "AOSP "$androidVer".0" ]] || [[ "$variant" == "AOSP 12.0/12.1" ]] || [[ "$variant" == "OneUI "$androidVer".0" ]]; then
        cat arch/arm64/boot/dts/exynos7880-"$dev"y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-"$dev"y17lte_common.dtsi
        cat arch/arm64/configs/rise-"$dev"y17lte_defconfig >> arch/arm64/configs/tmp_defconfig

        if [[ "$variant" == "AOSP "$androidVer".0" ]] || [[ "$variant" == "AOSP 12.0/12.1" ]]; then
            cat arch/arm64/configs/lineage_defconfig >> arch/arm64/configs/tmp_defconfig
        elif [[ "$variant" == "OneUI "$androidVer".0" ]]; then
            cat arch/arm64/configs/oneui_defconfig >> arch/arm64/configs/tmp_defconfig
        fi

    elif [[ "$variant" == "Treble "$androidVer".0" ]]; then
        cat arch/arm64/boot/dts/exynos7880-"$dev"y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-"$dev"y17lte_common.dtsi
        cat arch/arm64/configs/rise-"$dev"y17lte_defconfig >> arch/arm64/configs/tmp_defconfig

        cat arch/arm64/configs/treble_defconfig >> arch/arm64/configs/tmp_defconfig
    fi

    if [[ "$variant" == "AOSP 12.0/12.1" ]] || [[ "$variant" == "AOSP 13.0" ]]; then

        # Force permissive for AOSP 12.0/12.1/13.0 since SePolicy is not written (yet?)
        sed -i 's|# CONFIG_FORCE_PERMISSIVE is not set|CONFIG_FORCE_PERMISSIVE=y|g' arch/arm64/configs/tmp_defconfig

        # Disable CONFIG_RT_GROUP_SCHED
        # Yet to figure out if this is actually needed
        sed -i 's|CONFIG_RT_GROUP_SCHED=y|# CONFIG_RT_GROUP_SCHED is not set|g' arch/arm64/configs/tmp_defconfig
    fi

    if [[ "$variant" == "AOSP 13.0" ]]; then
        sed -i 's|# CONFIG_STORE_MODE is not set|CONFIG_STORE_MODE=y|g' arch/arm64/configs/tmp_defconfig
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

    # Show error when the build failed
    if [ ! -f arch/arm64/boot/Image ]; then
        clear
        printf "${RED}ERROR${NC} encountered during build!\nSee rise/build.log for more information\n"
        exit
    fi

    rm arch/arm64/boot/dts/exynos7880-"$dev"y17lte_common.dtsi
    
    rm arch/arm64/configs/tmp_defconfig

    echo "Packing boot.img..."

    if [[ "$variant" == "AOSP "$androidVer".0" ]] || [[ "$variant" == "AOSP 12.0/12.1" ]]; then
        secVar="Lineage_"$androidVer".0"
    elif [[ "$variant" == "OneUI "$androidVer".0" ]]; then
        secVar="OneUI_"$androidVer".0"
    elif [[ "$variant" == "Treble "$androidVer".0" ]]; then
        secVar="Treble_"$androidVer".0"
    fi

    cp $imagepath $aikpath/$secVar/split_img/boot.img-zImage
    cp $dtbpath $aikpath/$secVar/split_img/boot.img-dt
    chmod +x $aikpath/$secVar/repackimg.sh
    $aikpath/$secVar/repackimg.sh

    clear

    secVar=$(echo "$secVar" | tr '[:upper:]' '[:lower:]')

    printf "\nOutput file is rise/boot_"$secVar"_"$dev".img\n\n"
}

BUILD_ALL() {
    if [ -f rise/build.log ]; then
        rm rise/build.log
    fi
    
    if [ -f rise/build.info ]; then
        rm rise/build.info
    fi

    if [ -f arch/arm64/configs/tmp_defconfig ]; then
        rm arch/arm64/configs/tmp_defconfig
    fi

    echo "Building..."

    for i in ${deviceArray[@]}; do
        BUILD_BOOT "AOSP 13.0" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

        BUILD_BOOT "AOSP 12.0/12.1" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

        BUILD_BOOT "AOSP 11.0" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

        BUILD_BOOT "AOSP 10.0" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

        BUILD_BOOT "OneUI 10.0" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

        BUILD_BOOT "Treble 11.0" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

        BUILD_BOOT "Treble 10.0" "$i" "y"
        ./cleanup.sh > /dev/null 2>&1

    done

    clear

    ./rise/zip/zip.sh $riseVer $buildDate
}

clear
echo "Select build variant: [1-8] "

select opt in "AOSP 13.0" "AOSP 12.0/12.1" "AOSP 11.0" "AOSP 10.0" "Treble 11.0" "Treble 10.0" "OneUI 10.0" "Installation zip"
do

    clear
    echo "Selected: $opt"

    case $opt in
    "AOSP 13.0")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "AOSP 13.0" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "AOSP 13.0" "a7"
	else
	    echo "Unknown device: $device"
	fi
	break ;;

    "AOSP 12.0/12.1")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "AOSP 12.0/12.1" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "AOSP 12.0/12.1" "a7"
	else
	    echo "Unknown device: $device"
	fi
	break ;;

    "AOSP 11.0")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "AOSP 11.0" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "AOSP 11.0" "a7"
	else
	    echo "Unknown device: $device"
	fi
	break ;;

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

    "Treble 11.0")
	read -p "Enter device: [A5/A7] " device
	if [[ "$device" == "A5" || "$device" == "a5" ]]; then
	    BUILD_BOOT "Treble 11.0" "a5"
	elif [[ "$device" == "A7" || "$device" == "a7" ]]; then
	    BUILD_BOOT "Treble 11.0" "a7"
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

    "Installation zip")
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

