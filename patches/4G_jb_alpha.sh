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
#
echo "apply device/hp/tenderloin cherrypicks"
pushd device/hp/tenderloin
git fetch http://review.cyanogenmod.com/CyanogenMod/android_device_hp_tenderloin refs/changes/15/24215/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply frameworks/base cherrypicks"
pushd frameworks/base
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/83/24883/1 && git cherry-pick -n FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/android_frameworks_base refs/changes/57/24957/1 && git cherry-pick -n FETCH_HEAD
git reset HEAD
popd
#
echo "apply kernel/hp/tenderloin cherrypicks"
pushd kernel/hp/tenderloin
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/16/24816/1 && git cherry-pick -n FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/17/24817/1 && git cherry-pick -n FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/18/24818/1 && git cherry-pick -n FETCH_HEAD
git fetch http://review.cyanogenmod.com/CyanogenMod/hp-kernel-tenderloin refs/changes/19/24819/1 && git cherry-pick -n FETCH_HEAD
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
git apply ~/android/cm10jb/vendor/mbm/patches/development.patch
cd ~/android/cm10jb
#
echo "hardware_ril patch"
cd hardware/ril
git apply ~/android/cm10jb/vendor/mbm/patches/hardware_ril.patch
cd ~/android/cm10jb
#
echo "kernel_hp_tenderloin patch"
cd kernel/hp/tenderloin
git apply ~/android/cm10jb/vendor/mbm/patches/kernel_hp_tenderloin.patch
git apply ~/android/cm10jb/vendor/mbm/patches/kernel_hp_tenderloin_A6.patch
cd ~/android/cm10jb
#
echo "system_core patch"
cd system/core
git apply ~/android/cm10jb/vendor/mbm/patches/system_core.patch
cd ~/android/cm10jb
#
echo "apply vendor cm patch"
cd vendor/cm
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

