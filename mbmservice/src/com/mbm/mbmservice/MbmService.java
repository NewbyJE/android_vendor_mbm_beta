package com.mbm.mbmservice;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.os.IBinder;
import android.telephony.CellLocation;
import android.telephony.PhoneStateListener;
import android.telephony.TelephonyManager;

public class MbmService extends Service
{
    private static final String TAG = "MBM_GPS_SERVICE";
    private static boolean gps_enabled;
    private static boolean serviceStarted;
    private static Context context;
    private static LocationManager lm;
    private static Status currentStatus = null;
    public IBinder onBind(Intent intent)
    {
        return null;
    }

    @Override
    public void onCreate()
    {
        MbmLog.v(TAG, "MbmGpsService Created");
        final MbmServiceReceiver msr = new MbmServiceReceiver();
        final TelephonyManager tm = (TelephonyManager) getSystemService(Context.TELEPHONY_SERVICE);
        lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        getApplicationContext().registerReceiver(msr, new IntentFilter(ConnectivityManager.ACTION_BACKGROUND_DATA_SETTING_CHANGED));

        PhoneStateListener cellLocationListener = new PhoneStateListener() {
            public void onCellLocationChanged(CellLocation location) {
                msr.onCellLocationChanged(tm);
            }
        };

        tm.listen(cellLocationListener, PhoneStateListener.LISTEN_CELL_LOCATION);

        gps_enabled = lm.isProviderEnabled(LocationManager.GPS_PROVIDER);

        context = getApplicationContext();

        if(currentStatus == null)
            currentStatus = new Status();

        startServiceLoop();

        serviceStarted = true;
    }

    private static synchronized void startServiceLoop() {
        if(gps_enabled) {
            Runnable serviceMonitor = new Runnable() {
                public void run() {
                    ServiceLoop serviceLoop = new ServiceLoop();
                    int retries = 0;

                    while(!serviceLoop.init(context)) {
                        MbmLog.e(TAG, "Error initing service loop");
                        if(retries++ > 15) {
                            MbmLog.e(TAG, "15 attempts exhausted. Giving up.");
                            return;
                        }
                        MbmLog.e(TAG, "Retrying in 5 seconds");
                        try {
                            Thread.sleep(5000);
                        } catch (InterruptedException e) {
                            e.printStackTrace();
                        }
                    }

                    MbmLog.d(TAG, "MBM Service initiated");
                    try {
                        serviceLoop.start();
                    } catch(IllegalThreadStateException ie) {
                        ie.printStackTrace();
                        MbmLog.e(TAG, "Service loop already running");
                    }
                }
            };

            Thread t = new Thread(serviceMonitor);
            t.start();
        } else {
            MbmLog.d(TAG, "Gps is not enabled. Not starting service loop");
        }
    }

    public static void onLocationProviderChanged() {
        if(serviceStarted) {
            gps_enabled = lm.isProviderEnabled(LocationManager.GPS_PROVIDER);
            startServiceLoop();
        } else {
            MbmLog.d(TAG, "Not starting service loop since service not started yet");
        }

    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        MbmLog.v(TAG, "MbmGpsService -- onStartCommand()");

        // We want this service to continue running until it is explicitly
        // stopped, so return sticky.
        return START_STICKY;
    }


    @Override
    public void onDestroy()
    {
        MbmLog.v(TAG, "MbmGpsService Destroyed");
    }

    public static Status getCurrentStatus() {
        if(currentStatus == null)
            currentStatus = new Status();
        return currentStatus;
    }
}
