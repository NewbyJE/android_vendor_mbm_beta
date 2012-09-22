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

#include "at_tok.h"
#include "gps_ctrl.h"
#include "atchannel.h"
#include "../mbm_service_handler.h"
#include "pgps.h"

#define LOG_NDEBUG 0
#define LOG_TAG "libgpsctrl-pgps"
#include "../log.h"

void* onPgpsUrlReceived(void *data)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;
    int ignore;
    int id;
    char *url;
    char *cmd;
    char *line = (char *) data;

    err = at_tok_start(&line);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        return NULL;
    }

    /* Ignore 1 integer from line */
    err = at_tok_nextint(&line, &ignore);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        return NULL;
    }

    err = at_tok_nextint(&line, &id);
    if (err < 0) {
        MBMLOGE("%s error parsing data", __FUNCTION__);
        return NULL;
    }

    err = at_tok_nextstr(&line, &url);
    if (err < 0) {
        MBMLOGW("%s error parsing pgps url", __FUNCTION__);
        return NULL;
    }

    err = asprintf(&cmd, "%d\n%s", id, url);
    if (err < 0) {
        MBMLOGE("%s error allocating cmd string", __FUNCTION__);
        return NULL;
    }

    service_handler_send_message(CMD_DOWNLOAD_PGPS_DATA, cmd);

    return NULL;
}

void onPgpsDataFailed(void)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    ENTER;
    EXIT;
}

int pgps_set_eedata (int connected)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    int err;

    ENTER;

    if (connected) {
        err = at_send_command("AT*EEGPSEEDATA=1");
        if (err < 0) {
            EXIT;
            return -1;
        }
    } else {
        err = at_send_command("AT*EEGPSEEDATA=0");
        if (err < 0) {
            EXIT;
            return -1;
        }
    }
    EXIT;
    return 0;
}

void pgps_read_data (int id, char *path)
{
    GpsCtrlContext *context = get_gpsctrl_context();
    FILE *file;
    long size;
    char *buffer;
    char *cmd;
    size_t result;
    int err;

    ENTER;

    file = fopen(path, "rb");
    if (file == NULL) {
        MBMLOGE("%s, unable to open file %s", __FUNCTION__, path);
        return;
    }

    /* obtain file size */
    fseek(file , 0 , SEEK_END);
    size = ftell(file);
    rewind(file);

    /* allocate memory to contain the whole file */
    buffer = (char*) malloc (sizeof(char)*size);
    if (buffer == NULL) {
        MBMLOGE("%s, malloc failed", __FUNCTION__);
        fclose(file);
        EXIT;
        return;
    }

    /* copy the file into the buffer */
    result = fread(buffer, 1, size, file);
    if ((long)result != size) {
        MBMLOGE("%s, error reading file", __FUNCTION__);
        fclose(file);
        free(buffer);
        EXIT;
        return;
    }

    /* the whole file is now loaded in the memory buffer */
    MBMLOGD("%s, read from file: %s", __FUNCTION__, buffer);

    err = asprintf(&cmd, "AT*EEGPSEEDATA=1,%d,%d", id, (int)size);
    if (err < 0) {
        MBMLOGE("%s, error creating command", __FUNCTION__);
        fclose(file);
        free(buffer);
        EXIT;
        return;
    }

    err = at_send_command_transparent(cmd, buffer, size, "", NULL);
    if (err < 0) {
        MBMLOGE("%s, error sending pgps data", __FUNCTION__);
    }

    free(cmd);
    fclose(file);
    free(buffer);
    EXIT;
}
