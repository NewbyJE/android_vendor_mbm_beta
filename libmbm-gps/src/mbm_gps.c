/* 
 * Copyright (C) Ericsson AB 2009-2010
 * Copyright 2006, The Android Open Source Project
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
 * Author: Torgny Johansson <torgny.johansson@ericsson.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <poll.h>
#include <termios.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <cutils/properties.h>
#include <hardware/gps.h>

#include "gpsctrl/gps_ctrl.h"
#include "gpsctrl/nmeachannel.h"
#include "gpsctrl/atchannel.h"
#include "nmea_reader.h"
#include "mbm_service_handler.h"
#include "version.h"

#define LOG_NDEBUG 0
#define LOG_TAG "libmbm-gps"
#include "log.h"

/* Just check this file */
#ifdef __GNUC__
#pragma GCC diagnostic warning "-pedantic"
#endif

#define MAX_AT_RESPONSE (8 * 1024)
#define TIMEOUT_POLL 200
#define EMRDY_POLL_INTERVAL 1000 /* needs to be a multiple of TIMEOUT_POLL */
#define TIMEOUT_EMRDY 15000 /* needs to be a multiple of EMRDY_POLL_INTERVAL */

#define SUPLNI_VERIFY_ALLOW 0
#define SUPLNI_VERIFY_DENY 1
#define SUPLNI_NOTIFY 2
#define SUPLNI_NOTIFY_DENIED 3

#define DEFAULT_NMEA_PORT "/dev/ttyACM2"
#define DEFAULT_CTRL_PORT "/dev/bus/usb/002/049"

#define PROP_SUPL "SUPL"
#define PROP_STANDALONE "STANDALONE"
#define PROP_PGPS "PGPS"

#define TIMEOUT_DEVICE_REMOVED 3

#define SINGLE_SHOT_INTERVAL 9999

#define CLEANUP_REQUESTED -10
#define DEVICE_NOT_READY -11

enum {
    CMD_STATUS_CB = 0,
    CMD_AGPS_STATUS_CB,
    CMD_NI_CB,
    CMD_AT_LOST,
    CMD_QUIT
};

typedef struct {
    int device_state;
    NmeaReader reader[1];
    GpsCtrlSuplConfig supl_config;
    gps_status_callback status_callback;
    gps_create_thread create_thread_callback;
    AGpsCallbacks agps_callbacks;
    AGpsStatus agps_status;

    int gps_started;
    int gps_should_start;
    int gps_initiated;
    int clear_flag;
    int cleanup_requested;

    int loglevel;

    int pref_mode;
    int allow_uncert;
    int enable_ni;

    GpsStatus gps_status;
    AGpsType type;

    int ril_connected;
    int ril_roaming;
    int ril_type;

    gps_ni_notify_callback ni_callback;
    GpsCtrlSuplNiRequest current_ni_request;
    GpsNiNotification notification;

    int cmd_fd[2];

    pthread_t main_thread;
    pthread_mutex_t mutex;
    pthread_mutex_t cleanup_mutex;
    pthread_cond_t cleanup_cond;
}GpsContext;

GpsContext global_context;

static void add_pending_command(char cmd);
static void nmea_received(char *line);
static int mbm_gps_start(void);
static void main_loop(void *arg);

static GpsContext* get_gps_context(void)
{
    return &global_context;
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       GPS-NI I N T E R F A C E                        *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

void mbm_gps_ni_request(GpsCtrlSuplNiRequest *ni_request)
{
    GpsContext *context = get_gps_context();

    ENTER;

    if (ni_request == NULL) {
        MBMLOGE("%s: ni_request == NULL", __FUNCTION__);
        return;
    }

    context->current_ni_request.message_id = ni_request->message_id;
    context->current_ni_request.message_type = ni_request->message_type;

    context->notification.notification_id = ni_request->message_id;
    context->notification.ni_type = GPS_NI_TYPE_UMTS_SUPL;
    context->notification.timeout = 30;
    context->notification.requestor_id_encoding = GPS_ENC_SUPL_UCS2;
    context->notification.text_encoding = GPS_ENC_SUPL_UCS2;

    MBMLOGD("%s: notification filled with id:%d, type:%d, id_encoding:%d, text_encoding:%d", __FUNCTION__, context->notification.notification_id, context->notification.ni_type, context->notification.requestor_id_encoding, context->notification.text_encoding);

    snprintf(context->notification.requestor_id, GPS_NI_SHORT_STRING_MAXLEN,
             "%s", ni_request->requestor_id_text);

    MBMLOGD("%s: requestor id filled with: %s", __FUNCTION__,
         context->notification.requestor_id);

    snprintf(context->notification.text, GPS_NI_LONG_STRING_MAXLEN,
             "%s", ni_request->client_name_text);

    MBMLOGD("%s: text filled with: %s", __FUNCTION__,
         context->notification.text);

    switch (ni_request->message_type) {
    case SUPLNI_VERIFY_ALLOW:
        context->notification.notify_flags =
            GPS_NI_NEED_VERIFY | GPS_NI_NEED_NOTIFY;
        context->notification.default_response = GPS_NI_RESPONSE_ACCEPT;
        MBMLOGI("%s: SUPL_VERIFY_ALLOW", __FUNCTION__);
        break;
    case SUPLNI_VERIFY_DENY:
        context->notification.notify_flags =
            GPS_NI_NEED_VERIFY | GPS_NI_NEED_NOTIFY;
        context->notification.default_response = GPS_NI_RESPONSE_DENY;
        MBMLOGI("%s: SUPL_VERIFY_DENY", __FUNCTION__);
        break;
    case SUPLNI_NOTIFY:
        context->notification.notify_flags = GPS_NI_NEED_NOTIFY;
        context->notification.default_response = GPS_NI_RESPONSE_ACCEPT;
        MBMLOGI("%s: SUPL_NOTIFY", __FUNCTION__);
        break;
    case SUPLNI_NOTIFY_DENIED:
        context->notification.notify_flags = GPS_NI_NEED_NOTIFY;
        context->notification.default_response = GPS_NI_RESPONSE_DENY;
        MBMLOGI("%s: SUPL_NOTIFY_DENIED", __FUNCTION__);
        break;
    default:
        MBMLOGI("%s: unknown request", __FUNCTION__);
        break;
    }

    add_pending_command(CMD_NI_CB);

    EXIT;

}

void mbm_gps_ni_init(GpsNiCallbacks * callbacks)
{
    GpsContext *context = get_gps_context();

    ENTER;

    if (callbacks == NULL) {
        MBMLOGE("%s: callbacks == NULL", __FUNCTION__);
        return;
    }

    context->ni_callback = callbacks->notify_cb;

    /* todo register for callback */

    EXIT;
}

void mbm_gps_ni_respond(int notif_id, GpsUserResponseType user_response)
{
    int allow;
    GpsContext *context = get_gps_context();

    MBMLOGV("%s: enter notif_id=%i", __FUNCTION__, notif_id);

    if (notif_id != context->current_ni_request.message_id) {
        MBMLOGE("Mismatch in notification ids. Ignoring the response.");
        return;
    }

    switch (user_response) {
    case GPS_NI_RESPONSE_ACCEPT:
        allow = 1;
        break;
    case GPS_NI_RESPONSE_DENY:
        allow = 0;
        break;
    case GPS_NI_RESPONSE_NORESP:
        if (context->current_ni_request.message_type == SUPLNI_VERIFY_DENY)
            allow = 0;
        else
            allow = 1;
        break;
    default:
        allow = 0;
        break;
    }

    gpsctrl_supl_ni_reply(&context->current_ni_request, allow);

    EXIT;
}

/**
 * Extended interface for Network-initiated (NI) support.
 */
static const GpsNiInterface mbmGpsNiInterface = {
    sizeof(GpsNiInterface),
    /** Registers the callbacks for HAL to use. */
    mbm_gps_ni_init,

    /** Sends a response to HAL. */
    mbm_gps_ni_respond,
};

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       AGPS I N T E R F A C E                          *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

void mbm_agps_init(AGpsCallbacks * callbacks)
{
    GpsContext *context = get_gps_context();

    ENTER;

    if (callbacks == NULL) {
        MBMLOGE("%s: callbacks == NULL", __FUNCTION__);
        return;
    }
    context->agps_callbacks = *callbacks;

    EXIT;
}

/* Function is called from java API when AGPS status
   is changed with GPS_REQUEST_AGPS_DATA_CONN
   DONT update AGPS status here!
*/
int mbm_agps_data_conn_open(const char *apn)
{
    GpsContext *context = get_gps_context();
    (void) apn;

    MBMLOGV("%s: enter, apn: %s", __FUNCTION__, apn);

    EXIT;
    return 0;
}

/* Function is called from java API when AGPS status
   is changed with GPS_RELEASE_AGPS_DATA_CONN
   DONT update AGPS status here!
*/
int mbm_agps_data_conn_closed(void)
{
    GpsContext *context = get_gps_context();

    ENTER;
    EXIT;
    return 0;
}

/* Function is called from java API when AGPS status
   is changed with GPS_RELEASE_AGPS_DATA_CONN
   DONT update AGPS status here!
*/
int mbm_agps_data_conn_failed(void)
{
    GpsContext *context = get_gps_context();

    ENTER;
    EXIT;
    return 0;
}

int mbm_agps_set_server(AGpsType type, const char *hostname, int port)
{
    GpsContext *context = get_gps_context();
    (void) type;
    (void) hostname;
    (void) port;

    MBMLOGV("%s: enter hostname: %s %d %s", __FUNCTION__, hostname, port,
         type == AGPS_TYPE_SUPL ? "SUPL" : "C2K");

    /* This is strange we get C2K if in flight mode?!?!? */

    /* store the server to be used in the supl config */
    gpsctrl_set_supl_server((char *)hostname);

    EXIT;
    return 0;
}

/** Extended interface for AGPS support. */
static const AGpsInterface mbmAGpsInterface = {
    sizeof(AGpsInterface),
    /**
     * Opens the AGPS interface and provides the callback routines
     * to the implemenation of this interface.
     */
    mbm_agps_init,
    /**
     * Notifies that a data connection is available and sets 
     * the name of the APN to be used for SUPL.
     */
    mbm_agps_data_conn_open,
    /**
     * Notifies that the AGPS data connection has been closed.
     */
    mbm_agps_data_conn_closed,
    /**
     * Notifies that a data connection is not available for AGPS. 
     */
    mbm_agps_data_conn_failed,
    /**
     * Sets the hostname and port for the AGPS server.
     */
    mbm_agps_set_server,
};


/*
  AGpsRil callbacks:
  agps_ril_request_set_id request_setid;
  agps_ril_request_ref_loc request_refloc;
  gps_create_thread create_thread_cb;

*/
void mbm_agpsril_init(AGpsRilCallbacks * callbacks)
{
    GpsContext *context = get_gps_context();

    ENTER;

    if (callbacks == NULL)
        MBMLOGE("%s, callback null", __FUNCTION__);

    EXIT;
}

static const char *agps_refid_str(int type)
{
    switch (type) {
    case AGPS_REF_LOCATION_TYPE_GSM_CELLID:
        return "AGPS_REF_LOCATION_TYPE_GSM_CELLID";
    case AGPS_REF_LOCATION_TYPE_UMTS_CELLID:
        return "AGPS_REF_LOCATION_TYPE_UMTS_CELLID";
    case AGPS_REG_LOCATION_TYPE_MAC:
        return "AGPS_REG_LOCATION_TYPE_MAC";
    default:
        return "UNKNOWN AGPS_REF_LOCATION";
    }
}

void
mbm_agpsril_set_ref_location(const AGpsRefLocation * agps_reflocation,
                             size_t sz_struct)
{
    GpsContext *context = get_gps_context();
    (void) agps_reflocation;
    (void) *agps_refid_str;

    ENTER;

    if (sz_struct != sizeof(AGpsRefLocation))
        MBMLOGE("AGpsRefLocation struct incorrect size");

    MBMLOGV("%s, AGpsRefLocation.type=%s size=%d", __FUNCTION__,
         agps_refid_str(agps_reflocation->type), sz_struct);

    EXIT;
}

static const char *agps_setid_str(int type)
{
    switch (type) {
    case AGPS_SETID_TYPE_NONE:
        return "AGPS_SETID_TYPE_NONE";
    case AGPS_SETID_TYPE_IMSI:
        return "AGPS_SETID_TYPE_IMSI";
    case AGPS_SETID_TYPE_MSISDN:
        return "AGPS_SETID_TYPE_MSISDN";
    default:
        return "UNKNOWN AGPS_SETID_TYPE";
    }
}

void mbm_agpsril_set_set_id(AGpsSetIDType type, const char *setid)
{
    GpsContext *context = get_gps_context();
    (void) type;
    (void) setid;
    (void) *agps_setid_str;

    MBMLOGV("%s: enter type=%s setid=%s", __FUNCTION__,
        agps_setid_str(type), setid);

    EXIT;
}

void mbm_agpsril_ni_message(uint8_t * msg, size_t len)
{
    GpsContext *context = get_gps_context();
    (void) msg;
    (void) len;

    MBMLOGV("%s: enter msg@%p len=%d", __FUNCTION__, msg, len);
    EXIT;
}


void
mbm_agpsril_update_network_state(int connected, int type, int roaming,
                                 const char *extra_info)
{
    GpsContext *context = get_gps_context();
    static int old_conn = -1;
    static int old_roam = -1;
    static int old_type = -1;

    ENTER;

    if ((old_conn != connected) || (old_roam != roaming) || (old_type != type)) {
        MBMLOGI("connected=%i type=%i roaming=%i, extra_info=%s",
            connected, type, roaming, extra_info);
        old_conn = connected;
        old_roam = roaming;
        old_type = type;
    }

    context->ril_connected = connected;
    context->ril_roaming = roaming;
    context->ril_type = type;

    if (context->gps_initiated) {
        gpsctrl_set_is_roaming(roaming);
        gpsctrl_set_is_connected(connected);
    }

    EXIT;
}

static void mbm_agpsril_update_network_availability (int available, const char *apn)
{
    GpsContext *context = get_gps_context();
    (void) available;
    (void) apn;

    MBMLOGV("%s: enter available=%d apn=%s", __FUNCTION__, available, apn);
    EXIT;
}

/* Not implemented just for debug*/
static const AGpsRilInterface mbmAGpsRilInterface = {
    sizeof(AGpsRilInterface),
    /**
     * Opens the AGPS interface and provides the callback routines
     * to the implemenation of this interface.
     */
    mbm_agpsril_init,
    /**
     * Sets the reference location.
     */
    mbm_agpsril_set_ref_location,
    /**
     * Sets the set ID.
     */
    mbm_agpsril_set_set_id,
    /**
     * Send network initiated message.
     */
    mbm_agpsril_ni_message,
    /**
     * Notify GPS of network status changes.
     * These parameters match values in the android.net.NetworkInfo class.
     */
    mbm_agpsril_update_network_state,

    /**
    * Notify GPS of network status changes.
    * These parameters match values in the android.net.NetworkInfo class.
    */
    mbm_agpsril_update_network_availability
};

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       M A I N  I N T E R F A C E                      *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/
static int jul_days(struct tm tm_day)
{
    return
        367 * (tm_day.tm_year + 1900) -
        floor(7 *
              (floor((tm_day.tm_mon + 10) / 12) +
               (tm_day.tm_year + 1900)) / 4) -
        floor(3 *
              (floor
               ((floor((tm_day.tm_mon + 10) / 12) +
                 (tm_day.tm_year + 1899)) / 100) + 1) / 4) +
        floor(275 * (tm_day.tm_mon + 1) / 9) + tm_day.tm_mday + 1721028 -
        2400000;
}

static void utc_to_gps(const time_t time, int *tow, int *week)
{
    GpsContext *context = get_gps_context();

    struct tm tm_utc;
    struct tm tm_gps;
    int day, days_cnt;

    if (tow == NULL || week == NULL) {
        MBMLOGE("%s: tow/week null", __FUNCTION__);
        return;
    }
    gmtime_r(&time, &tm_utc);
    tm_gps.tm_year = 80;
    tm_gps.tm_mon = 0;
    tm_gps.tm_mday = 6;

    days_cnt = jul_days(tm_utc) - jul_days(tm_gps);
    day = days_cnt % 7;
    *week = floor(days_cnt / 7);
    *tow = (day * 86400) + ((tm_utc.tm_hour * 60) +
                            tm_utc.tm_min) * 60 + tm_utc.tm_sec;
}

/* set the command to be handled from the main loop */
static void add_pending_command(char cmd)
{
    GpsContext *context = get_gps_context();
    write(context->cmd_fd[0], &cmd, 1);
}

static void nmea_received(char *line)
{
    GpsContext *context = get_gps_context();

    if (line == NULL) {
        MBMLOGE("%s: line null", __FUNCTION__);
        return;
    }

    MBMLOGD("%s: %s", __FUNCTION__, (char *) line);

    if ((strstr(line, "$GP") != NULL) && (strlen(line) > 3))
        nmea_reader_add(context->reader, (char *) line);
}

void supl_fail(void)
{
    GpsContext *context = get_gps_context();

    ENTER;

    pthread_mutex_lock(&context->mutex);

    if (context->gps_started) {
        MBMLOGI("%s, starting gps in fallback mode", __FUNCTION__);
        gpsctrl_start_fallback();
    }

    pthread_mutex_unlock(&context->mutex);

    EXIT;
}

static int epoll_register(int epoll_fd, int fd)
{
    struct epoll_event ev;
    int ret, flags;

    /* important: make the fd non-blocking */
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;
    do {
        ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    }
    while (ret < 0 && errno == EINTR);

    return ret;
}

static int epoll_deregister(int epoll_fd, int fd)
{
    int ret;

    do {
        ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    }
    while (ret < 0 && errno == EINTR);

    return ret;
}

static void onATReaderClosed(void)
{
    add_pending_command(CMD_AT_LOST);
}

static int safe_read(int fd, char *buf, int count)
{
    return read(fd, buf, count);
}

/**
 * Wait until the module reports EMRDY
 */
static int wait_for_emrdy (int fd, int timeout)
{
    struct pollfd fds[1];
    char start[MAX_AT_RESPONSE];
    int n, len, time;
    GpsContext *context = get_gps_context();
    char *at_emrdy = "AT*EMRDY?\r\n";

    MBMLOGI("Waiting for EMRDY...");

    fds[0].fd = fd;
    fds[0].events = POLLIN | POLLERR | POLLHUP;

    time = TIMEOUT_POLL;
    while (1) {
        /* dump EMRDY? on the line every EMRDY_POLL_INTERVAL to poll if no unsolicited is received
         * don't care if it doesn't work
         */
        if ((time % EMRDY_POLL_INTERVAL) == 0)
            write(fd, at_emrdy, strlen(at_emrdy));

        n = poll(fds, 1, TIMEOUT_POLL);

        if ((n < 0) || (fds[0].revents & (POLLERR | POLLHUP))) {
            MBMLOGE("%s: Poll error", __FUNCTION__);
            return -1;
        } else if ((n > 0) && (fds[0].revents & POLLIN)) {
            MBMLOGD("%s, got an event", __FUNCTION__);
            memset(start, 0, MAX_AT_RESPONSE);
            len = safe_read(fd, start, MAX_AT_RESPONSE - 1);

            if (start == NULL)
                MBMLOGW("Oops, empty string");
            else if (strstr(start, "EMRDY: 0") != NULL)
                MBMLOGW("Oops, not EMRDY yet");
            else if (strstr(start, "EMRDY: 1") != NULL)
                break;
            else
                MBMLOGW("Oops, this was not EMRDY [%s]",start);

            /* sleep a while to not increase the time variable to quickly */
            usleep(TIMEOUT_POLL * 1000);
        }

        if (context->cleanup_requested) {
            MBMLOGW("aborting because of cleanup requested");
            return CLEANUP_REQUESTED;
        } else if (time >= timeout) {
            MBMLOGW("EMRDY timeout, go ahead anyway(might work)...");
            return 0;
        }

        time += TIMEOUT_POLL;
    }

    if (context->cleanup_requested) {
        MBMLOGI("%s: Got EMRDY but aborting because of cleanup requested", __FUNCTION__);
        return CLEANUP_REQUESTED;
    } else {
        MBMLOGI("Got EMRDY");
        return 0;
    }
}

/* open ctrl device */
int open_at_channel(void)
{
    int at_fd, ret;
    char *at_dev = gpsctrl_get_at_device();
    GpsContext *context = get_gps_context();

    MBMLOGI("Trying to open (%s)", at_dev);
    while (1) {
        if (context->cleanup_requested) {
            MBMLOGW("%s, aborting because of cleanup", __FUNCTION__);
            return CLEANUP_REQUESTED;
        }
        at_fd = open(at_dev, O_RDWR | O_NONBLOCK);
        if (at_fd < 0) {
            MBMLOGD("%s, at_fd < 0, dev:%s, error:%s", __FUNCTION__, at_dev, strerror(errno));
            MBMLOGD("Trying again");
            sleep(1);
        } else
            break;
    }

    if (strstr(at_dev, "/dev/ttyA")) {
        struct termios ios;
        MBMLOGD("%s, flushing device", __FUNCTION__);
        /* Clear the struct and then call cfmakeraw*/
        tcflush(at_fd, TCIOFLUSH);
        tcgetattr(at_fd, &ios);
        memset(&ios, 0, sizeof(struct termios));
        cfmakeraw(&ios);
        /* OK now we are in a known state, set what we want*/
        ios.c_cflag |= CRTSCTS;
        /* ios.c_cc[VMIN]  = 0; */
        /* ios.c_cc[VTIME] = 1; */
        ios.c_cflag |= CS8;
        tcsetattr(at_fd, TCSANOW, &ios);
        tcflush(at_fd, TCIOFLUSH);
        tcsetattr(at_fd, TCSANOW, &ios);
        tcflush(at_fd, TCIOFLUSH);
        tcflush(at_fd, TCIOFLUSH);
        cfsetospeed(&ios, B115200);
        cfsetispeed(&ios, B115200);
        tcsetattr(at_fd, TCSANOW, &ios);

        fcntl(at_fd, F_SETFL, 0);
    }

    ret = wait_for_emrdy(at_fd, TIMEOUT_EMRDY);

    if (ret == CLEANUP_REQUESTED)
        return CLEANUP_REQUESTED;
    else if (ret == -1) {
        close(at_fd);
        return -1;
    } else
        return at_fd;
}


/* this loop is needed to be able to run callbacks in
 * the correct thread, created with the create_thread callback
 */
static void main_loop(void *arg)
{
    GpsCtrlContext *ctrlcontext = get_gpsctrl_context();
    GpsContext *context = get_gps_context();
    int cmd_fd = context->cmd_fd[1];
    int epoll_fd = -1;
    char cmd = 255;
    char nmea[MAX_NMEA_LENGTH];
    int ret;
    int at_fd;
    int nmea_fd = -1;
    int at_channel_lost;
    int nmea_channel_lost;
    int i, at_dev, nmea_dev;
    struct stat sb;
    (void) arg;

    ENTER;

    while (epoll_fd < 0) {
        if (context->cleanup_requested)
            goto exit;
        epoll_fd = epoll_create(2);
        if (epoll_fd < 0) {
            MBMLOGE("epoll_create() unexpected error: %s. Retrying...",
                strerror(errno));
            sleep(1);
        }
    }

    while (1) {
        at_channel_lost = 0;
        nmea_channel_lost = 0;
        at_fd = open_at_channel();
        if (at_fd == CLEANUP_REQUESTED)
            goto exit;
        else if (at_fd < 0) {
            MBMLOGE("Error opening at channel. Retrying...");
            sleep(1);
            goto retry;
        }

        ret = gpsctrl_open(at_fd, onATReaderClosed);
        if (ret < 0) {
            MBMLOGE("Error opening gpsctrl device. Retrying...");
            sleep(1);
            goto retry;
        }

        context->gps_status.status = GPS_STATUS_ENGINE_ON;
        add_pending_command(CMD_STATUS_CB);

        gpsctrl_set_device_is_ready(1);

        gpsctrl_set_position_mode(context->pref_mode, 1);

        ret = gpsctrl_init_supl(context->allow_uncert, context->enable_ni);
        if (ret < 0)
            MBMLOGE("Error initing SUPL");

        ret = gpsctrl_init_pgps();
        if (ret < 0)
            MBMLOGE("Error initing PGPS");

        pthread_mutex_lock(&context->mutex);
        if (context->gps_should_start || context->gps_started) {
            MBMLOGI("%s, restoring GPS state to started", __FUNCTION__);
            mbm_gps_start();
        }
        pthread_mutex_unlock(&context->mutex);

        nmea_fd = gpsctrl_get_nmea_fd();

        epoll_register(epoll_fd, cmd_fd);
        epoll_register(epoll_fd, nmea_fd);

        while (!nmea_channel_lost) {
            struct epoll_event event[2];
            int nevents;
            nevents = epoll_wait(epoll_fd, event, 2, -1);
            if (nevents < 0 && errno != EINTR) {
                MBMLOGE("epoll_wait() unexpected error for fd %d: %s. Retrying...",
                    epoll_fd, strerror(errno));
                if (context->cleanup_requested)
                    goto exit;
                sleep(1);
                continue;
            }
            for (i=0; i<nevents; i++) {
                if (((event[i].events & (EPOLLERR | EPOLLHUP)) != 0)) {
                    MBMLOGE("EPOLLERR or EPOLLHUP after epoll_wait(%x)!", event[i].events);
                    if (event[i].data.fd == nmea_fd) {
                        MBMLOGW("NMEA channel lost. Will try to recover.");
                        nmea_channel_lost = 1;
                    }
                }

                if ((event[i].events & EPOLLIN) != 0) {
                    int fd = event[i].data.fd;

                    if (fd == cmd_fd) {
                        do {
                            ret = read(fd, &cmd, 1);
                        }
                        while (ret < 0 && errno == EINTR);

                        MBMLOGD("%s, command %d", __FUNCTION__, (int) cmd);

                        switch (cmd) {
                        case CMD_STATUS_CB:
                            context->status_callback(&context->gps_status);
                            break;
                        case CMD_AGPS_STATUS_CB:
                            context->agps_callbacks.status_cb
                                                (&context->agps_status);
                            break;
                        case CMD_NI_CB:
                            MBMLOGV("%s: CMD_NI_CB", __FUNCTION__);
                            context->ni_callback(&context->notification);
                            MBMLOGV("%s: CMD_NI_CB sent", __FUNCTION__);
                            break;
                        case CMD_AT_LOST:
                            MBMLOGW("%s: CMD_AT_LOST, will try to recover.",
                                                        __FUNCTION__);
                            at_reader_close();
                            gpsctrl_set_device_is_ready(0);
                            at_channel_lost = 1;
                            MBMLOGW("AT channel lost. Will try to recover");
                            break;
                        case CMD_QUIT:
                            goto exit;
                            break;
                        default:
                            break;
                        }
                    } else if (fd == nmea_fd) {
                        nmea_read(nmea_fd, nmea);
                        nmea_received(nmea);
                    } else
                        MBMLOGE("epoll_wait() returned unkown fd %d ?", fd);
                } else
                    MBMLOGE("epoll_wait() returned unkown event %x", event[i].events);
            }
        }

        gpsctrl_set_device_is_ready(0);
        epoll_deregister(epoll_fd, cmd_fd);
        epoll_deregister(epoll_fd, nmea_fd);

retry:
        gpsctrl_cleanup();

        if (context->gps_status.status != GPS_STATUS_ENGINE_OFF) {
            context->gps_status.status = GPS_STATUS_ENGINE_OFF;
            context->status_callback(&context->gps_status);
        }

        /* Make sure devices is removed before trying to reopen
           otherwise we might end up in a race condition when
           devices is being removed from filesystem */
        i = TIMEOUT_DEVICE_REMOVED;
        do {
            at_dev = (0 == stat(ctrlcontext->at_dev, &sb));
            nmea_dev = (0 == stat(ctrlcontext->nmea_dev, &sb));
            if (at_dev || nmea_dev) {
                MBMLOGD("Waiting for %s%s%s to be removed (%d)...",
                    at_dev ? ctrlcontext->at_dev : "",
                    at_dev && nmea_dev ? " and ": "",
                    nmea_dev ? ctrlcontext->nmea_dev : "", i);
                sleep(1);
                i--;
            }
        } while ((at_dev || nmea_dev) && i);
    }

exit:
    gpsctrl_set_device_is_ready(0);
    gpsctrl_cleanup();

    if (context->gps_status.status != GPS_STATUS_ENGINE_OFF) {
        context->gps_status.status = GPS_STATUS_ENGINE_OFF;
        context->status_callback(&context->gps_status);
    }

    pthread_mutex_lock(&context->cleanup_mutex);

    if (epoll_fd >= 0) {
        epoll_deregister(epoll_fd, cmd_fd);
        epoll_deregister(epoll_fd, nmea_fd);
        close(epoll_fd);
    }

    pthread_cond_signal(&context->cleanup_cond);
    pthread_mutex_unlock(&context->cleanup_mutex);

    MBMLOGI("%s, terminated", __FUNCTION__);

    EXIT;
}

static void get_properties(char *nmea_dev, char *at_dev)
{
    GpsContext *context = get_gps_context();
    char prop[PROPERTY_VALUE_MAX];
    int len;

    len = property_get("mbm.gps.config.loglevel", prop, "");
    if (strstr(prop, "ERROR")) {
        context->loglevel = ANDROID_LOG_ERROR;
        MBMLOGI("Setting Loglevel to ERROR");
    } else if (strstr(prop, "SILENT")) {
        context->loglevel = ANDROID_LOG_SILENT;
        MBMLOGI("Setting Loglevel to ERROR");
    } else if (strstr(prop, "WARN")) {
        context->loglevel = ANDROID_LOG_WARN;
        MBMLOGI("Setting Loglevel to WARN");
    } else if (strstr(prop, "INFO")) {
        context->loglevel = ANDROID_LOG_INFO;
        MBMLOGI("Setting Loglevel to INFO");
    } else if (strstr(prop, "DEBUG")) {
        context->loglevel = ANDROID_LOG_DEBUG;
        MBMLOGI("Setting Loglevel to DEBUG");
    } else if (strstr(prop, "VERBOSE")) {
        context->loglevel = ANDROID_LOG_VERBOSE;
        MBMLOGI("Setting Loglevel to VERBOSE");
    } else {
        context->loglevel = ANDROID_LOG_INFO;
        MBMLOGI("Setting Loglevel to INFO");
    }

    len = property_get("mbm.gps.config.gps_pref_mode", prop, "");
    if (strstr(prop, PROP_SUPL)) {
        MBMLOGI("Setting preferred AGPS mode to SUPL");
        context->pref_mode = MODE_SUPL;
    } else if (strstr(prop, PROP_PGPS)) {
        MBMLOGI("Setting preferred AGPS mode to PGPS");
        context->pref_mode = MODE_PGPS;
    } else if (strstr(prop, PROP_STANDALONE)) {
        MBMLOGI("Setting preferred AGPS mode to STANDALONE");
        context->pref_mode = MODE_STAND_ALONE;
    } else {
        MBMLOGI("Setting preferred AGPS mode to PGPS (prop %s)",
             prop);
        context->pref_mode = MODE_PGPS;
    }

    len = property_get("mbm.gps.config.supl.enable_ni", prop, "no");
    if (strstr(prop, "yes")) {
        MBMLOGI("Enabling network initiated requests");
        context->enable_ni = 1;
    } else {
        MBMLOGI("Disabling network initiated requests");
        context->enable_ni = 0;
    }

    len = property_get("mbm.gps.config.supl.uncert", prop, "no");
    if (strstr(prop, "yes")) {
        MBMLOGI("Allowing unknown certificates");
        context->allow_uncert = 1;
    } else {
        MBMLOGI("Not allowing unknown certificates");
        context->allow_uncert = 0;
    }

    len = property_get("mbm.gps.config.gps_ctrl", at_dev, "");
    if (len)
        MBMLOGI("Using gps ctrl device: %s", at_dev);
    else {
        MBMLOGI("No gps ctrl device set, using the default instead.");
        snprintf(at_dev, PROPERTY_VALUE_MAX, "%s", DEFAULT_CTRL_PORT);
    }

    len = property_get("mbm.gps.config.gps_nmea", nmea_dev, "");
    if (len)
        MBMLOGI("Using gps nmea device: %s", nmea_dev);
    else {
        MBMLOGI("No gps nmea device set, using the default instead.");
        snprintf(nmea_dev, PROPERTY_VALUE_MAX, "%s", DEFAULT_NMEA_PORT);
    }
}

static int mbm_gps_init(GpsCallbacks * callbacks)
{
    int ret;
    GpsContext *context = get_gps_context();
    pthread_mutexattr_t mutex_attr;
    char nmea_dev[PROPERTY_VALUE_MAX];
    char at_dev[PROPERTY_VALUE_MAX];

    ALOGI("MBM-GPS version: %s", MBM_GPS_VERSION);

    context->gps_started = 0;
    context->gps_initiated = 0;
    context->clear_flag = CLEAR_AIDING_DATA_NONE;
    context->cleanup_requested = 0;
    context->gps_should_start = 0;

    get_properties(nmea_dev, at_dev);

    if (callbacks == NULL) {
        MBMLOGE("%s, callbacks null", __FUNCTION__);
        return -1;
    }

    if (callbacks->set_capabilities_cb) {
        callbacks->set_capabilities_cb(GPS_CAPABILITY_SCHEDULING |
                                       GPS_CAPABILITY_MSB |
                                       GPS_CAPABILITY_MSA |
                                       GPS_CAPABILITY_SINGLE_SHOT);
    } else {
        MBMLOGE("%s capabilities_cb is null", __FUNCTION__);
    }

    context->agps_status.size = sizeof(AGpsStatus);

    nmea_reader_init(context->reader, context->loglevel);

    context->cmd_fd[0] = -1;
    context->cmd_fd[1] = -1;
    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, context->cmd_fd) < 0) {
        MBMLOGE("could not create thread control socket pair: %s",
             strerror(errno));
        return -1;
    }

    context->status_callback = callbacks->status_cb;

    context->create_thread_callback = callbacks->create_thread_cb;

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&context->mutex, &mutex_attr);

    pthread_mutex_init(&context->cleanup_mutex, NULL);
    pthread_cond_init(&context->cleanup_cond, NULL);

    /* initialize mbm service handler */
    if (service_handler_init(context->loglevel) < 0)
        MBMLOGE("%s, error initializing service handler", __FUNCTION__);

    ret = gpsctrl_init(context->loglevel);
    if (ret < 0)
        MBMLOGE("Error initing gpsctrl lib");

    gpsctrl_set_devices(at_dev, nmea_dev);
    gpsctrl_set_supl_ni_callback(mbm_gps_ni_request);
    gpsctrl_set_supl_fail_callback(supl_fail);

    if (ret < 0) {
        MBMLOGE("Error setting devices");
        return -1;
    }

    /* main thread must be started prior to setting nmea_reader callbacks */
    context->main_thread = context->create_thread_callback("mbm_main_thread",
                                                     main_loop, NULL);

    nmea_reader_set_callbacks(context->reader, callbacks);

    context->gps_initiated = 1;

    MBMLOGV("%s: exit", __FUNCTION__);
    return 0;
}

static void mbm_gps_cleanup(void)
{
    GpsContext *context = get_gps_context();

    ENTER;

    context->gps_initiated = 0;

    MBMLOGD("%s, waiting for main thread to exit", __FUNCTION__);
    pthread_mutex_lock(&context->cleanup_mutex);

    add_pending_command(CMD_QUIT);
    context->cleanup_requested = 1;

    pthread_cond_wait(&context->cleanup_cond, &context->cleanup_mutex);

    MBMLOGD("%s, stopping service handler", __FUNCTION__);
    service_handler_stop();

    MBMLOGD("%s, cleanup gps ctrl", __FUNCTION__);
    gpsctrl_cleanup();

    pthread_mutex_unlock(&context->cleanup_mutex);

    pthread_mutex_destroy(&context->mutex);
    pthread_mutex_destroy(&context->cleanup_mutex);
    pthread_cond_destroy(&context->cleanup_cond);

    EXIT;
}


static int mbm_gps_start(void)
{
    int err;
    GpsContext *context = get_gps_context();

    ENTER;

    pthread_mutex_lock(&context->mutex);

    if (!gpsctrl_get_device_is_ready()) {
        MBMLOGW("%s, device is not ready. Deferring start of gps.", __FUNCTION__);
        context->gps_should_start = 1;
        pthread_mutex_unlock(&context->mutex);
        EXIT;
        return 0;
    }

    err = gpsctrl_start();
    if (err < 0) {
        MBMLOGE("Error starting gps");
        pthread_mutex_unlock(&context->mutex);
        EXIT;
        return -1;
    }

    context->gps_started = 1;

    context->gps_status.status = GPS_STATUS_SESSION_BEGIN;
    add_pending_command(CMD_STATUS_CB);

    pthread_mutex_unlock(&context->mutex);
    EXIT;
    return 0;
}

static int mbm_gps_stop(void)
{
    int err;
    GpsContext *context = get_gps_context();

    ENTER;

    pthread_mutex_lock(&context->mutex);

    if (!gpsctrl_get_device_is_ready()) {
        MBMLOGW("%s, device not ready.", __FUNCTION__);
    } else {
        err = gpsctrl_stop();
        if (err < 0)
            MBMLOGE("Error stopping gps");
    }

    context->gps_started = 0;
    context->gps_should_start = 0;

    context->gps_status.status = GPS_STATUS_SESSION_END;
    add_pending_command(CMD_STATUS_CB);

    if (context->clear_flag > CLEAR_AIDING_DATA_NONE) {
        MBMLOGW("%s: Executing deferred operation of deleting aiding data", __FUNCTION__);
        gpsctrl_delete_aiding_data(context->clear_flag);
        context->clear_flag = CLEAR_AIDING_DATA_NONE;
    }

    pthread_mutex_unlock(&context->mutex);

    EXIT;
    return 0;
}


/* Not implemented just debug*/
static int
mbm_gps_inject_time(GpsUtcTime time, int64_t timeReference,
                    int uncertainty)
{
    GpsContext *context = get_gps_context();
    char buff[100];
    int tow, week;
    time_t s_time;
    (void) timeReference;
    (void) uncertainty;

    ENTER;

    s_time = time / 1000;
    utc_to_gps(s_time, &tow, &week);
    memset(buff, 0, 100);
    snprintf(buff, 98, "AT*E2GPSTIME=%d,%d\r", tow, week);
    MBMLOGD("%s: %s", __FUNCTION__, buff);

    EXIT;
    return 0;
}

/** Injects current location from another location provider
 *  (typically cell ID).
 *  latitude and longitude are measured in degrees
 *  expected accuracy is measured in meters
 *  Not implemented just debug
 */
static int
mbm_gps_inject_location(double latitude, double longitude, float accuracy)
{
    GpsContext *context = get_gps_context();

    ENTER;

    MBMLOGD("%s: lat = %f , lon = %f , acc = %f", __FUNCTION__, latitude,
         longitude, accuracy);

    EXIT;

    return 0;
}

static void mbm_gps_delete_aiding_data(GpsAidingData flags)
{
    GpsContext *context = get_gps_context();
    int clear_flag = CLEAR_AIDING_DATA_NONE;

    ENTER;

    /* Support exist for EPHEMERIS and ALMANAC only
     * where only EPHEMERIS can be cleared by it self.
     */
    if ((GPS_DELETE_EPHEMERIS | GPS_DELETE_ALMANAC) ==
        ((GPS_DELETE_EPHEMERIS | GPS_DELETE_ALMANAC) & flags))
        clear_flag = CLEAR_AIDING_DATA_ALL;
    else if (GPS_DELETE_EPHEMERIS == (GPS_DELETE_EPHEMERIS & flags))
        clear_flag = CLEAR_AIDING_DATA_EPH_ONLY;
    else
        MBMLOGW("Parameters not supported for deleting");

    if (!context->gps_started)
        gpsctrl_delete_aiding_data(clear_flag);
    else {
        MBMLOGW("%s, Deferring operation of deleting aiding data until gps is stopped", __FUNCTION__);
        context->clear_flag = clear_flag;
    }

    EXIT;
}

static char *get_mode_name(int mode)
{
    switch (mode) {
    case GPS_POSITION_MODE_STANDALONE:
        return "GPS_POSITION_MODE_STANDALONE";
    case GPS_POSITION_MODE_MS_BASED:
        return "GPS_POSITION_MODE_MS_BASED";
    case GPS_POSITION_MODE_MS_ASSISTED:
        return "GPS_POSITION_MODE_MS_ASSISTED";
    default:
        return "UNKNOWN MODE";
    }
}

static int
mbm_gps_set_position_mode(GpsPositionMode mode,
                          GpsPositionRecurrence recurrence,
                          uint32_t min_interval,
                          uint32_t preferred_accuracy,
                          uint32_t preferred_time)
{
    GpsContext *context = get_gps_context();
    int new_mode = MODE_STAND_ALONE;
    int interval;
    (void) preferred_accuracy;
    (void) preferred_time;
    (void) *get_mode_name;

    MBMLOGV("%s:enter  %s min_interval = %d recurrence=%d pref_time=%d",
         __FUNCTION__, get_mode_name(mode), min_interval, recurrence,
         preferred_time);

    switch (mode) {
    case GPS_POSITION_MODE_MS_ASSISTED:
    case GPS_POSITION_MODE_MS_BASED:
        if (context->pref_mode == MODE_SUPL)
            new_mode = MODE_SUPL;
        else if (context->pref_mode == MODE_STAND_ALONE)
            /* override Androids choice since stand alone
             * has been explicitly selected via a property
             */
            new_mode = MODE_STAND_ALONE;
        else
            new_mode = MODE_PGPS;
        break;
    case GPS_POSITION_MODE_STANDALONE:
        new_mode = MODE_STAND_ALONE;
        break;
    default:
        break;
    }

    if (recurrence == GPS_POSITION_RECURRENCE_SINGLE || min_interval == SINGLE_SHOT_INTERVAL)
        interval = 0;
    else if (min_interval < 1000)
        interval = 1;
    else
        interval = min_interval / 1000;

    gpsctrl_set_position_mode(new_mode, interval);

    EXIT;
    return 0;
}

static const void *mbm_gps_get_extension(const char *name)
{
    GpsContext *context = get_gps_context();

    MBMLOGV("%s: enter name=%s", __FUNCTION__, name);

    if (name == NULL)
        return NULL;

    MBMLOGD("%s, querying %s", __FUNCTION__, name);

    if (!strncmp(name, AGPS_INTERFACE, 10)) {
        MBMLOGD("%s: exit &mbmAGpsInterface", __FUNCTION__);
        return &mbmAGpsInterface;
    }

    if (!strncmp(name, AGPS_RIL_INTERFACE, 10)) {
        MBMLOGD("%s: exit &mbmAGpsRilInterface", __FUNCTION__);
        return &mbmAGpsRilInterface;
    }

    if (!strncmp(name, GPS_NI_INTERFACE, 10)) {
        MBMLOGD("%s: exit &mbmGpsNiInterface", __FUNCTION__);
        return &mbmGpsNiInterface;
    }

    EXIT;
    return NULL;
}

/** Represents the standard GPS interface. */
static const GpsInterface mbmGpsInterface = {
    sizeof(GpsInterface),
        /**
	 * Opens the interface and provides the callback routines
	 * to the implemenation of this interface.
	 */
    mbm_gps_init,

        /** Starts navigating. */
    mbm_gps_start,

        /** Stops navigating. */
    mbm_gps_stop,

        /** Closes the interface. */
    mbm_gps_cleanup,

        /** Injects the current time. */
    mbm_gps_inject_time,

        /** Injects current location from another location provider
	 *  (typically cell ID).
	 *  latitude and longitude are measured in degrees
	 *  expected accuracy is measured in meters
	 */
    mbm_gps_inject_location,

        /**
	 * Specifies that the next call to start will not use the
	 * information defined in the flags. GPS_DELETE_ALL is passed for
	 * a cold start.
	 */

    mbm_gps_delete_aiding_data,
        /**
	 * fix_frequency represents the time between fixes in seconds.
	 * Set fix_frequency to zero for a single-shot fix.
	 */
    mbm_gps_set_position_mode,

        /** Get a pointer to extension information. */
    mbm_gps_get_extension,
};

const GpsInterface *gps_get_hardware_interface(void)
{
    GpsContext *context = get_gps_context();

    MBMLOGD("gps_get_hardware_interface");
    return &mbmGpsInterface;
}

/* This is for Gingerbread */
const GpsInterface *mbm_get_gps_interface(struct gps_device_t *dev)
{
    GpsContext *context = get_gps_context();
    (void) dev;

    MBMLOGD("mbm_gps_get_hardware_interface");
    return &mbmGpsInterface;
}

static int
mbm_open_gps(const struct hw_module_t *module,
             char const *name, struct hw_device_t **device)
{
    GpsContext *context = get_gps_context();
    struct gps_device_t *dev;
    (void) name;

    ENTER;

    if (module == NULL) {
        MBMLOGE("%s: module null", __FUNCTION__);
        return -1;
    }

    dev = malloc(sizeof(struct gps_device_t));
    if (!dev) {
        MBMLOGE("%s: malloc fail", __FUNCTION__);
        return -1;
    }
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *) module;
    dev->get_gps_interface = mbm_get_gps_interface;

    *device = (struct hw_device_t *) dev;

    EXIT;
    return 0;
}

static struct hw_module_methods_t mbm_gps_module_methods = {
    .open = mbm_open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "MBM GPS",
    .author = "Ericsson",
    .methods = &mbm_gps_module_methods,
};
