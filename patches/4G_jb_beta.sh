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
ccache --max-size=50G
export USE_CCACHE=1
#
echo "resync git pointers"
repo forall -c git reset --hard
# repo forall -c git reset --hard HEAD
echo "repo sync"
repo sync -j16
repo status
#
echo "copy the most recent HP TP proprietary files"
rm -r vendor/hp/tenderloin/proprietary/*
cp -R device/hp/tenderloin/Proprietary/* vendor/hp/tenderloin/proprietary/
#
echo "apply external/tinyalsa cherrypicks"
pushd external/tinyalsa
# WIP: properly support multivalued controls (James Sullins)
git fetch http://review.cyanogenmod.org/CyanogenMod/android_external_tinyalsa refs/changes/46/33646/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply frameworks/base cherrypicks"
pushd frameworks/base
# telephony: Fix MMS for when operator has different APNs for Data and MMS (John Newby)
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/83/24883/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "development patch"
cd development
# Add package name providing network location support.
git apply $TOP/vendor/mbm/patches/development.patch
cd $TOP
#
# echo "device_hp_tenderloin patches"
# cd device/hp/tenderloin
# Change to portrait mode.
# git apply $TOP/vendor/mbm/patches/device_hp_tenderloin_portrait.patch
# cd $TOP
#
echo "external_wpa_supplicant_8 patch"
cd external/wpa_supplicant_8
# turn on nl80211 driver support and fix missing commands.
git apply $TOP/vendor/mbm/patches/external_wpa_supplicant_8.patch
cd $TOP
#
echo "hardware_atheros_wlan patch"
cd hardware/atheros/wlan
# fix missing commands.
git apply $TOP/vendor/mbm/patches/hardware_atheros_wlan.patch
cd $TOP
#
echo "hardware_libhardware_legacy patch"
cd hardware/libhardware_legacy
# fix wlan0 loading and fw path directory.
git apply $TOP/vendor/mbm/patches/hardware_libhardware_legacy_jen.patch
cd $TOP
#
echo "system_core patch"
cd system/core
# Proper radio buffer sizes and increased size for dmesg dump
git apply $TOP/vendor/mbm/patches/system_core.patch
cd $TOP
#
echo "system_netd patch"
cd system/netd
# Start up wlan0 interface earlier.
git apply $TOP/vendor/mbm/patches/system_netd.patch
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
./get-prebuilts
cd $TOP
#
echo "setup make files"
pushd device/hp/tenderloin/
./setup-makefiles.sh
cd $TOP
#
echo "clean build and set name"
make clean
export CM_BUILDTYPE=MBM_L
export CM_EXTRAVERSION_DATESTAMP=1
export CM_EXTRAVERSION_TAG="4g_beta5"
#
echo "brunch tenderloin"
ccache --max-size=50G
brunch tenderloin 2>&1 | tee jb_build.log
echo "Completed new build"
echo `date +%Y%m%d-%H%M`

