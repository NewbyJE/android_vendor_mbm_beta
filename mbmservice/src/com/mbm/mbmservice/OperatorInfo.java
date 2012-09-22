package com.mbm.mbmservice;

public class OperatorInfo {
    private String name;
    private String mcc;
    private String mnc;
    
    public OperatorInfo(String name, String mcc, String mnc) {
        this.name = name.trim();
        this.mcc = mcc.trim();
        this.mnc = mnc.trim();
    }
    
    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getMcc() {
        return mcc;
    }

    public void setMcc(String mcc) {
        this.mcc = mcc;
    }

    public String getMnc() {
        return mnc;
    }

    public void setMnc(String mnc) {
        this.mnc = mnc;
    }

    public String toString() {
        return "Name[" + name + "]MCC/MNC[" + mcc + "/" + mnc + "]";
    }
}
