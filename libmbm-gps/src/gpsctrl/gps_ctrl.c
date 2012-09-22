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

#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "atchannel.h"
#include "at_tok.h"
#include "nmeachannel.h"
#include "misc.h"
#include "gps_ctrl.h"
#include "supl.h"
#include "pgps.h"

static GpsCtrlContext global_gpsctrl_context;

#define LOG_NDEBUG 0
#define LOG_TAG "libgpsctrl"
#include "../log.h"

typedef struct {
    gpsctrl_queued_event handler;
    char *data;
} queued_event;

/**************************************************************
 * Internal functions
 *
 */
static int initializeGpsCtrlContext(int loglevel)
{
    GpsCtrlContext *context;

    context = &global_gpsctrl_context;
    memset(context, 0, sizeof(GpsCtrlContext));

    context->data_enabled = 0;
    context->background_data_allowed = 0;
    context->data_roaming_allowed = 0;
    context->is_roaming = 0;
    context->supl_initialized = 0;
    context->fallback = 0;
    context->is_ready = 0;
    context->loglevel = loglevel;
    context->isInitialized = 1;

    ALOGI("Initialized new gps ctrl context");

    return 0;
}

/* handle E2GPSSTAT unsolicited messages */
static void* onGpsStatusChange (void *s)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;
    int ignore;
    int i;
    int supl_status;
    int mode;
    char *line = (char *) s;

    ENTER;

    err = at_tok_start(&line);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        return NULL;
    }

    /* Ignore 3 integers from line */
    for (i = 0; i < 3; i++) {
        err = at_tok_nextint(&line, &ignore);
        if (err < 0) {
            MBMLOGE("%s error parsing data", __FUNCTION__);
            return NULL;
        }
    }

    /* now we look at the mode flag */
    err = at_tok_nextint(&line, &mode);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        return NULL;
    }

    /* now we look at the supl status flag */
    err = at_tok_nextint(&line, &supl_status);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        return NULL;
    }

    if (supl_status != 0) {
        MBMLOGW("%s, supl failed. Fallback required.", __FUNCTION__);
        if (context->supl_fail_callback)
            context->supl_fail_callback();
    }

    /*
     * in certain cases supl can fail but the e2gpsstat
     * doesn't correctly indicate that. Instead the status is
     * 1,0,0,0,0. In that case, we should fallback if the preferred
     * mode is supl. It's important that e2gpsstat is turned off in gpsctrl_stop
     * _before_ e2gpsctl=0 is sent. Otherwise we may end up in a loop here
     */
    if ((mode == 0) && (supl_status == 0) && (context->pref_mode == MODE_SUPL)) {
        MBMLOGW("%s, supl seems to have failed. Fallback required.", __FUNCTION__);
        if (context->supl_fail_callback)
            context->supl_fail_callback();
    }

    EXIT;
    return NULL;
}

/* Handle queued unsolicited events */
static void *unsolicitedHandler(void *data)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    queued_event *event = (queued_event *)data;
    if (NULL == event)
        MBMLOGE("%s: event = NULL", __FUNCTION__);
    else if (NULL == event->handler)
        MBMLOGE("%s: event->handler = NULL", __FUNCTION__);
    else {
        event->handler(event->data);
        free(event->data);
        free(event);
    }

    EXIT;
    return NULL;
}

/**
 * Called by atchannel when an unsolicited line appears.
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here.
 */
static void onUnsolicited(const char *s, const char *sms_pdu)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    (void) sms_pdu;

    ENTER;

    /* enqueue events for any function calls that will send at commands */

    if (strStartsWith(s, "*E2GPSSUPLNI:"))
        onSuplNiRequest((char *)s);
    else if (strStartsWith(s, "*EEGPSEEDATA:"))
        onPgpsUrlReceived((char *)s);
    else {
        gpsctrl_queued_event gpsctrl_event = NULL;
        queued_event *event = NULL;
        const char *str = NULL;
        int err;

        /* List queuing events below */

        /* Actually, current implementation of the queuing creating separate
         * threads for each queued event could in theory, though not very
         * likely, lead to that the unsolicited messages are handled in
         * reverse order. So far, analysis of the unsolicited messages handled
         * below do not indicate issues, even if handled in reverse order. For
         * any new message added, an analysis is needed, and when we hit a
         * message which will have issues, we need to consider a new
         * implementation of the queuing strategy.
         */
        if (strStartsWith(s, "*E2CERTUN:")) {
            gpsctrl_event = onUnknownCertificate;
            str = s;
        }
        else if (strStartsWith(s, "*E2GPSSTAT:")) {
            gpsctrl_event = onGpsStatusChange;
            str = s;
        }

        if (gpsctrl_event && str) {
            event = malloc(sizeof(queued_event));
            if (!event) {
                MBMLOGE("%s: allocating memory for event", __FUNCTION__);
                EXIT;
                return;
            }

            event->handler = gpsctrl_event;
            err = asprintf(&event->data, "%s", str);
            if (err < 0) {
                MBMLOGE("%s: allocating memory for event->data", __FUNCTION__);
                free(event);
                EXIT;
                return;
            }
            enqueue_event(unsolicitedHandler, (void *)event);
        }
    }
    EXIT;
}

static void onATTimeout(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    MBMLOGW("AT channel timeout; restarting..\n");
    /* Last resort, throw escape on the line, close the channel
       and hope for the best. */
    at_send_escape();

    /* TODO We may cause a reset here. */
}


/**************************************************************
 * MBM GPS CTRL interface functions
 *
 */
/* get the current context */
GpsCtrlContext* get_gpsctrl_context(void)
{
    if (!global_gpsctrl_context.isInitialized)
        ALOGE("%s, GPSCtrl context not initialized. Possible problems ahead!", __FUNCTION__);
    return &global_gpsctrl_context;
}

/* enqueue a function to be executed in own thread
 * maybe this should be changed to not spawn a new thread
 * every time?
 */
void enqueue_event (gpsctrl_queued_event queued_event, void *data)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int ret;

    ENTER;

    pthread_t event_thread;

    ret = pthread_create(&event_thread, NULL, queued_event, data);
    if (ret < 0)
        MBMLOGE("%s error creating event thread", __FUNCTION__);

    EXIT;
}

/* set the devices to be used */
int gpsctrl_set_devices (char *at_dev, char* nmea_dev)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int ret;

    ENTER;

    ret = asprintf(&context->at_dev, "%s", at_dev);
    if (ret < 0) {
        EXIT;
        return -1;
    }

    ret = asprintf(&context->nmea_dev, "%s", nmea_dev);
    if (ret < 0) {
        EXIT;
        return -1;
    }

    MBMLOGI("%s, control device: %s, nmea device: %s", __FUNCTION__, context->at_dev, context->nmea_dev);

    EXIT;
    return 0;
}

/* get the ctrl device name */
char *gpsctrl_get_at_device(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    return context->at_dev;
}

/* initialize context */
int gpsctrl_init(int loglevel)
{
    if (initializeGpsCtrlContext(loglevel)) {
        ALOGE("initializeGpsCtrlContext() failed!");
        return -1;
    }

    supl_init_context();

    return 0;
}


/* open at and nmea channels */
int gpsctrl_open (int at_fd, void (*onClose)(void))
{
    int ret;
    int err;
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    context->pref_mode = MODE_STAND_ALONE;
    context->interval = 2;

    context->at_fd = at_fd;

    /* initialize at channel thread */
    ret = at_reader_open(context->at_fd, onUnsolicited, context->loglevel);
    if (ret < 0) {
        MBMLOGE("%s, AT error %d on create_at_thread\n", __FUNCTION__, ret);
        EXIT;
        return -1;
    }

    at_set_on_reader_closed(onClose);
    at_set_on_timeout(onATTimeout);

    if (at_handshake() < 0) {
        MBMLOGE("%s, at handshake failed", __FUNCTION__);
        EXIT;
        return -1;
    }

    err = at_send_command("AT");
    if (err < 0) {
        MBMLOGE("%s, error sending AT", __FUNCTION__);
        EXIT;
        return -1;
    }

    /* Set default character set. */
    err = at_send_command("AT+CSCS=\"UTF-8\"");
    if (err < 0) {
        MBMLOGE("%s, error setting utf8", __FUNCTION__);
        EXIT;
        return -1;
    }

    at_make_default_channel();
    at_set_timeout_msec(1000 * 180);

    /* initialize nmea channel */
    context->nmea_fd = nmea_open(context->nmea_dev);
    if (context->nmea_fd < 0) {
        MBMLOGE("%s, error opening nmea channel", __FUNCTION__);
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

/* set position mode; stand alone, supl, etc and the desired fix interval*/
void gpsctrl_set_position_mode (int mode, int recurrence)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    context->pref_mode = mode;

    if (context->pref_mode == MODE_SUPL && recurrence == 1) {
        MBMLOGW("%s, interval can not be 1 when using SUPL. Changing it to 2", __FUNCTION__);
        context->interval = 2;
    } else
        context->interval = recurrence;

    EXIT;
}

/* delete aiding data */
int gpsctrl_delete_aiding_data(int clear_flag)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    if (!gpsctrl_get_device_is_ready()) {
        MBMLOGW("%s, device not ready. Not clearing data.", __FUNCTION__);
        return -1;
    }

    MBMLOGI("%s, force sleep for 10 seconds for gps to settle", __FUNCTION__);
    sleep(10);

    if (clear_flag == CLEAR_AIDING_DATA_ALL)
        err = at_send_command("AT*E2GPSCLM=0");
    else
        err = at_send_command("AT*E2GPSCLM=1");
    if (err < 0) {
        EXIT;
        return -1;
    }

    if (clear_flag == CLEAR_AIDING_DATA_ALL)
        err = at_send_command("AT*EEGPSEECLM=0");
    else
        err = at_send_command("AT*EEGPSEECLM=1");
    if (err < 0) {
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

 /* get nmea fd */
int gpsctrl_get_nmea_fd(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    return context->nmea_fd;
}

/* set supl related configuration, apn, supl server etc */
int gpsctrl_init_supl (int allow_uncert, int enable_ni)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    context->supl_config.allow_uncert = allow_uncert;
    context->supl_config.enable_ni = enable_ni;

    return supl_init();
}

/* init pgps related configuration */
int gpsctrl_init_pgps(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;
    EXIT;

    return 0;
}


/* set the supl server */
int gpsctrl_set_supl_server (char *server)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int ret;

    ENTER;

    free(context->supl_config.supl_server);
    context->supl_config.supl_server = NULL;

    ret = asprintf(&context->supl_config.supl_server,
                   "%s", server);
    if (ret < 0) {
        MBMLOGE("%s, Error allocating string", __FUNCTION__);
        EXIT;
        return -1;
    }

    if (gpsctrl_get_device_is_ready()) {
        EXIT;
        return supl_set_server();
    } else {
        EXIT;
        return 0;
    }
}

/* set the supl apn */
int gpsctrl_set_apn_info (char *apn, char *user, char *password, char* authtype)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int ret;

    ENTER;

    free(context->supl_config.apn);
    context->supl_config.apn = NULL;

    free(context->supl_config.username);
    context->supl_config.username = NULL;

    free(context->supl_config.password);
    context->supl_config.password = NULL;

    free(context->supl_config.authtype);
    context->supl_config.authtype = NULL;

    ret = asprintf(&context->supl_config.apn,
                   "%s", apn);
    if (ret < 0) {
        EXIT;
        return -1;
    }

    ret = asprintf(&context->supl_config.username,
                   "%s", user);
    if (ret < 0) {
        EXIT;
        return -1;
    }

    ret = asprintf(&context->supl_config.password,
                   "%s", password);
    if (ret < 0) {
        EXIT;
        return -1;
    }

    ret = asprintf(&context->supl_config.authtype,
                   "%s", authtype);
    if (ret < 0) {
        EXIT;
        return -1;
    }

    if (context->supl_initialized && gpsctrl_get_device_is_ready()) {
        EXIT;
        return supl_set_apn_info();
    } else {
        EXIT;
        return 0;
    }
}

/*  set callback for supl start failures */
void gpsctrl_set_supl_fail_callback (gpsctrl_supl_fail_callback supl_fail_callback)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;
    context->supl_fail_callback = supl_fail_callback;
    EXIT;
}

/*  set callback for supl ni requests */
void gpsctrl_set_supl_ni_callback (gpsctrl_supl_ni_callback supl_ni_callback)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;
    context->supl_ni_callback = supl_ni_callback;
    EXIT;
}

/* reply on supl network initiated requests */
int gpsctrl_supl_ni_reply (GpsCtrlSuplNiRequest *supl_ni_request, int allow)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    ENTER;
    EXIT;

    return supl_send_ni_reply(supl_ni_request->message_id, allow);
}

/* start the gps */
int gpsctrl_start(void)
{
    int err;
    int mode;
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    if (context->fallback) {
        mode = MODE_STAND_ALONE;
        context->fallback = 0;
    } else {
        /* if roaming and SUPL selected, go to stand alone */
        if (context->is_roaming && !context->data_roaming_allowed && (context->pref_mode == MODE_SUPL)) {
            mode = MODE_STAND_ALONE;
            MBMLOGW("%s, falling back to stand alone mode due to roaming data not allowed", __FUNCTION__);
        /* SUPL selected but 3G data not allowed, got to stand alone */
        } else if ((!context->data_enabled || !context->background_data_allowed) && (context->pref_mode == MODE_SUPL)) {
            mode = MODE_STAND_ALONE;
            MBMLOGW("%s, falling back to stand alone mode due to data/background data not allowed", __FUNCTION__);
        } else {
            mode = context->pref_mode;
        }
    }

    if (nmea_activate_port(context->nmea_fd) < 0) {
        MBMLOGE("%s, activate port failed", __FUNCTION__);
        return -1;
    }

    if (mode == MODE_SUPL) {
        err = at_send_command("AT*E2GPSSTAT=1");
        if (err < 0)
            return -1;
    }

    err = at_send_command("AT*E2GPSCTL=%d,%d", mode, context->interval);
    if (err < 0)
        return -1;

    return 0;
}

/* start the gps in the fallback mode */
int gpsctrl_start_fallback(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;
    context->fallback = 1;
    EXIT;

    return gpsctrl_start();
}

/* stop the gps */
int gpsctrl_stop(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    /* it's important that this is run before
     * e2gpsctl. Otherwise we can run into a
     * loop in onGpsStatusChange
     */
    err = at_send_command("AT*E2GPSSTAT=0");

    err = at_send_command("AT*E2GPSCTL=0");

    if (err < 0) {
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

static void close_devices(void)
{

    at_reader_close();

    nmea_close();

}

/* close at and nmea ports and kick off a cleanup */
int gpsctrl_cleanup(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    close_devices();

    EXIT;

    return 0;
}

/* set if data is enabled */
void gpsctrl_set_data_enabled (int enabled)
{
    get_gpsctrl_context()->data_enabled = enabled;
}

/* set if background data is allowed */
void gpsctrl_set_background_data_allowed (int allowed)
{
    get_gpsctrl_context()->background_data_allowed = allowed;
}

/* set if data roaming is allowed */
void gpsctrl_set_data_roaming_allowed (int allowed)
{
    get_gpsctrl_context()->data_roaming_allowed = allowed;
}

/* set if the device is roaming */
void gpsctrl_set_is_roaming(int roaming)
{
    get_gpsctrl_context()->is_roaming = roaming;
}

/* set if the device is connected to the internet */
void gpsctrl_set_is_connected (int connected)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    if (context->pref_mode == MODE_PGPS) {
        if (gpsctrl_get_device_is_ready())
            pgps_set_eedata(connected);
        else
            MBMLOGW("Not setting eedata since the device is not ready.");
    } else
        MBMLOGI("Not setting eedata since preferred mode is not PGPS");
}

/* set if the device is available and ready */
void gpsctrl_set_device_is_ready (int ready) {

    GpsCtrlContext *context = get_gpsctrl_context();

    MBMLOGV("%s, is ready: %d", __FUNCTION__, ready);
    context->is_ready = ready;

}

/* get if the device is available and ready */
int gpsctrl_get_device_is_ready(void)
{
    return get_gpsctrl_context()->is_ready;
}
