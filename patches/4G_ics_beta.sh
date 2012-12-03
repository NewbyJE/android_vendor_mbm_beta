#/bin/bash
#
echo "Started new build"
echo `date +%Y%m%d-%H%M`
echo "setup android"
cd ~/android/system
TOP="~/android/system"; export TOP
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
echo "apply Trebuchet cherrypicks"
pushd packages/apps/Trebuchet
# Trebuchet: Add overlayable config for tablet workspace grid size (Jellybean version already merged into CM10 already)
git fetch http://review.cyanogenmod.org/CyanogenMod/android_packages_apps_Trebuchet refs/changes/62/20962/2 && git cherry-pick FETCH_HEAD
git reset HEAD
popd
#
echo "apply frameworks/base cherrypicks"
pushd frameworks/base
# telephony: Fix MMS for when operator has different APNs for Data and MMS
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/83/24883/1 && git cherry-pick -n FETCH_HEAD
# SystemUI/res: Move files to proper directory
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/26/24826/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply kernel/hp/tenderloin cherrypicks"
pushd kernel/hp/tenderloin
# drivers: Update to most recent acm, wdm, ncm, and usbnet drivers
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/16/24816/1 && git cherry-pick -n FETCH_HEAD
# tenderloin4g_android_defconfig: Update for MBM HAL 4.0.0 BETA
git fetch http://review.cyanogenmod.org/CyanogenMod/hp-kernel-tenderloin refs/changes/06/27506/1 && git cherry-pick -n FETCH_HEAD
# mdmgpio: Change permissions for mdm_poweron
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/19/24819/1 && git cherry-pick -n FETCH_HEAD
# Fixed Touchpad magnetometer
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/49/24149/2 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply system/core cherrypick"
pushd system/core
# devices: enables cdc-wdmX devices for ICS
git fetch http://review.cyanogenmod.com/CyanogenMod/android_system_core refs/changes/25/24825/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "device_hp_tenderloin patch"
cd device/hp/tenderloin
# Pollerr.sh, NITZ switch, Overlay changes
git apply ~/android/system/vendor/mbm/patches/device_hp_tenderloin.patch
cd ~/android/system
#
echo "frameworks_base patch"
cd frameworks/base
# Additional support for HSPA+ (HSPAP)
git apply ~/android/system/vendor/mbm/patches/frameworks_base.patch
cd ~/android/system
#
echo "hardware_ril patch"
cd hardware/ril
# Ability to enter AT commands to the MBM HAL from the command line using radiooptions
git apply ~/android/system/vendor/mbm/patches/hardware_ril.patch
cd ~/android/system
#
echo "kernel_hp_tenderloin patch"
cd kernel/hp/tenderloin
# Increase size of logger files to assist with debugging
git apply ~/android/system/vendor/mbm/patches/kernel_hp_tenderloin.patch
git apply ~/android/system/vendor/mbm/patches/kernel_hp_tenderloin_A6.patch
cd ~/android/system
#
echo "packages_apps_email patch"
cd packages/apps/Email
# Allow email download of compressed files
git apply ~/android/system/vendor/mbm/patches/packages_apps_Email.patch
cd ~/android/system
#
echo "system_core patch"
cd system/core
# Proper radio buffer sizes and increased size for dmesg dump
git apply ~/android/system/vendor/mbm/patches/system_core.patch
cd ~/android/system
#
echo "vendor_mbm patch"
cd vendor/mbm
# NITZ and RSSI plus GPS fixes
git apply ~/android/system/vendor/mbm/patches/vendor_mbm_gps.patch
git apply ~/android/system/vendor/mbm/patches/vendor_mbm_ril.patch
cd ~/android/system
#
echo "apply vendor/cm patch"
cd vendor/cm
# Non-release builds and increase default boot animation resolution
git apply ~/android/system/vendor/mbm/patches/vendor_cm.patch
./get-prebuilts
echo "change to custom bootanimation"
cp /home/john/Andriod_Builds/bootanimation/Nice_CM9_bootanimation.zip prebuilt/common/bootanimation/horizontal-1024x768.zip
cd ~/android/system
#
echo "setup make files"
pushd device/hp/tenderloin/
./setup-makefiles.sh
popd
#
echo "clean build and set name"
make clean
export CM_BUILDTYPE=MBM_Beta
export CM_EXTRAVERSION_DATESTAMP=1
export CM_EXTRAVERSION_TAG="4g_beta1"
#
echo "brunch tenderloin"
ccache --max-size=50G
brunch tenderloin 2>&1 | tee ics_build.log
echo "Completed new build"
echo `date +%Y%m%d-%H%M`

