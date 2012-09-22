#!/bin/bash

if [ -z $ANDROID_BUILD_TOP ]; then
    echo "Error: ANDROID_BUILD_TOP not set, please source build/envsetup.sh"
    exit 1;
fi

MYDIR=$ANDROID_BUILD_TOP/vendor/mbm/patches

old_dir=`pwd`
#####################################
cp $MYDIR/files/cdc.h.2.6.39.4 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/include/linux/usb/cdc.h
#####################################
cd $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/usb/class
cp $MYDIR/files/cdc-acm.c.2.6.39.4 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/usb/class/cdc-acm.c
cp $MYDIR/files/cdc-acm.h.2.6.39.4 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/usb/class/cdc-acm.h
patch -p1 < $MYDIR/files/cdc-acm.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/usb/class
cp $MYDIR/files/cdc-wdm.c.next-2012-03-09 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/usb/class/cdc-wdm.c
cp $MYDIR/files/cdc-wdm.h.next-2012-03-09 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/include/linux/usb/cdc-wdm.h
patch -p1 < $MYDIR/files/cdc-wdm.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/net/usb
cp $MYDIR/files/cdc_ncm.c.next-2012-03-17 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/net/usb/cdc_ncm.c
patch -p1 < $MYDIR/files/cdc_ncm.patch
patch -p1 < $MYDIR/files/cdc_ncm_fix_ifc_error_count.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/net/usb
cp $MYDIR/files/usbnet.c.next-2012-03-24 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/drivers/net/usb/usbnet.c
patch -p1 < $MYDIR/files/usbnet.c.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $ANDROID_BUILD_TOP/kernel/hp/tenderloin/include/linux/usb
cp $MYDIR/files/usbnet.h.next-2012-03-24 $ANDROID_BUILD_TOP/kernel/hp/tenderloin/include/linux/usb/usbnet.h
patch -p1 < $MYDIR/files/usbnet.h.patch
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
#####################################
cd $old_dir
exit $ret

