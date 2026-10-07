#!/bin/bash
# Manual phone APK build with dev keystore (release keystore password lost).
# Usage: build-phone-dev.sh
set -e
APP_DIR=~/workspace/works/projects/xr-wrist-display/phone
OUT_APK=$APP_DIR/app/build/outputs/apk/release/app-release.apk
BUILD_TYPE="release"

BT=~/android/build-tools/35.0.0
PLATFORM=~/android/platforms/android-35/android.jar
AAPT2=$BT/aapt2
D8=$BT/d8
ZIPALIGN=$BT/zipalign
APKSIGNER=$BT/apksigner
KOTLINC=~/dl/kotlinc/bin/kotlinc

export JAVA_HOME=$HOME/jdk
export PATH=$JAVA_HOME/bin:$PATH

WORK=$(mktemp -d)
trap "rm -rf $WORK" EXIT

SRC=$APP_DIR/app/src/main
MANIFEST_SRC=$SRC/AndroidManifest.xml

VERSION_CODE=$(grep -oP "versionCode\s+\K\d+" $APP_DIR/app/build.gradle | head -1)
VERSION_NAME=$(grep -oP "versionName\s+['\"]\K[^'\"]+" $APP_DIR/app/build.gradle | head -1)
MIN_SDK=$(grep -oP "minSdk\s+\K\d+" $APP_DIR/app/build.gradle | head -1)
TARGET_SDK=$(grep -oP "targetSdk\s+\K\d+" $APP_DIR/app/build.gradle | head -1)
MANIFEST=$WORK/AndroidManifest.xml
python3 -c "
import re
with open('$MANIFEST_SRC') as f: m = f.read()
m = re.sub(r'<manifest([^>]*)>', r'<manifest\1 android:versionCode=\"$VERSION_CODE\" android:versionName=\"$VERSION_NAME\">', m, count=1)
uses_sdk = '<uses-sdk android:minSdkVersion=\"$MIN_SDK\" android:targetSdkVersion=\"$TARGET_SDK\" />'
if '<uses-sdk' not in m:
    m = re.sub(r'(<manifest[^>]*>)', r'\1' + uses_sdk, m, count=1)
with open('$MANIFEST','w') as f: f.write(m)
print('manifest version: $VERSION_CODE / $VERSION_NAME, sdk: $MIN_SDK -> $TARGET_SDK')
"

echo "=== aapt2 compile ==="
$AAPT2 compile --dir $SRC/res -o $WORK/res.zip

echo "=== aapt2 link ==="
LINK_ARGS="-o $WORK/base.apk -I $PLATFORM --manifest $MANIFEST --java $WORK/gen"
$AAPT2 link $LINK_ARGS $WORK/res.zip

echo "=== kotlinc ==="
mkdir -p $WORK/classes
STDLIB=~/dl/kotlinc/lib/kotlin-stdlib.jar
find $SRC/java -name "*.kt" > $WORK/sources.txt
find $WORK/gen -name "*.java" >> $WORK/sources.txt
set +e
$KOTLINC -cp $PLATFORM -d $WORK/classes @${WORK}/sources.txt -jvm-target 17 > $WORK/kotlinc.log 2>&1
KOTLIN_RC=$?
set -e
grep -v "^$" $WORK/kotlinc.log | head -30 || true
if [ $KOTLIN_RC -ne 0 ] || grep -qE "error:" $WORK/kotlinc.log; then
  echo "KOTLINC FAILED — aborting build"
  grep -E "error:" $WORK/kotlinc.log | head -20
  exit 1
fi
[ -n "$(find $WORK/classes -name '*.class' | head -1)" ] || { echo "KOTLINC produced no classes — aborting"; exit 1; }
echo "kotlinc ok: $(find $WORK/classes -name '*.class' | wc -l) classes"

echo "=== d8 ==="
mkdir -p $WORK/dex
$D8 --lib $PLATFORM --min-api 29 --output $WORK/dex $(find $WORK/classes -name "*.class" | tr '\n' ' ') $STDLIB

echo "=== package ==="
cp $WORK/base.apk $WORK/unsigned.apk
cd $WORK/dex && zip -q -j $WORK/unsigned.apk classes.dex && cd - > /dev/null
# Android R+ requires AndroidManifest.xml STORED; also keep resources.arsc STORED
cd $WORK && unzip -p unsigned.apk AndroidManifest.xml > AndroidManifest.xml
zip -0 -q unsigned.apk AndroidManifest.xml && cd - > /dev/null
# Ensure resources.arsc is STORED (uncompressed) for API 30+
APK_WORK=$WORK python3 << 'PYEOF'
import zipfile, os
apk = os.path.join(os.environ['APK_WORK'], 'unsigned.apk')
with zipfile.ZipFile(apk, 'r') as zin:
    infos = zin.infolist()
    arsc = [i for i in infos if i.filename == 'resources.arsc']
    if arsc and arsc[0].compress_type != zipfile.ZIP_STORED:
        print("re-storing resources.arsc as STORED")
        data = {i.filename: zin.read(i.filename) for i in infos}
        with zipfile.ZipFile(apk, 'w', zipfile.ZIP_DEFLATED) as zout:
            for i in infos:
                ni = zipfile.ZipInfo(i.filename, date_time=i.date_time)
                ni.external_attr = i.external_attr
                ni.compress_type = zipfile.ZIP_STORED if i.filename in ('resources.arsc','AndroidManifest.xml') else zipfile.ZIP_DEFLATED
                zout.writestr(ni, data[i.filename])
    else:
        print("resources.arsc already STORED")
PYEOF

echo "=== zipalign ==="
$ZIPALIGN -f 4 $WORK/unsigned.apk $WORK/aligned.apk

echo "=== sign ==="
KS=~/workspace/works/projects/xr-wrist-display/phone/xrwrist-phone.keystore
$APKSIGNER sign --ks $KS --ks-pass pass:xrwrist-dev --key-pass pass:xrwrist-dev \
  --out $OUT_APK $WORK/aligned.apk

echo "=== verify ==="
$APKSIGNER verify --print-certs $OUT_APK | head -5
echo "BUILT: $OUT_APK"
ls -la $OUT_APK
