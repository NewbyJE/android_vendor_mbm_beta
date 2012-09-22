package com.mbm.mbmservice;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.net.URLConnection;

import android.content.Context;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

public class ServiceLoop extends Thread {
    private static final String TAG = "MBM_GPS_SERVICE";

    private static final int CMD_DOWNLOAD_PGPS_DATA = 1;
    private static final int CMD_QUIT = 2;
    private static final int CMD_SEND_ALL_INFO = 3;

    private static final int DEFAULT_TIMEOUT = 10000;

    static LocalSocket localSocket;
    InputStream is;
    boolean quit;
    static boolean initDone;
    private Context context;

    public void run() {
        MbmLog.v(TAG, "Starting service loop");
        if(!initDone) {
            MbmLog.e(TAG, "Error, init not done. Can't start loop.");
            return;
        }
        try {
            int cmd;
            while(!quit) {
                try {
                    cmd = is.read();
                    MbmLog.d(TAG, "SERVICE LOOP READ: " + cmd);
                    if(cmd == -1)
                        break;
                } catch (Exception e) {
                    e.printStackTrace();
                    break;
                }

                switch(cmd) {
                case CMD_DOWNLOAD_PGPS_DATA:
                    downloadPgpsData();
                    break;
                case CMD_QUIT:
                    quit = true;
                    break;
                case CMD_SEND_ALL_INFO:
                    MbmService.getCurrentStatus().sendAllInfo();
                    break;
                default:
                    break;
                }
            }

            initDone = false;
            is.close();
            localSocket.close();
            MbmLog.v(TAG, "Service loop exiting");
        } catch (Exception e) {
            e.printStackTrace();
        }
    };

    public boolean init(Context context) {
        this.context = context;
        localSocket = new LocalSocket();
        try {
            localSocket.connect(new LocalSocketAddress("/data/data/mbmservice",LocalSocketAddress.Namespace.FILESYSTEM));
            is = localSocket.getInputStream();
            initDone = true;
            return true;
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
    }

    private String readPgpsUrl() throws IOException {
        int length = is.read();
        byte urlBytes[] = new byte[length];
        int read = is.read(urlBytes);
        if(read != length) {
            MbmLog.d(TAG, "readPgpsUrl read less than length.");
            return null;
        }

        return new String(urlBytes);
    }

    private void downloadPgpsData() {
        InputStream urlIS = null;
        FileOutputStream fos = null;
        try {
            String pgpsCmd = readPgpsUrl();
            String[] split = pgpsCmd.split(Status.MSG_DELIMETER, 2);
            int pgps_id = Integer.parseInt(split[0]);
            String sUrl = split[1];
            if(sUrl == null) {
                sendMessage(Status.MSG_PGPS_DATA + ":failed");
                return;
            }
            MbmLog.d(TAG, "Got id: " + pgps_id + ", url: " + sUrl);

            URL url = new URL(sUrl);
            URLConnection urlConn = url.openConnection();
            urlConn.setConnectTimeout(DEFAULT_TIMEOUT);
            urlConn.setReadTimeout(DEFAULT_TIMEOUT);

            urlIS = urlConn.getInputStream();
            fos = context.openFileOutput("pgps.data", Context.MODE_WORLD_READABLE);

            int oneChar, count=0;
            while((oneChar = urlIS.read()) != -1) {
                fos.write(oneChar);
                count++;
            }

            MbmLog.v(TAG, "Wrote " + count + " bytes to " + context.getFileStreamPath("pgps.data").getAbsolutePath());

            send(Status.MSG_PGPS_DATA + ":" +
                    Status.MSG_DELIMETER + "id=" + pgps_id +
                    Status.MSG_DELIMETER + "path=" + context.getFileStreamPath("pgps.data").getAbsolutePath() +
                    Status.MSG_DELIMETER);
        } catch (Exception e) {
            e.printStackTrace();
            send(Status.MSG_PGPS_DATA + ":failed");
        } finally {
            if(urlIS != null) {
                try {
                    urlIS.close();
                } catch (IOException ioe) {
                    ioe.printStackTrace();
                }
            }
            if(fos != null) {
                try {
                    fos.close();
                } catch (IOException ioe) {
                    ioe.printStackTrace();
                }
            }
        }
    }

    private void send(String text) {
        MbmLog.v(TAG, text);
        try {
            ServiceLoop.sendMessage(text);
        } catch (IOException e) {
            MbmLog.e(TAG,"Could not send to libmbm-gps socket");
            e.printStackTrace();
        }
    }

    public static synchronized boolean sendMessage(String text) throws IOException {
        if(!initDone) {
            MbmLog.d(TAG, "not sending message, init not done");
            return false;
        }
        /* send msb (of 16 bits) as one byte */
        localSocket.getOutputStream().write(text.getBytes().length >> 8);
        /* send lsb */
        localSocket.getOutputStream().write(text.getBytes().length);
        localSocket.getOutputStream().write(text.getBytes());
        localSocket.getOutputStream().flush();

        return true;
    }
}
