#!/bin/bash 

if [ -z $ANDROID_BUILD_TOP ]; then
    echo "Error: ANDROID_BUILD_TOP not set, please source build/envsetup.sh"
    #exit 1;
fi

MYDIR=$ANDROID_BUILD_TOP/vendor/mbm

$MYDIR/patches/apply_mbm_patches.sh $*

old_dir=`pwd`
#####################################
cd $ANDROID_BUILD_TOP/device/hp/tenderloin
patch -p1 < $MYDIR/patches-internal/enable_mbm_ril_gps.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $ANDROID_BUILD_TOP/kernel/hp/tenderloin
patch -p1 < $MYDIR/patches-internal/kernel_config.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $ANDROID_BUILD_TOP/system/core
patch -p1 < $MYDIR/patches-internal/enable_mbm_devices.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cp $MYDIR/patches-internal/apns-conf.xml $ANDROID_BUILD_TOP/device/hp/tenderloin/
cp $MYDIR/patches-internal/gps.conf $ANDROID_BUILD_TOP/device/hp/tenderloin/
#####################################
cd $old_dir
exit $ret
