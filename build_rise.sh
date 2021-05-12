#!/bin/bash

# Set to y to see full ouput of make
debug=n

BUILD_BOOT() {
    variant=$1
    dev=$2

    export ARCH=arm64
    export SUBARCH=arm64
    export CROSS_COMPILE=/media/simon/Linux_data/android-build-tools/ubertc4/bin/aarch64-linux-android-
    export ANDROID_MAJOR_VERSION=p

    if [[ "$variant" == "AOSP 10.0" ]]; then
        if [[ "$dev" == "a5" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a5y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
            make lineage-a5y17lte_defconfig > /dev/null 2>&1
        elif [[ "$dev" == "a7" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a7y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
            make lineage-a7y17lte_defconfig > /dev/null 2>&1
        fi
    elif [[ "$variant" == "OneUI 10.0" ]]; then
        if [[ "$dev" == "a5" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a5y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
            make oneui-a5y17lte_defconfig > /dev/null 2>&1
        elif [[ "$dev" == "a7" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a7y17lte_lineage_oneui.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
            make oneui-a7y17lte_defconfig > /dev/null 2>&1
        fi
    elif [[ "$variant" == "Treble 10.0" ]]; then
        if [[ "$dev" == "a5" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a5y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
            make treble-a5y17lte_defconfig > /dev/null 2>&1
        elif [[ "$dev" == "a7" ]]; then
            cat arch/arm64/boot/dts/exynos7880-a7y17lte_treble.dtsi > arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
            make treble-a7y17lte_defconfig > /dev/null 2>&1
        fi
    fi

    echo "Building..."

    if [[ "$debug" == "n" ]]; then
        make -j64 > /dev/null 2>&1
    elif [[ "$debug" == "y" ]]; then
        make -j64
    fi

    if [[ "$dev" == "a5" ]]; then
        rm arch/arm64/boot/dts/exynos7880-a5y17lte_common.dtsi
    elif [[ "$dev" == "a7" ]]; then
        rm arch/arm64/boot/dts/exynos7880-a7y17lte_common.dtsi
    fi
}

echo "Select build variant:"

select opt in "AOSP 10.0" "Treble 10.0" "OneUI 10.0"
do

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
	break
    esac
done

echo "Build finished"


