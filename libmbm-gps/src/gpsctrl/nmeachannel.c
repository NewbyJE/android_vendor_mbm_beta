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

#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include "nmeachannel.h"
#include "../nmea_reader.h"
#include "gps_ctrl.h"

#define LOG_NDEBUG 0
#define LOG_TAG "libgpsctrl-nmea"
#include "../log.h"

int nmea_read (int fd, char *nmea)
{
    NmeaContext *context = get_nmea_context();
    int ret;

    ENTER;

    do {
        ret = read(fd, nmea, MAX_NMEA_LENGTH);
    }
    while (ret < 0 && errno == EINTR);

    /* remove \r\n */
    if (strncmp(&nmea[ret - 2], "\r\n", 2)) {
        nmea[0] = '\0';
        EXIT;
        return -1;
    }
    nmea[ret - 2] = '\0';

    EXIT;
    return 0;
}

static int writeline (int fd, const char *s)
{
    NmeaContext *context = get_nmea_context();
    size_t cur = 0;
    size_t len;
    ssize_t written;
    char *cmd = NULL;

    ENTER;

    MBMLOGD("NMEA(%d)> %s\n", fd, s);

    len = asprintf(&cmd, "%s\r\n", s);

    /* The main string. */
    while (cur < len) {
        do {
            written = write (fd, cmd + cur, len - cur);
        } while (written < 0 && (errno == EINTR || errno == EAGAIN));

        if (written < 0) {
            free(cmd);
            MBMLOGV("%s, error write returned (%d) on nmea port", __FUNCTION__, (int) written);
            EXIT;
            return -1;
        }

        cur += written;
    }

    free(cmd);

    EXIT;
    return 0;
}

int nmea_activate_port (int nmea_fd)
{
    NmeaContext *context = get_nmea_context();
    int ret;

    ENTER;

    ret = writeline(nmea_fd, "AT*E2GPSNPD");
    if (ret < 0) {
        MBMLOGE("%s, error setting up port for nmea data", __FUNCTION__);
        EXIT;
        return -1;
    }

    EXIT;
    return 0;
}

int nmea_open (char *dev)
{
    NmeaContext *context = get_nmea_context();
    int ret;
    int nmea_fd;
    struct termios ios;

    ENTER;

    nmea_fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (nmea_fd < 0) {
        MBMLOGE("%s, nmea_fd < 0", __FUNCTION__);
        return -1;
    }

	tcflush(nmea_fd, TCIOFLUSH);
	tcgetattr(nmea_fd, &ios);
	memset(&ios, 0, sizeof(struct termios));
	ios.c_cflag |= CRTSCTS;
	ios.c_lflag |= ICANON;
	ios.c_cflag |= CS8;
	tcsetattr(nmea_fd, TCSANOW, &ios);
	tcflush(nmea_fd, TCIOFLUSH);
	tcsetattr(nmea_fd, TCSANOW, &ios);
	tcflush(nmea_fd, TCIOFLUSH);
	tcflush(nmea_fd, TCIOFLUSH);
	cfsetospeed(&ios, B115200);
	cfsetispeed(&ios, B115200);
	tcsetattr(nmea_fd, TCSANOW, &ios);

    /* setup port for receiving nmea data */
    ret = writeline(nmea_fd, "AT");
    if (ret < 0) {
        MBMLOGE("%s, error setting up port for nmea data", __FUNCTION__);
        return -1;
    }

    MBMLOGD("%s, pausing to let nmea port settle", __FUNCTION__);
    sleep(1);

    EXIT;

    return nmea_fd;
}

void nmea_close (void)
{
    GpsCtrlContext *context = get_gpsctrl_context();

    if(NULL == context) {
        ALOGW("%s, context invalid", __FUNCTION__);
        return;
    }

    ENTER;

    if (context->nmea_fd >= 0) {
        MBMLOGV("%s, closing fd=%d", __FUNCTION__, context->nmea_fd);
        if (close(context->nmea_fd) != 0)
            MBMLOGE("%s, failed to close fd %d!", __FUNCTION__, context->nmea_fd);
    } else {
        MBMLOGV("%s, fd already closed", __FUNCTION__);
        EXIT;
        return;
    }

    context->nmea_fd = -1;

    EXIT;
}
