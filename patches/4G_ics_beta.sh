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
git fetch http://review.cyanogenmod.com/CyanogenMod/android_device_hp_tenderloin refs/changes/15/24215/1 && git cherry-pick FETCH_HEAD
popd
#
echo "apply frameworks/base cherrypicks"
pushd frameworks/base
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/39/21339/1 && git cherry-pick FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/83/24883/1 && git cherry-pick FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/26/24826/1 && git cherry-pick FETCH_HEAD
popd
#
echo "apply kernel/hp/tenderloin cherrypicks"
pushd kernel/hp/tenderloin
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/16/24816/1 && git cherry-pick FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/17/24817/1 && git cherry-pick FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/18/24818/1 && git cherry-pick FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/19/24819/1 && git cherry-pick FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/49/24149/2 && git cherry-pick FETCH_HEAD
popd
#
echo "apply system/core cherrypick"
pushd system/core
git fetch http://review.cyanogenmod.com/CyanogenMod/android_system_core refs/changes/25/24825/1 && git cherry-pick FETCH_HEAD
popd
#
echo "frameworks_base patch"
cd frameworks/base
git apply ~/android/system/vendor/mbm/patches/frameworks_base.patch
cd ~/android/system
#
echo "hardware_ril patch"
cd hardware/ril
git apply ~/android/system/vendor/mbm/patches/hardware_ril.patch
cd ~/android/system
#
echo "kernel_hp_tenderloin patch"
cd kernel/hp/tenderloin
git apply ~/android/system/vendor/mbm/patches/kernel_hp_tenderloin.patch
cd ~/android/system
#
echo "system_core patch"
cd system/core
git apply ~/android/system/vendor/mbm/patches/system_core.patch
cd ~/android/system
#
echo "apply vendor/cm patch"
cd vendor/cm
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
export CM_EXTRAVERSION_TAG="4g_beta0"
#
echo "brunch tenderloin"
ccache --max-size=50G
brunch tenderloin 2>&1 | tee ics_build.log
echo "Completed new build"
echo `date +%Y%m%d-%H%M`

