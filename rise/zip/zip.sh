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

cat META-INF/com/google/android/update-binary > update-binary.bak

sed -i "s|PLACEHOLDER|'$riseVer'|g" META-INF/com/google/android/update-binary

# Use 7z if available
if [[ `which 7z` == *"7z"* ]]; then
    printf "\nCreating flashable zip using ${RED}7z${NC}..."
    7z a -mmt16 -x'!rise/*/PLACEHOLDER' -tzip riseKernel-$riseVer-$buildDate-a57y17lte.zip META-INF/ rise/*/*.img
else
    printf "\nCreating flashable zip using ${RED}zip${NC}..."
    zip -r9 riseKernel-$riseVer-$buildDate-a57y17lte.zip META-INF/ rise/*/*.img
fi

cat update-binary.bak > META-INF/com/google/android/update-binary
rm update-binary.bak

clear
printf "\nOutput zip is rise/zip/riseKernel-$riseVer-$buildDate-a5y17lte.zip\n\n"
