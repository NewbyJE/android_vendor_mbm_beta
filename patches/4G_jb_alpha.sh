#/bin/bash
#
# Set environment for your build system
TOP=~/android/cm10jb; export TOP
#
echo "Started new build"
echo `date +%Y%m%d-%H%M`
echo "setup android"
cd $TOP
source build/envsetup.sh
export USE_CCACHE=1
#
echo "resync git pointers"
repo forall -c git reset --hard
# repo forall -c git reset --hard HEAD
echo "repo sync"
repo sync -j16
repo status
#
echo "delete extra files"
pushd device/hp/tenderloin
rm camerahal/cameraConfig.cpp
rm libsensors/MPLSensor.cpp
rm libsensors/MPLSensor.h
rm tenderloinparts/res/xml/camera_preferences.xml
rm tenderloinparts/src/com/cyanogenmod/settings/device/CameraFragmentActivity.java
popd
pushd kernel/hp/tenderloin/
rm drivers/misc/mpu3050/accel/bma250.c
rm drivers/misc/mpu3050/compass/ak8972.c
popd
#
echo "copy the most recent HP TP proprietary files"
cp -R device/hp/tenderloin/Proprietary/* vendor/hp/tenderloin/proprietary/
#
echo "apply device/hp/tenderloin cherrypicks"
pushd device/hp/tenderloin
# TouchPad Camera Settings (Tomasz Rostanski)
git fetch http://review.cyanogenmod.org/CyanogenMod/android_device_hp_tenderloin refs/changes/93/29493/1 && git cherry-pick -n FETCH_HEAD
# Add Gyroscope sensor userspace driver (Tomasz Rostanski)
git fetch http://review.cyanogenmod.org/CyanogenMod/android_device_hp_tenderloin refs/changes/38/29838/10 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply frameworks/base cherrypicks"
pushd frameworks/base
# telephony: Fix MMS for when operator has different APNs for Data and MMS (John Newby)
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/83/24883/1 && git cherry-pick -n FETCH_HEAD
# add criticalBatteryShutdownLevel; fix plugged/charging status (James Sullins)
git fetch http://review.cyanogenmod.org/CyanogenMod/android_frameworks_base refs/changes/89/28689/2 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply kernel/hp/tenderloin cherrypicks"
pushd kernel/hp/tenderloin
# Update MPU3050 kernel driver (Tomasz Rostanski)
git fetch http://review.cyanogenmod.org/CyanogenMod/hp-kernel-tenderloin refs/changes/33/29733/8 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "development patch"
cd development
# Add package name providing network location support.
git apply $TOP/vendor/mbm/patches/development.patch
cd $TOP
#
echo "hardware_ril patch"
cd hardware/ril
# Ability to enter AT commands to the MBM HAL from the command line using radiooptions
git apply $TOP/vendor/mbm/patches/hardware_ril.patch
cd $TOP
#
echo "kernel_hp_tenderloin patch"
cd kernel/hp/tenderloin
# usb_debug_dirty.patch: More debug information for the USB issues, correction in the pehci.c driver, and revised hub race patch
git apply $TOP/vendor/mbm/patches/usb_debug_dirty.patch
cd $TOP
#
echo "packages_apps_email patch"
cd packages/apps/Email
# Allow email download of compressed files
git apply $TOP/vendor/mbm/patches/packages_apps_Email.patch
cd $TOP
#
echo "system_core patch"
cd system/core
# Proper radio buffer sizes and increased size for dmesg dump
git apply $TOP/vendor/mbm/patches/system_core.patch
cd $TOP
#
echo "vendor_mbm patch"
cd vendor/mbm
# NITZ and RSSI plus GPS fixes and fast dormancy
git apply $TOP/vendor/mbm/patches/vendor_mbm.patch
cd $TOP
#
echo "apply vendor cm patch"
cd vendor/cm
# Non-release builds
git apply $TOP/vendor/mbm/patches/vendor_cm.patch
cd $TOP
#
echo "setup make files"
pushd device/hp/tenderloin/
./setup-makefiles.sh
cd $TOP
#
echo "clean build and set name"
make clean
export CM_BUILDTYPE=MBM_Beta
export CM_EXTRAVERSION_DATESTAMP=1
export CM_EXTRAVERSION_TAG="4g_alpha1"
#
echo "brunch tenderloin"
ccache --max-size=50G
brunch tenderloin 2>&1 | tee jb_build.log
echo "Completed new build"
echo `date +%Y%m%d-%H%M`
