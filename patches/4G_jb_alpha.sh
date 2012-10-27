#/bin/bash
#
echo "Started new build"
echo `date +%Y%m%d-%H%M`
echo "setup android"
cd ~/android/cm10jb
TOP="~/android/cm10jb"; export TOP
source build/envsetup.sh
export USE_CCACHE=1
#
echo "resync git pointers"
repo forall -c git reset --hard
# repo forall -c git reset --hard HEAD
echo "repo sync"
repo sync -j16
#
echo "apply device/hp/tenderloin cherrypicks"
pushd device/hp/tenderloin
# Fixed Touchpad magnetometer forcing more reasonable update rate
git fetch http://review.cyanogenmod.com/CyanogenMod/android_device_hp_tenderloin refs/changes/15/24215/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply frameworks/base cherrypicks"
pushd frameworks/base
# telephony: Fix MMS for when operator has different APNs for Data and MMS
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/83/24883/1 && git cherry-pick -n FETCH_HEAD
# core and telephony: Additional HSPAP support
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/57/24957/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply kernel/hp/tenderloin cherrypicks"
pushd kernel/hp/tenderloin
# drivers: Update to most recent acm, wdm, ncm, and usbnet drivers
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/16/24816/1 && git cherry-pick -n FETCH_HEAD
# drivers: Fix part of the hub race condition on TouchPad 2.6.35
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/17/24817/1 && git cherry-pick -n FETCH_HEAD
# tenderloin4g_android_defconfig: Update for MBM HAL 4.0.0 BETA
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/18/24818/1 && git cherry-pick -n FETCH_HEAD
# mdmgpio: Change permissions for mdm_poweron
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/19/24819/1 && git cherry-pick -n FETCH_HEAD
# Fixed Touchpad magnetometer
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/49/24149/2 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "special, revert qcom update until fixed"
pushd hardware/qcom/display
git fetch http://review.cyanogenmod.com/CyanogenMod/android_hardware_qcom_display refs/changes/74/24874/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "development patch"
cd development
# Add package name providing network location support.
git apply ~/android/cm10jb/vendor/mbm/patches/development.patch
cd ~/android/cm10jb
#
echo "hardware_ril patch"
cd hardware/ril
# Ability to enter AT commands to the MBM HAL from the command line using radiooptions
git apply ~/android/cm10jb/vendor/mbm/patches/hardware_ril.patch
cd ~/android/cm10jb
#
echo "kernel_hp_tenderloin patch"
cd kernel/hp/tenderloin
# Increase size of logger files to assist with debugging
git apply ~/android/cm10jb/vendor/mbm/patches/kernel_hp_tenderloin.patch
git apply ~/android/cm10jb/vendor/mbm/patches/kernel_hp_tenderloin_A6.patch
cd ~/android/cm10jb
#
echo "system_core patch"
cd system/core
# Proper radio buffer sizes and increased size for dmesg dump
git apply ~/android/cm10jb/vendor/mbm/patches/system_core.patch
cd ~/android/cm10jb
#
echo "apply vendor cm patch"
cd vendor/cm
# Non-release builds
git apply ~/android/cm10jb/vendor/mbm/patches/vendor_cm.patch
./get-prebuilts
cd ~/android/cm10jb
#
echo "setup make files"
pushd device/hp/tenderloin/
./setup-makefiles.sh
cd ~/android/cm10jb
#
echo "clean build and set name"
make clean
export CM_BUILDTYPE=MBM_Beta
export CM_EXTRAVERSION_DATESTAMP=1
export CM_EXTRAVERSION_TAG="4g_alpha0"
#
echo "brunch tenderloin"
ccache --max-size=50G
brunch tenderloin 2>&1 | tee jb_build.log
echo "Completed new build"
echo `date +%Y%m%d-%H%M`

