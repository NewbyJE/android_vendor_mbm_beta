package com.mbm.mbmservice;

import java.util.ArrayList;

public class ApnInfo {
    private static final String TAG = "MBM_GPS_SERVICE";

    private String name;
    private String apn;
    private String password;
    private String username;
    private String type;
    private String mcc;
    private String mnc;
    private String authtype;

    public ApnInfo(String name, String apn, String username, String password, String mcc, String mnc, String type, String authtype) {
        this.name = name.trim();
        this.apn = apn.trim();
        this.username = username.trim();
        this.password = password.trim();
        this.mcc = mcc.trim();
        this.mnc = mnc.trim();
        this.type = type.trim();
        this.authtype = authtype.trim();
    }

    public String getName() {
        return name;
    }

    public String getApn() {
        return apn;
    }

    public String getPassword() {
        return password;
    }

    public String getUsername() {
        return username;
    }

    public String getType() {
        return type;
    }

    public String getMcc() {
        return mcc;
    }

    public String getMnc() {
        return mnc;
    }

    public String getAuthtype() {
        return authtype;
    }

    public static ApnInfo getPreferredApn(ArrayList<ApnInfo> preferapns, ArrayList<ApnInfo> apns, OperatorInfo operator) {
        int suplApnId = -1;
        int defaultApnId = -1;

        ApnInfo preferapn = preferapns.get(0);
        MbmLog.d(TAG, "PreferAPN " + preferapn.toString());

        // First find any matching APN and SUPL Type, if not
        // found then store the ID so that we can find it
        // in the sorted list.

         MbmLog.v(TAG, "APNList 1st attempt" + apns.size() );
         for (int i=0; i < apns.size(); i++) {
            ApnInfo apn = apns.get(i);
            MbmLog.v(TAG, "#" + i + "(" + apn.toString() + ")");
            if ( preferapn.getApn().equals(apn.getApn()) ) {
                if (apn.getType().toLowerCase().contains("supl")) {
                    MbmLog.d(TAG, "Found APN & SUPL match, ID:" + i);
                    suplApnId = i;
                    break;
                } else if (apn.getType().toLowerCase().contains("internet") ||
                        apn.getType().toLowerCase().contains("default") ||
                        apn.getType().toLowerCase().contains("*") ||
                        apn.getType().toLowerCase().length() == 0) {
                    defaultApnId = i;
                    break;
                }
            }
        }

        if (suplApnId != -1)
            return apns.get(suplApnId);
        else if(defaultApnId == -1) {
            MbmLog.d(TAG, "No Matching APN found!");
            return null;
        }

        // No luck in finding a matching APN and SUPL Type,
        // then we need to rely on that the APN Name is
        // defined as "Operator ******"
        // Start searching from default APN.

        String[] operArray = apns.get(defaultApnId).getName().split(" ");
        String opername = operArray[0];

        MbmLog.v(TAG, "Searching for Operator:" + opername);

        MbmLog.v(TAG, "APNList 2nd attempt" + apns.size() );
        for (int i=defaultApnId+1; i < apns.size(); i++) {
            ApnInfo apn = apns.get(i);
            String apnoperArray[] = apn.getName().split(" ");
            if (opername.equals(apnoperArray[0])) {
                MbmLog.v(TAG, "#" + i + "(" + apn.toString() + ")");
                if (apn.getType().toLowerCase().contains("supl")) {
                    MbmLog.d(TAG, "Found SUPL match, ID:" + i);
                    suplApnId = i;
                    break;
                } else if (apn.getType().toLowerCase().contains("internet") ||
                        apn.getType().toLowerCase().contains("default") ||
                        apn.getType().toLowerCase().contains("*") ||
                        apn.getType().toLowerCase().length() == 0) {
                    MbmLog.d(TAG, "Found default match, ID:" + i);
                    defaultApnId = i;
                    break;
                }
            }
        }

        if (suplApnId != -1)
            return apns.get(suplApnId);
        else if(defaultApnId != -1)
            return apns.get(defaultApnId);
        else {
            MbmLog.i(TAG, "SUPL APN not found!");
            return null;
        }
    }

    public String toString() {
        return "Name[" + name + "]Apn[" + apn + "]Type[" + type + "]User[" + username + "]MCC/MNC[" + mcc + "/" + mnc + "]";
    }
}
