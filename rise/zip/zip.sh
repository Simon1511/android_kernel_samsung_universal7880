#!/bin/bash

riseVer=$1
buildDate=$2

# Colors
RED='\033[0;31m'
NC='\033[0m'

if [[ ! `echo $PWD` == *"rise/zip"* ]]; then
    cd rise/zip
fi

rm *.zip

sed -i 's|#PLACEHOLDER|ui_print "           '$riseVer'            "|g' META-INF/com/google/android/update-binary

# Use 7z if available
if [[ `which 7z` == *"7z"* ]]; then
    printf "\nCreating flashable zip using ${RED}7z${NC}..."
    7z a -mmt16 -x'!rise/*/PLACEHOLDER' -tzip riseKernel-$riseVer-$buildDate-a57y17lte.zip META-INF/ rise/*/*.img
else
    printf "\nCreating flashable zip using ${RED}zip${NC}..."
    zip -r9 riseKernel-$riseVer-$buildDate-a57y17lte.zip META-INF/ rise/*/*.img
fi

clear
printf "\nOutput zip is rise/zip/riseKernel-$riseVer-$buildDate-a5y17lte.zip\n\n"

sed -i 's|ui_print "           '$riseVer'            "|#PLACEHOLDER|g' META-INF/com/google/android/update-binary
