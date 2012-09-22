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

#include <unistd.h>

#include "atchannel.h"
#include "at_tok.h"
#include "gps_ctrl.h"
#include "supl.h"

#define LOG_NDEBUG 0
#define LOG_TAG "libgpsctrl-supl"
#include "../log.h"

void* onUnknownCertificate (void *data)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    char *line = (char *) data;
    int err;
    int ignore;
    int msg_id;
    int app_id;

    ENTER;

    err = at_tok_start(&line);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &ignore);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &msg_id);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &app_id);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        EXIT;
        return NULL;
    }

    MBMLOGD("%s, msg_id=%d, app_id=%d", __FUNCTION__, msg_id, app_id);

    err = at_send_command("AT*E2CERTUNREPLY=%d,%d", msg_id,
                   context->supl_config.allow_uncert);
    if (err < 0) {
        MBMLOGE("%s error sending at command", __FUNCTION__);
        EXIT;
        return NULL;
    }
    EXIT;
    return NULL;
}

void* onSuplNiRequest(void *data)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    char *line = (char *) data;
    int err;
    int ignore;
    GpsCtrlSuplNiRequest ni_request;

    ENTER;

    err = at_tok_start(&line);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &ignore);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &ni_request.message_id);
    if (err < 0) {
        MBMLOGE("%s error parsing data message id", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &ni_request.message_type);
    if (err < 0) {
        MBMLOGE("%s error parsing data message type", __FUNCTION__);
        EXIT;
        return NULL;
    }

    err = at_tok_nextint(&line, &ni_request.requestor_id_type);
    if (err < 0) {
        MBMLOGW("%s couldn't parse data requestor id type", __FUNCTION__);
        ni_request.requestor_id_type = -1;
    }

    err = at_tok_nextstr(&line, &ni_request.requestor_id_text);
    if (err < 0) {
        MBMLOGW("%s couldn't parse data requestor id text", __FUNCTION__);
        ni_request.requestor_id_text = "";
    }

    err = at_tok_nextint(&line, &ni_request.client_name_type);
    if (err < 0) {
        MBMLOGW("%s couldn't parse data client name type", __FUNCTION__);
        ni_request.client_name_type = -1;
    }

    err = at_tok_nextstr(&line, &ni_request.client_name_text);
    if (err < 0) {
        MBMLOGW("%s couldn't parse data client name text", __FUNCTION__);
        ni_request.client_name_text = "";
    }

    context->supl_ni_callback(&ni_request);

    EXIT;
    return NULL;
}

int supl_send_ni_reply (int msg_id, int allow)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    MBMLOGV("%s, msg_id=%d, allow=%d", __FUNCTION__, msg_id, allow);

    err = at_send_command("AT*E2GPSSUPLNIREPLY=%d,%d", msg_id,
                   allow);
    if (err < 0) {
        MBMLOGE("%s error sending at command", __FUNCTION__);
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

/**
 * Returns a pointer to allocated memory filled with AT command
 * UCS-2 formatted string corresponding to the input string.
 * Note: Caller need to take care of freeing the
 *  allocated memory by calling free( ) when the
 *  created string is no longer used.
 */
static char *ucs2StringCreate(const char *iString)
{
    int slen = 0;
    int idx = 0;
    char *ucs2String = NULL;

    /* In case of NULL input, create an empty string as output */
    if (NULL == iString)
        slen = 0;
    else
        slen = strlen(iString);

    ucs2String = (char *)malloc(sizeof(char)*(slen*4+1));
    for (idx = 0; idx < slen; idx++)
        sprintf(&ucs2String[idx*4], "%04x", iString[idx]);
    ucs2String[idx*4] = '\0';
    return ucs2String;
}

static int setCharEncoding(const char *enc)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    err = at_send_command("AT+CSCS=\"%s\"", enc);

    if (err < 0) {
        MBMLOGE("%s failed to set AT+CSCS=%s", __FUNCTION__, enc);
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

static char *getCharEncoding(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    ATResponse *p_response = NULL;
    int err;
    char *line, *chSet, *ret;

    ENTER;

    err = at_send_command_singleline("AT+CSCS?", "+CSCS:", &p_response);

    if (err < 0) {
        ALOGE("%s failed to read AT+CSCS?", __FUNCTION__);
        EXIT;
        return NULL;
    }

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) {
        EXIT;
        return NULL;
    }
    err = at_tok_nextstr(&line, &chSet);
    if (err < 0) {
        EXIT;
        return NULL;
    }

    /* If not any of the listed below, assume UCS-2 */
    if (!strcmp(chSet, "GSM") || !strcmp(chSet, "IRA") ||
        !strncmp(chSet, "8859",4) || !strcmp(chSet, "UTF-8"))
        ret = strdup(chSet);
    else
        ret = strdup("UCS-2");

    at_response_free(p_response);
    EXIT;
    return ret;
}

static int networkAuth(const char *authentication, const char *user, const char *pass, int index)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    char *atAuth = NULL, *atUser = NULL, *atPass = NULL;
    char *chSet = NULL;
    char *end;
    long int auth;
    int err;
    char *oldenc;
    enum {
        NO_PAP_OR_CHAP,
        PAP,
        CHAP,
        PAP_OR_CHAP,
    };

    ENTER;

    auth = strtol(authentication, &end, 10);
    if (end == NULL) {
        EXIT;
        return -1;
    }
    switch (auth) {
    case NO_PAP_OR_CHAP:
        /* PAP and CHAP is never performed., only none
         * PAP never performed; CHAP never performed */
        atAuth = "00001";
        break;
    case PAP:
        /* PAP may be performed; CHAP is never performed.
         * PAP may be performed; CHAP never performed */
        atAuth= "00011";
        break;
    case CHAP:
        /* CHAP may be performed; PAP is never performed
         * PAP never performed; CHAP may be performed */
        atAuth = "00101";
        break;
    case PAP_OR_CHAP:
        /* PAP / CHAP may be performed - baseband dependent.
         * PAP may be performed; CHAP may be performed. */
        atAuth = "00111";
        break;
    default:
        MBMLOGW("%s unrecognized authentication type %s. Using default value (CHAP, PAP and None).", __FUNCTION__, authentication);
        atAuth = "00111";
        break;
    }
    if (!user)
        user = "";
    if (0 == strcmp(user,"null"))
        user = "";
    if (!pass)
        pass = "";
    if (0 == strcmp(pass,"null"))
        pass = "";

    if ((NULL != strchr(user, '\\')) || (NULL != strchr(pass, '\\'))) {
        /* Because of module FW issues, some characters need UCS-2 format to be supported
         * in the user and pass strings. Read current setting, change to UCS-2 format,
         * send *EIAAUW command, and finally change back to previous character set.
         */
        oldenc = getCharEncoding();
        setCharEncoding("UCS2");

        atUser = ucs2StringCreate(user);
        atPass = ucs2StringCreate(pass);
        /* Even if sending of the command below would be erroneous, we should still
         * try to change back the character set to the original.
         */
        err = at_send_command("AT*EIAAUW=%d,1,\"%s\",\"%s\",%s", index, atUser, atPass, atAuth);
        free(atPass);
        free(atUser);

        /* Set back to the original character set */
        chSet = ucs2StringCreate(oldenc);
        setCharEncoding(chSet);
        free(chSet);
        free(oldenc);

        if (err < 0)
            return -1;
    } else {
        /* No need to change to UCS-2 during user and password setting */
        err = at_send_command("AT*EIAAUW=%d,1,\"%s\",\"%s\",%s", index, user, pass, atAuth);

        if (err < 0)
            return -1;
    }

    return 0;
}

int supl_set_apn_info(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    MBMLOGV("%s %s", __FUNCTION__, context->supl_config.apn);

    err = at_send_command("AT+CGDCONT=%d,\"IP\",\"%s\"", SUPL_CID,
                   context->supl_config.apn);
    if (err < 0) {
        EXIT;
        return -1;
    }

    if (networkAuth(context->supl_config.authtype, context->supl_config.username, context->supl_config.password, SUPL_CID) < 0) {
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

int supl_set_server(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    err = at_send_command("AT*E2GPSSUPL=1,%d,\"%s\",%d", SUPL_CID,
                   context->supl_config.supl_server,
                   context->supl_config.enable_ni);
    if (err < 0) {
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

int supl_set_allow_uncert (int allow)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    context->supl_config.allow_uncert = allow;

    err = at_send_command("AT*E2CERTUN=1");
    if (err < 0) {
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

int supl_set_enable_ni (int allow)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    context->supl_config.enable_ni = allow;

    err = at_send_command("AT*E2GPSSUPLNI=%d", context->supl_config.enable_ni);
    if (err < 0) {
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

int supl_init_context(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    ENTER;

    /* init to empty strings and let the mbm service fill it in */
    asprintf(&context->supl_config.apn, "%s", "");
    asprintf(&context->supl_config.username, "%s", "");
    asprintf(&context->supl_config.password, "%s", "");
    asprintf(&context->supl_config.authtype, "%s", "");

    /* start with empty supl server and use auto h-hslp unless
     * if a specific server is not configured with gps.conf
     */
    asprintf(&context->supl_config.supl_server, "%s", "");

    /* start with not allowing unknown certificates and network initiated requests
     * and let them later be set when read from static properties
     */
    context->supl_config.allow_uncert = 0;
    context->supl_config.enable_ni = 0;

    EXIT;
    return 0;
}

int supl_init(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    err = supl_set_apn_info();
    if (err < 0) {
        EXIT;
        return -1;
    }

    err = supl_set_allow_uncert (context->supl_config.allow_uncert);
    if (err < 0) {
        EXIT;
        return -1;
    }

    err = supl_set_server();
     if (err < 0) {
        EXIT;
        return -1;
    }

    err = supl_set_enable_ni (context->supl_config.enable_ni);
     if (err < 0) {
        EXIT;
        return -1;
    }

    context->supl_initialized = 1;

    EXIT;
    return 0;
}
