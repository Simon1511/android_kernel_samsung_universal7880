riseVer=$1
buildDate=$(date '+%Y%m%d')

rm *.zip

zip -r9 riseKernel-$riseVer-$buildDate-a57y17lte.zip META-INF/ rise/*/*.img
