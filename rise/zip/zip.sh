riseVer=$1
buildDate=$2

if [[ ! `echo $PWD` == *"rise/zip"* ]]; then
    cd rise/zip
fi

rm *.zip

cat META-INF/com/google/android/update-binary > update-binary.bak

sed -i "s|PLACEHOLDER|'$riseVer'|g" META-INF/com/google/android/update-binary


zip -r9 riseKernel-$riseVer-$buildDate-a57y17lte.zip META-INF/ rise/*/*.img


cat update-binary.bak > META-INF/com/google/android/update-binary
rm update-binary.bak
