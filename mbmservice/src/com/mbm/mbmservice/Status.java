package com.mbm.mbmservice;

import java.io.IOException;
import java.text.NumberFormat;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

import android.net.NetworkInfo;
import android.os.Handler;

import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneFactory;

public class Status {
    private static final String TAG = "MBM_GPS_SERVICE";

    private static final int EVENT_OEM_RIL_MESSAGE = 13;

    static final String MSG_AIRPLANE_MODE = "AIRPLANE_MODE";
    static final String MSG_EXTRA_NETWORK_INFO = "EXTRA_NETWORK_INFO";
    static final String MSG_EXTRA_OTHER_NETWORK_INFO = "EXTRA_OTHER_NETWORK_INFO";
    static final String MSG_EXTRA_NO_CONNECTIVITY = "EXTRA_NO_CONNECTIVITY";
    static final String MSG_BACKGROUND_DATA_SETTING = "BACKGROUND_DATA_SETTING";
    static final String MSG_MOBILE_DATA_ALLOWED = "MOBILE_DATA_ALLOWED";
    static final String MSG_ROAMING_ALLOWED = "ROAMING_ALLOWED";
    static final String MSG_ANY_DATA_STATE = "ANY_DATA_STATE";
    static final String MSG_APN_INFO = "APN_INFO";
    static final String MSG_NO_APN_DEFINED = "NO_APN_DEFINED";
    static final String MSG_OPERATOR_INFO = "OPERATOR_INFO";
    static final String MSG_PGPS_DATA = "MSG_PGPS_DATA";
    static final String MSG_DELIMETER = "\n";

    private ApnInfo apnInfo;
    private OperatorInfo operatorInfo;
    private NetworkInfo networkInfo;
    private NetworkInfo extraNetworkInfo;
    private boolean backgroundDataSetting, mobileDataAllowed, roamingAllowed,
            noConnectivity;
    private Object airplaneMode;
    private String dataState;
    private String time;

    public Status() {
        backgroundDataSetting = mobileDataAllowed = roamingAllowed = false;
        noConnectivity = true;
        apnInfo = null;
        operatorInfo = null;
        networkInfo = extraNetworkInfo = null;
        airplaneMode = null;
        dataState = "";
        time = getCurrentTime();
    }

    private void send(String text) {
        MbmLog.v(TAG, text);
        try {
            ServiceLoop.sendMessage(text);
        } catch (IOException e) {
            MbmLog.e(TAG, "Could not send to libmbm-gps socket");
            e.printStackTrace();
        }
    }

    public String getCurrentTime() {
        String sign;
        NumberFormat format = NumberFormat.getInstance();
        Date date = new Date();
        Calendar cal = Calendar.getInstance();
        TimeZone tz = cal.getTimeZone();
        int tzQuarters = (tz.getRawOffset() / (1000 * 3600)) * 4;
        SimpleDateFormat dateFormat = new SimpleDateFormat("yy/MM/dd,HH:mm:ss");
        String dateString = dateFormat.format(date);

        if (tzQuarters < 0) {
            sign = "-";
            tzQuarters = -tzQuarters;
        } else
            sign = "+";

        format.setMinimumIntegerDigits(2);
        time = dateString + "" + sign + format.format(tzQuarters);

        return time;
    }

    public void sendAllInfo() {
        if (apnInfo != null)
            send(MSG_APN_INFO + ":" + "name=" + apnInfo.getName()
                    + MSG_DELIMETER + "apn=" + apnInfo.getApn() + MSG_DELIMETER
                    + "user=" + apnInfo.getUsername() + MSG_DELIMETER + "pass="
                    + apnInfo.getPassword() + MSG_DELIMETER + "mcc="
                    + apnInfo.getMcc() + MSG_DELIMETER + "mnc="
                    + apnInfo.getMnc() + MSG_DELIMETER + "type="
                    + apnInfo.getType() + MSG_DELIMETER + "authtype="
                    + apnInfo.getAuthtype() + MSG_DELIMETER);
        else
            send(MSG_NO_APN_DEFINED);

        if (operatorInfo != null)
            send(MSG_OPERATOR_INFO + ":" + "name=" + operatorInfo.getName()
                    + MSG_DELIMETER + "mcc=" + operatorInfo.getMcc()
                    + MSG_DELIMETER + "mnc=" + operatorInfo.getMnc());
        else
            MbmLog.d(TAG, "operatorInfo not set before sendAllInfo");

        if (networkInfo != null)
            send(MSG_EXTRA_NETWORK_INFO + ":" + "type=" + networkInfo.getType()
                    + MSG_DELIMETER + "roaming=" + networkInfo.isRoaming());
        else
            MbmLog.d(TAG, "networkInfo not set before sendAllInfo");

        send(MSG_EXTRA_OTHER_NETWORK_INFO + ":" + extraNetworkInfo);
        send(MSG_BACKGROUND_DATA_SETTING + ":" + backgroundDataSetting);
        send(MSG_MOBILE_DATA_ALLOWED + ":" + mobileDataAllowed);
        send(MSG_ROAMING_ALLOWED + ":" + roamingAllowed);
        send(MSG_EXTRA_NO_CONNECTIVITY + ":" + noConnectivity);
        send(MSG_AIRPLANE_MODE + ":" + airplaneMode);
        send(MSG_ANY_DATA_STATE + ":" + dataState);
    }

    public ApnInfo getApnInfo() {
        return apnInfo;
    }

    public void setApnInfo(ApnInfo apnInfo) {
        this.apnInfo = apnInfo;

        if (apnInfo != null)
            send(MSG_APN_INFO + ":" + "name=" + apnInfo.getName()
                    + MSG_DELIMETER + "apn=" + apnInfo.getApn() + MSG_DELIMETER
                    + "user=" + apnInfo.getUsername() + MSG_DELIMETER + "pass="
                    + apnInfo.getPassword() + MSG_DELIMETER + "mcc="
                    + apnInfo.getMcc() + MSG_DELIMETER + "mnc="
                    + apnInfo.getMnc() + MSG_DELIMETER + "type="
                    + apnInfo.getType() + MSG_DELIMETER + "authtype="
                    + apnInfo.getAuthtype() + MSG_DELIMETER);
        else
            send(MSG_NO_APN_DEFINED);
    }

    public OperatorInfo getOperatorInfo() {
        return operatorInfo;
    }

    public void setOperatorInfo(OperatorInfo operatorInfo) {
        this.operatorInfo = operatorInfo;

        if (operatorInfo != null)
            send(MSG_OPERATOR_INFO + ":" + "name=" + operatorInfo.getName()
                    + MSG_DELIMETER + "mcc=" + operatorInfo.getMcc()
                    + MSG_DELIMETER + "mnc=" + operatorInfo.getMnc());
    }

    public NetworkInfo getNetworkInfo() {
        return networkInfo;
    }

    public void setNetworkInfo(NetworkInfo networkInfo) {
        this.networkInfo = networkInfo;

        if (networkInfo != null)
            send(MSG_EXTRA_NETWORK_INFO + ":" + "type=" + networkInfo.getType()
                    + MSG_DELIMETER + "roaming=" + networkInfo.isRoaming());
    }

    public NetworkInfo getExtraNetworkInfo() {
        return extraNetworkInfo;
    }

    public void setExtraNetworkInfo(NetworkInfo extraNetworkInfo) {
        this.extraNetworkInfo = extraNetworkInfo;

        send(MSG_EXTRA_OTHER_NETWORK_INFO + ":" + extraNetworkInfo);
    }

    public boolean isBackgroundDataSetting() {
        return backgroundDataSetting;
    }

    public void setBackgroundDataSetting(boolean backgroundDataSetting) {
        this.backgroundDataSetting = backgroundDataSetting;

        send(MSG_BACKGROUND_DATA_SETTING + ":" + backgroundDataSetting);
    }

    public boolean isMobileDataAllowed() {
        return mobileDataAllowed;
    }

    public void setMobileDataAllowed(boolean mobileDataAllowed) {
        this.mobileDataAllowed = mobileDataAllowed;

        send(MSG_MOBILE_DATA_ALLOWED + ":" + mobileDataAllowed);
    }

    public boolean isRoamingAllowed() {
        return roamingAllowed;
    }

    public void setRoamingAllowed(boolean roamingAllowed) {
        this.roamingAllowed = roamingAllowed;

        send(MSG_ROAMING_ALLOWED + ":" + roamingAllowed);
    }

    public boolean isNoConnectivity() {
        return noConnectivity;
    }

    public void setNoConnectivity(boolean noConnectivity) {
        this.noConnectivity = noConnectivity;

        send(MSG_EXTRA_NO_CONNECTIVITY + ":" + noConnectivity);
    }

    public Object getAirplaneMode() {
        return airplaneMode;
    }

    public void setAirplaneMode(Object airplaneMode) {
        this.airplaneMode = airplaneMode;

        send(MSG_AIRPLANE_MODE + ":" + airplaneMode);
    }

    public String getDataState() {
        return dataState;
    }

    public void setDataState(String dataState) {
        this.dataState = dataState;

        send(MSG_ANY_DATA_STATE + ":" + dataState);
    }

    public String getTime() {
        return time;
    }

    public void setTime(String time) {
        this.time = time;

        Handler handler = new Handler();
        String strings[] = new String[1];
        strings[0] = "AT+CCLK=\"" + time + "\"\r\n";
        Phone phone = PhoneFactory.getDefaultPhone();
        phone.invokeOemRilRequestStrings(strings, handler.obtainMessage(EVENT_OEM_RIL_MESSAGE));
    }
}
