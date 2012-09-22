/* Ericsson libgpsctrl
 *
 * Copyright (C) Ericsson AB 2011
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * Author: Torgny Johansson <torgny.johansson@ericsson.com>
 *
 */
#ifndef GPS_CTRL_H
#define GPS_CTRL_H 1

/* supl configuration */
typedef struct {
    char *apn;
    char *username;
    char *password;
    char *authtype;
    char *supl_server;
    int allow_uncert;    /* allow servers without matching certificate */
    int enable_ni;       /* enable network initiated supl requests */
} GpsCtrlSuplConfig;

#define GC_CHAR_LEN 255
#define CLEAR_AIDING_DATA_NONE 0
#define CLEAR_AIDING_DATA_ALL 1
#define CLEAR_AIDING_DATA_EPH_ONLY 2

/* supl ni request */
typedef struct {
    int message_id;
    int message_type;
    int requestor_id_type;                      /* optional */
    /* char requestor_id_text[GC_CHAR_LEN];         optional */
    char *requestor_id_text;        /* optional */
    int client_name_type;                       /* optional */
    /* char client_name_text[GC_CHAR_LEN];          optional */
    char *client_name_text;         /* optional */
} GpsCtrlSuplNiRequest;

/* queued event function */
typedef void* (* gpsctrl_queued_event) (void *data);

/* callback for supl ni requests */
typedef void (* gpsctrl_supl_ni_callback)(GpsCtrlSuplNiRequest *supl_ni_request);

/* Callback for supl failures. Could be used to start a fallback mode instead */
typedef void (* gpsctrl_supl_fail_callback)(void);

typedef struct {
    char *at_dev;
    char *nmea_dev;
    int at_fd;
    int nmea_fd;
    int pref_mode;
    int fallback;
    int interval;
    int isInitialized;
    int supl_initialized;
    int background_data_allowed;
    int data_roaming_allowed;
    int data_enabled;
    int is_roaming;
    int is_ready;
    int loglevel;
    gpsctrl_supl_fail_callback supl_fail_callback;
    gpsctrl_supl_ni_callback supl_ni_callback;
    GpsCtrlSuplConfig supl_config;
}GpsCtrlContext;

/* gps modes */
#define MODE_STAND_ALONE 1
#define MODE_SUPL 3
#define MODE_PGPS 4

/* supl ni replies */
#define SUPL_NI_DENY  0
#define SUPL_NI_ALLOW 1

#define SUPL_CID 25

/* get the current context */
GpsCtrlContext* get_gpsctrl_context(void);

/* enqueue an event */
void enqueue_event (gpsctrl_queued_event queued_event, void *data);

/* set the devices to be used */
int gpsctrl_set_devices (char *ctrl_dev, char* nmea_dev);

/* get the at device name */
char *gpsctrl_get_at_device(void);

/* initialize context */
int gpsctrl_init(int loglevel);

/* open at and nmea channels */
int gpsctrl_open (int at_fd, void (*onClose)(void));

/* set position mode; stand alone, supl, etc and the desired fix interval*/
void gpsctrl_set_position_mode (int mode, int recurrence);

/* get the nmea fd */
int gpsctrl_get_nmea_fd(void);

/* init supl related configuration, apn, supl server etc */
int gpsctrl_init_supl (int allow_uncert, int enabled_ni);

/* init pgps related configuration */
int gpsctrl_init_pgps(void);

/* set the supl server */
int gpsctrl_set_supl_server (char *server);

/* set the supl apn */
int gpsctrl_set_apn_info (char *apn, char *user, char *password, char* authtype);

/*  set callback for supl failures */
void gpsctrl_set_supl_fail_callback (gpsctrl_supl_fail_callback supl_fail_callback);

/*  set callback for supl ni requests */
void gpsctrl_set_supl_ni_callback (gpsctrl_supl_ni_callback supl_ni_callback);

/* reply on supl network initiated requests */
int gpsctrl_supl_ni_reply (GpsCtrlSuplNiRequest *supl_ni_request, int allow);

/* delete aiding data */
int gpsctrl_delete_aiding_data (int clear_flag); /* must wait 10 seconds after gps has stopped */

/* start the gps */
int gpsctrl_start(void);

/* start the gps in fallback mode */
int gpsctrl_start_fallback(void);

/* stop the gps */
int gpsctrl_stop(void);

/* close at and nmea ports and cleanup */
int gpsctrl_cleanup(void);

/* set if data is enabled */
void gpsctrl_set_data_enabled (int enabled);

/* set if background data is allowed */
void gpsctrl_set_background_data_allowed (int allowed);

/* set if data roaming is allowed */
void gpsctrl_set_data_roaming_allowed (int allowed);

/* set if the device is roaming */
void gpsctrl_set_is_roaming (int roaming);

/* set if the device is connected to the internet */
void gpsctrl_set_is_connected (int connected);

/* set if the device is available and ready */
void gpsctrl_set_device_is_ready (int ready);

/* get if the device is available and ready */
int gpsctrl_get_device_is_ready(void);

#endif /* GPS_CTRL_H */
