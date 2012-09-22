package com.mbm.mbmservice;

import java.util.ArrayList;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.provider.Settings;
import android.telephony.TelephonyManager;
import android.os.StrictMode;

public class MbmServiceReceiver extends BroadcastReceiver {
    private static final String TAG = "MBM_GPS_SERVICE";

    private OperatorInfo currentOperator;

    private static String oldApn = " ";

    public MbmServiceReceiver() {
        super();
        currentOperator = new OperatorInfo("", "", "");
    }

    private void updateOperatorInfo(TelephonyManager tm) {
        String networkOperator = tm.getNetworkOperator();
        String networkOperatorName = tm.getNetworkOperatorName();
        boolean roaming = tm.isNetworkRoaming();
        if (networkOperator != null && networkOperator.length() >= 5) {
            MbmLog.d(TAG, "NetworkOperator: " + networkOperator);
            String oMcc = networkOperator.substring(0, 3);
            String oMnc = networkOperator.substring(3);
            currentOperator.setName(networkOperatorName);
            currentOperator.setMcc(oMcc);
            currentOperator.setMnc(oMnc);

            MbmService.getCurrentStatus().setOperatorInfo(currentOperator);
        }
    }

    public void onCellLocationChanged(TelephonyManager tm) {
        MbmLog.v(TAG, "onCellLocationChanged");
        updateOperatorInfo(tm);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        MbmLog.v(TAG, "MBM onReceive");
        String id = "";
        String apn = "";
        String password = "";
        String name = "";
        String username = "";
        String type = "";
        String mcc = "";
        String mnc = "";
        String authtype = "";
        String current = "";

        Status currentStatus = MbmService.getCurrentStatus();

        if (intent.getAction().equals(Intent.ACTION_BOOT_COMPLETED)) {
            MbmLog.v(TAG, "BOOT COMPLETED");
            Intent i = new Intent();
            i.setAction("com.mbm.mbmservice.MbmService");
            context.startService(i);
        } else if (intent.getAction().equals(
                "android.location.PROVIDERS_CHANGED")) {
            MbmLog.v(TAG, "Location providers changed");
            MbmService.onLocationProviderChanged();
        } else if (intent.getAction().equals(
                Intent.ACTION_AIRPLANE_MODE_CHANGED)) {
            Object state = intent.getExtras().get("state");
            currentStatus.setAirplaneMode(state);
        } else if (intent.getAction().equals(
                ConnectivityManager.CONNECTIVITY_ACTION)) {
            NetworkInfo info = intent
                    .getParcelableExtra(ConnectivityManager.EXTRA_NETWORK_INFO);
            NetworkInfo extrainfo = intent
                    .getParcelableExtra(ConnectivityManager.EXTRA_OTHER_NETWORK_INFO);
            boolean no_connection = intent.getBooleanExtra(
                    ConnectivityManager.EXTRA_NO_CONNECTIVITY, false);

            currentStatus.setNetworkInfo(info);
            currentStatus.setExtraNetworkInfo(extrainfo);
            currentStatus.setNoConnectivity(no_connection);
        } else if (intent.getAction().equals(
                ConnectivityManager.ACTION_BACKGROUND_DATA_SETTING_CHANGED)) {
            ConnectivityManager cm = (ConnectivityManager) context
                    .getSystemService(Context.CONNECTIVITY_SERVICE);
            boolean backgroundData = cm.getBackgroundDataSetting();

            currentStatus.setBackgroundDataSetting(backgroundData);
        } else if (intent.getAction().equals(
                "android.intent.action.ANY_DATA_STATE")) {
            Object state = intent.getExtras().get("state");
            if (intent.getExtras().get("state") != null) {
                ConnectivityManager cm = (ConnectivityManager) context
                        .getSystemService(Context.CONNECTIVITY_SERVICE);
                boolean backgroundData = cm.getBackgroundDataSetting();
                currentStatus.setBackgroundDataSetting(backgroundData);

                boolean mobileDataAllowed = Settings.Secure.getInt(
                        context.getContentResolver(), "mobile_data", 1) == 1;
                currentStatus.setMobileDataAllowed(mobileDataAllowed);

                boolean roamingAllowed = Settings.Secure.getInt(
                        context.getContentResolver(),
                        Settings.Secure.DATA_ROAMING, 1) == 1;
                currentStatus.setRoamingAllowed(roamingAllowed);

                TelephonyManager tm = (TelephonyManager) context
                        .getSystemService(Context.TELEPHONY_SERVICE);
                updateOperatorInfo(tm);

                if (state.equals("CONNECTED") || state.equals("DISCONNECTED")) {
                    currentStatus.setDataState(state.toString().toLowerCase());

                    StrictMode.ThreadPolicy old = StrictMode.getThreadPolicy();
                    StrictMode.setThreadPolicy(new StrictMode.ThreadPolicy
                        .Builder(old)
                        .permitDiskReads()
                        .permitDiskWrites()
                        .build());

                    ArrayList<ApnInfo> preferapns = new ArrayList<ApnInfo>(5);
                    Cursor mCursor = context.getContentResolver().query(
                            Uri.parse("content://telephony/carriers/preferapn"),
                            null, null, null, null);

                    if (mCursor != null && (mCursor.getCount() > 0) ) {

                        while (mCursor != null && mCursor.moveToNext()) {
                            id = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("_id"));
                            name = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("name"));
                            apn = ""
                                    + mCursor.getString(
                                            mCursor.getColumnIndex("apn"))
                                            .toLowerCase();
                            username = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("user"));
                            password = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("password"));
                            mcc = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("mcc"));
                            mnc = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("mnc"));
                            authtype = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("authtype"));
                            type = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("type"));

                            preferapns.add(new ApnInfo(name, apn, username, password, mcc,
                                    mnc, type, authtype));
                            }
                            mCursor.close();
                    } else {
                            MbmLog.w(TAG,"preferapn database empty or missing");
                            currentStatus.setApnInfo(null);
                    }

                    ArrayList<ApnInfo> apns = new ArrayList<ApnInfo>(5);
                    mCursor = context.getContentResolver().query(
                            Uri.parse("content://telephony/carriers"), null,
                            "current=1", null, "name,type ASC");

                    if (mCursor != null && (mCursor.getCount() > 0) && (preferapns.size() > 0) ) {

                        while (mCursor != null && mCursor.moveToNext()) {
                            id = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("_id"));
                            name = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("name"));
                            apn = ""
                                    + mCursor.getString(
                                            mCursor.getColumnIndex("apn"))
                                            .toLowerCase();
                            username = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("user"));
                            password = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("password"));
                            mcc = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("mcc"));
                            mnc = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("mnc"));
                            authtype = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("authtype"));
                            type = ""
                                    + mCursor.getString(mCursor
                                            .getColumnIndex("type"));

                            apns.add(new ApnInfo(name, apn, username, password, mcc,
                                    mnc, type, authtype));
                        }
                        mCursor.close();
                        ApnInfo pApn = ApnInfo.getPreferredApn(preferapns, apns,
                                currentOperator);
                        if ((pApn != null) && !(oldApn.equals(pApn.getApn())) ) {
                            oldApn = pApn.getApn();
                            MbmLog.i(TAG,"SUPL Name[" + pApn.getName() +
                                "]APN[" + pApn.getApn() +
                                "]USER[" + pApn.getUsername() +
                                "]PASS[Not Displayed" +
                                "]AUTH[" + pApn.getAuthtype() +
                                "]TYPE[" + pApn.getType() + "]");
                        }
                        currentStatus.setApnInfo(pApn);
                    } else {
                        MbmLog.w(TAG,"carrier database empty or missing");
                        currentStatus.setApnInfo(null);
                    }
                    StrictMode.setThreadPolicy(old);
                }
            }
        } else if (intent.getAction().equals(
                TelephonyManager.ACTION_PHONE_STATE_CHANGED)) {
            MbmLog.v(TAG, "phone state changed");

            ConnectivityManager cm = (ConnectivityManager) context
                    .getSystemService(Context.CONNECTIVITY_SERVICE);
            boolean backgroundData = cm.getBackgroundDataSetting();
            currentStatus.setBackgroundDataSetting(backgroundData);

            boolean mobileDataAllowed = Settings.Secure.getInt(
                    context.getContentResolver(), "mobile_data", 1) == 1;
            currentStatus.setMobileDataAllowed(mobileDataAllowed);

            boolean roamingAllowed = Settings.Secure.getInt(
                    context.getContentResolver(), Settings.Secure.DATA_ROAMING,
                    1) == 1;
            currentStatus.setRoamingAllowed(roamingAllowed);
        } else if (intent.getAction().equals(Intent.ACTION_TIME_CHANGED)) {
            currentStatus.setTime(currentStatus.getCurrentTime());
        }
    }
}
