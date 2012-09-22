/* ST-Ericsson U300 RIL
**
** Copyright (C) ST-Ericsson AB 2008-2009
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** Based on reference-ril by The Android Open Source Project.
**
** Modified for ST-Ericsson U300 modems.
** Author: Christian Bejram <christian.bejram@stericsson.com>
*/

#include "atchannel.h"
#include "at_tok.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

#include <poll.h>

#define LOG_NDEBUG 0
#define LOG_TAG "libgpsctrl-at"
#include "../log.h"

#ifdef HAVE_ANDROID_OS
/* For IOCTL's */
#include <linux/omap_csmi.h>
#endif /*HAVE_ANDROID_OS*/

#include "misc.h"

#define MAX_AT_RESPONSE (8 * 1024)
#define HANDSHAKE_RETRY_COUNT 8
#define HANDSHAKE_TIMEOUT_MSEC 1000
#define DEFAULT_AT_TIMEOUT_MSEC (3 * 60 * 1000)
#define AT_WRITE_DELAY 150000
#define BUFFSIZE 256

struct atcontext {
    pthread_t tid_reader;
    int fd;                  /* fd of the AT channel. */
    int readerCmdFds[2];
    int isInitialized;
    ATUnsolHandler unsolHandler;

    /* For input buffering. */
    char ATBuffer[MAX_AT_RESPONSE+1];
    char *ATBufferCur;

    int readCount;

    /*
     * For current pending command, these are protected by commandmutex.
     *
     * The mutex and cond struct is memset in the getAtChannel() function,
     * so no initializer should be needed.
     */
    pthread_mutex_t requestmutex;
    pthread_mutex_t commandmutex;
    pthread_cond_t requestcond;
    pthread_cond_t commandcond;

    ATCommandType type;
    const char *responsePrefix;
    const char *smsPDU;
    size_t pdu_length;
    ATResponse *response;

    void (*onTimeout)(void);
    void (*onReaderClosed)(void);
    int readerClosed;

    int timeoutMsec;

    int loglevel;
};

static struct atcontext *s_defaultAtContext = NULL;
static va_list empty = {0};

static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static int writeTransparentMode (const char *s);
static int writeline (const char *s);
static void onReaderClosed(void);

static void make_key(void)
{
    (void) pthread_key_create(&key, NULL);
}

/**
 * Set the atcontext pointer. Useful for sub-threads that needs to hold
 * the same state information.
 *
 * The caller IS responsible for freeing any memory already allocated
 * for any previous atcontexts.
 */
static void set_at_context(struct atcontext *ac)
{
    (void) pthread_once(&key_once, make_key);
    (void) pthread_setspecific(key, ac);
}

static void free_at_context(void)
{
    struct atcontext *context = NULL;
    (void) pthread_once(&key_once, make_key);
    if ((context = pthread_getspecific(key)) != NULL) {
        free(context);
        ALOGV("%s() freed current thread AT context", __FUNCTION__);
    } else {
        ALOGW("%s() No AT context exist for current thread, cannot free it",
            __FUNCTION__);
    }
}

static int initializeAtContext(int loglevel)
{
    struct atcontext *context = NULL;

    if (pthread_once(&key_once, make_key)) {
        ALOGE("%s() Pthread_once failed!", __FUNCTION__);
        goto error;
    }

    context = pthread_getspecific(key);

    if (context == NULL) {
        context = malloc(sizeof(struct atcontext));
        if (context == NULL) {
            ALOGE("%s(): Failed to allocate memory", __FUNCTION__);
            goto error;
        }

        memset(context, 0, sizeof(struct atcontext));

        context->fd = -1;
        context->readerCmdFds[0] = -1;
        context->readerCmdFds[1] = -1;
        context->ATBufferCur = context->ATBuffer;
        context->loglevel = loglevel;

        if (pipe(context->readerCmdFds)) {
            ALOGE("%s(): Failed to create pipe: %s", __FUNCTION__,
                 strerror(errno));
            goto error;
        }

        pthread_mutex_init(&context->commandmutex, NULL);
        pthread_mutex_init(&context->requestmutex, NULL);
        pthread_cond_init(&context->requestcond, NULL);
        pthread_cond_init(&context->commandcond, NULL);

        context->timeoutMsec = DEFAULT_AT_TIMEOUT_MSEC;

        if (pthread_setspecific(key, context)) {
            ALOGE("%s() calling pthread_setspecific failed!", __FUNCTION__);
            goto error;
        }
    }

    context->loglevel = loglevel;

    MBMLOGV("Initialized new AT Context!");

    return 0;

error:
    ALOGE("%s() failed initializing new AT Context!", __FUNCTION__);
    free(context);
    return -1;
}

static struct atcontext *get_at_context(void)
{
    struct atcontext *context = NULL;

    (void) pthread_once(&key_once, make_key);

    if ((context = pthread_getspecific(key)) == NULL) {
        if (s_defaultAtContext)
            context = s_defaultAtContext;
        else {
            ALOGE("WARNING! get_at_context() called from external thread with "
                 "no defaultAtContext set!! This IS a bug! "
                 "A crash is possibly nearby!");
        }
    }

    return context;
}

/**
 * This function will make the current at thread the default channel,
 * meaning that calls from a thread that is not a queuerunner will
 * be executed in this context.
 */
void at_make_default_channel(void)
{
    struct atcontext *context = get_at_context();

    ENTER;

    if (context->isInitialized)
        s_defaultAtContext = context;

    EXIT;
}

#if AT_DEBUG
void  AT_DUMP(const char*  prefix, const char*  buff, int  len)
{
    if (len < 0)
        len = strlen(buff);
    ALOGD("%.*s", len, buff);
}
#endif

#ifndef HAVE_ANDROID_OS
int pthread_cond_timeout_np(pthread_cond_t *cond,
                            pthread_mutex_t * mutex,
                            unsigned msecs)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec += msecs / 1000;
    ts.tv_nsec += (msecs % 1000) * 1000000;
    return pthread_cond_timedwait(cond, mutex, &ts);
}
#endif /*HAVE_ANDROID_OS*/

static void sleepMsec(long long msec)
{
    struct timespec ts;
    int err;

    ts.tv_sec = (msec / 1000);
    ts.tv_nsec = (msec % 1000) * 1000 * 1000;

    do {
        err = nanosleep (&ts, &ts);
    } while (err < 0 && errno == EINTR);
}



/** Add an intermediate response to sp_response. */
static void addIntermediate(const char *line)
{
    struct atcontext *context = get_at_context();
    ATLine *p_new;

    ENTER;

    p_new = (ATLine  *) malloc(sizeof(ATLine));

    p_new->line = strdup(line);

    /* Note: This adds to the head of the list, so the list will
       be in reverse order of lines received. the order is flipped
       again before passing on to the command issuer. */
    p_new->p_next = context->response->p_intermediates;
    context->response->p_intermediates = p_new;

    EXIT;
}


/**
 * Returns 1 if line is a final response indicating error.
 * See 27.007 annex B.
 * WARNING: NO CARRIER and others are sometimes unsolicited.
 */
static const char * s_finalResponsesError[] = {
    "ERROR",
    "+CMS ERROR:",
    "+CME ERROR:",
    "NO CARRIER",      /* Sometimes! */
    "NO ANSWER",
    "NO DIALTONE",
};
static int isFinalResponseError(const char *line)
{
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesError) ; i++) {
        if (strStartsWith(line, s_finalResponsesError[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * Returns 1 if line is a final response indicating success.
 * See 27.007 annex B.
 * WARNING: NO CARRIER and others are sometimes unsolicited.
 */
static const char * s_finalResponsesSuccess[] = {
    "OK",
    "CONNECT"       /* Some stacks start up data on another channel. */
};
static int isFinalResponseSuccess(const char *line)
{
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesSuccess) ; i++) {
        if (strStartsWith(line, s_finalResponsesSuccess[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * Returns 1 if line is the first line in (what will be) a two-line
 * SMS unsolicited response.
 */
static const char * s_smsUnsoliciteds[] = {
    "+CMT:",
    "+CDS:",
    "+CBM:"
};
static int isSMSUnsolicited(const char *line)
{
    size_t i;

    for (i = 0 ; i < NUM_ELEMS(s_smsUnsoliciteds) ; i++) {
        if (strStartsWith(line, s_smsUnsoliciteds[i])) {
            return 1;
        }
    }

    return 0;
}


/** Assumes s_commandmutex is held. */
static void handleFinalResponse(const char *line)
{
    struct atcontext *context = get_at_context();

    ENTER;

    context->response->finalResponse = strdup(line);

    pthread_cond_signal(&context->commandcond);

    EXIT;
}

static void handleUnsolicited(const char *line)
{
    struct atcontext *context = get_at_context();

    ENTER;

    if (context->unsolHandler != NULL) {
        context->unsolHandler(line, NULL);
    }
    EXIT;
}

static void processLine(const char *line)
{
    struct atcontext *context = get_at_context();

    ENTER;

    pthread_mutex_lock(&context->commandmutex);

    if (context->response == NULL) {
        /* No command pending. */
        handleUnsolicited(line);
    } else if (isFinalResponseSuccess(line)) {
        context->response->success = 1;
        handleFinalResponse(line);
    } else if (isFinalResponseError(line)) {
        context->response->success = 0;
        handleFinalResponse(line);
    } else if (context->smsPDU != NULL && 0 == strcmp(line, "> ")) {
        /* See eg. TS 27.005 4.3.
           Commands like AT+CMGS have a "> " prompt. */
        writeTransparentMode(context->smsPDU);
        context->smsPDU = NULL;
    } else switch (context->type) {
        case NO_RESULT:
            handleUnsolicited(line);
            break;
        case NUMERIC:
            if (context->response->p_intermediates == NULL
                && isdigit(line[0])
            ) {
                addIntermediate(line);
            } else {
                /* Either we already have an intermediate response or
                   the line doesn't begin with a digit. */
                handleUnsolicited(line);
            }
            break;
        case SINGLELINE:
            if (context->response->p_intermediates == NULL
                && strStartsWith (line, context->responsePrefix)
            ) {
                addIntermediate(line);
            } else {
                /* We already have an intermediate response. */
                handleUnsolicited(line);
            }
            break;
        case MULTILINE:
            if (strStartsWith (line, context->responsePrefix)) {
                addIntermediate(line);
            } else {
                handleUnsolicited(line);
            }
        break;

        default: /* This should never be reached */
            MBMLOGE("Unsupported AT command type %d\n", context->type);
            handleUnsolicited(line);
        break;
    }

    pthread_mutex_unlock(&context->commandmutex);

    EXIT;
}


/**
 * Returns a pointer to the end of the next line,
 * special-cases the "> " SMS prompt.
 *
 * returns NULL if there is no complete line.
 */
static char * findNextEOL(char *cur)
{
    if (cur[0] == '>' && cur[1] == ' ' && cur[2] == '\0') {
        /* SMS prompt character...not \r terminated */
        return cur+2;
    }

    /* Find next newline */
    while (*cur != '\0' && *cur != '\r' && *cur != '\n') cur++;

    return *cur == '\0' ? NULL : cur;
}


/**
 * Reads a line from the AT channel, returns NULL on timeout.
 * Assumes it has exclusive read access to the FD.
 *
 * This line is valid only until the next call to readline.
 *
 * This function exists because as of writing, android libc does not
 * have buffered stdio.
 */

static const char *readline(void)
{
    ssize_t count;

    char *p_read = NULL;
    char *p_eol = NULL;
    char *ret = NULL;

    struct atcontext *context = get_at_context();

    ENTER;

    read(context->fd,NULL,0);

    /* This is a little odd. I use *s_ATBufferCur == 0 to mean
     * "buffer consumed completely". If it points to a character,
     * then the buffer continues until a \0.
     */
    if (*context->ATBufferCur == '\0') {
        /* Empty buffer. */
        context->ATBufferCur = context->ATBuffer;
        *context->ATBufferCur = '\0';
        p_read = context->ATBuffer;
    } else {   /* *s_ATBufferCur != '\0' */
        /* There's data in the buffer from the last read. */

        /* skip over leading newlines */
        while (*context->ATBufferCur == '\r' || *context->ATBufferCur == '\n')
            context->ATBufferCur++;

        p_eol = findNextEOL(context->ATBufferCur);

        if (p_eol == NULL) {
            /* A partial line. Move it up and prepare to read more. */
            size_t len;

            len = strlen(context->ATBufferCur);

            memmove(context->ATBuffer, context->ATBufferCur, len + 1);
            p_read = context->ATBuffer + len;
            context->ATBufferCur = context->ATBuffer;
        }
        /* Otherwise, (p_eol !- NULL) there is a complete line 
           that will be returned from the while () loop below. */
    }

    while (p_eol == NULL) {
        int err;
        struct pollfd pfds[2];

        if (0 >= MAX_AT_RESPONSE - (p_read - context->ATBuffer)) {
            MBMLOGE("%s() ERROR: Input line exceeded buffer", __FUNCTION__);
            /* Ditch buffer and start over again. */
            context->ATBufferCur = context->ATBuffer;
            *context->ATBufferCur = '\0';
            p_read = context->ATBuffer;
        }

        /* If our fd is invalid, we are probably closed. Return. */
        if (context->fd < 0) {
            EXIT;
            return NULL;
        }

        pfds[0].fd = context->fd;
        pfds[0].events = POLLIN | POLLERR;

        pfds[1].fd = context->readerCmdFds[0];
        pfds[1].events = POLLIN;

        err = poll(pfds, 2, -1);

        if (err < 0) {
            MBMLOGE("%s() poll: error: %s", __FUNCTION__, strerror(errno));
            EXIT;
            return NULL;
        }

        if (pfds[1].revents & POLLIN) {
            char buf[10];

            /* Just drain it. We don't care, this is just for waking up. */
            read(pfds[1].fd, &buf, 1);
            continue;
        }

        if (pfds[0].revents & POLLERR) {
            MBMLOGE("POLLERR! Returning..");
            EXIT;
            return NULL;
        }

        if (!(pfds[0].revents & POLLIN)) {
            continue;
        }

        do {
            count = read(context->fd, p_read,
                            MAX_AT_RESPONSE - (p_read - context->ATBuffer));
        } while (count < 0 && ((errno == EINTR) || (errno == EAGAIN)));

        if (count > 0) {
            AT_DUMP( "<< ", p_read, count );
            context->readCount += count;

            p_read[count] = '\0';

            /* Skip over leading newlines. */
            while (*context->ATBufferCur == '\r' || *context->ATBufferCur == '\n')
                context->ATBufferCur++;

            p_eol = findNextEOL(context->ATBufferCur);
            p_read += count;
        } else if (count <= 0) {
            /* Read error encountered or EOF reached. */
            if (count == 0) {
                MBMLOGE("%s() atchannel: EOF reached.", __FUNCTION__);
            } else {
                MBMLOGE("%s() atchannel: read error %s", __FUNCTION__, strerror(errno));
            }
            EXIT;
            return NULL;
        }
    }

    /* A full line in the buffer. Place a \0 over the \r and return. */

    ret = context->ATBufferCur;
    *p_eol = '\0';
    context->ATBufferCur = p_eol + 1;     /* This will always be <= p_read,    
                                        and there will be a \0 at *p_read. */

    MBMLOGD("AT(%d)< %s", context->fd, ret);

    EXIT;
    return ret;
}


static void onReaderClosed(void)
{
    struct atcontext *context = get_at_context();

    ENTER;

    if (context->onReaderClosed != NULL && context->readerClosed == 0) {

        pthread_mutex_lock(&context->commandmutex);

        context->readerClosed = 1;

        pthread_cond_signal(&context->commandcond);

        pthread_mutex_unlock(&context->commandmutex);

        context->onReaderClosed();
    }

    EXIT;
}


static void *readerLoop(void *arg)
{
    struct atcontext *context = NULL;

    set_at_context((struct atcontext *) arg);
    context = get_at_context();

    MBMLOGV("Entering readerLoop()");

    for (;;) {
        const char * line;

        line = readline();

        if (line == NULL)
            break;

        if(isSMSUnsolicited(line)) {
            char *line1;
            const char *line2;

            /* The scope of string returned by 'readline()' is valid only
               until next call to 'readline()' hence making a copy of line
               before calling readline again. */
            line1 = strdup(line);
            line2 = readline();

            if (line2 == NULL) {
                free(line1);
                break;
            }

            if (context->unsolHandler != NULL)
                context->unsolHandler(line1, line2);

            free(line1);
        } else
            processLine(line);
    }

    onReaderClosed();
    MBMLOGV("Exiting readerLoop()");
    return NULL;
}

/**
 * Sends string s to the radio with a \r appended.
 * Returns AT_ERROR_* on error, 0 on success.
 *
 * This function exists because as of writing, android libc does not
 * have buffered stdio.
 */
static int writeline (const char *s)
{
    size_t cur = 0;
    size_t len;
    ssize_t written;
    char *cmd = NULL;

    struct atcontext *context = get_at_context();

    ENTER;

    if (context->fd < 0 || context->readerClosed > 0) {
        EXIT;
        return AT_ERROR_CHANNEL_CLOSED;
    }

    MBMLOGD("AT(%d)> %s\n", context->fd, s);

    AT_DUMP( ">> ", s, strlen(s) );

    len = asprintf(&cmd, "%s\r\n", s);

    /* The main string. */
    while (cur < len) {
        do {
            usleep(AT_WRITE_DELAY);
            written = write (context->fd, cmd + cur, len - cur);
        } while (written < 0 && (errno == EINTR || errno == EAGAIN));

        if (written < 0) {
            free(cmd);
            EXIT;
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

    free(cmd);

    EXIT;
    return 0;
}


static int writeTransparentMode (const char *s)
{
    size_t cur = 0;
    size_t len;
    ssize_t written;
    size_t write_len = 0;

    struct atcontext *context = get_at_context();

    ENTER;

    len = context->pdu_length;

    if (context->fd < 0 || context->readerClosed > 0) {
        EXIT;
        return AT_ERROR_CHANNEL_CLOSED;
    }

    MBMLOGD("AT> %s^Z\n", s);

    AT_DUMP( ">* ", s, strlen(s) );

    /* The main string. */
    while (cur < len) {
        do {
            usleep(AT_WRITE_DELAY);
            if ((len - cur) >= 256)
                write_len = 256;
            else
                write_len = len - cur;
            written = write (context->fd, s + cur, write_len);
        } while (written < 0 && errno == EINTR);

        if (written < 0) {
            MBMLOGE("%s, AT_ERROR_GENERIC, written=%d", __FUNCTION__, (int)written);
            EXIT;
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

    EXIT;
    return 0;
}

static void clearPendingCommand(void)
{
    struct atcontext *context = get_at_context();

    ENTER;

    if (context->response != NULL) {
        at_response_free(context->response);
    }

    context->response = NULL;
    context->responsePrefix = NULL;
    context->smsPDU = NULL;

    EXIT;
}

static int merror(int type, int error)
{
    switch(type) {
    case AT_ERROR :
        return AT_ERROR_BASE + error;
    case CME_ERROR :
        return CME_ERROR_BASE + error;
    case CMS_ERROR:
        return CMS_ERROR_BASE + error;
    case GENERIC_ERROR:
        return GENERIC_ERROR_BASE + error;
    default:
        return GENERIC_ERROR_UNSPECIFIED;
    }
}

static AT_Error at_get_error(const ATResponse *p_response)
{
    int ret;
    int err;
    char *p_cur;

    if (p_response == NULL)
        return merror(GENERIC_ERROR, GENERIC_ERROR_UNSPECIFIED);

    if (p_response->success > 0) {
        return AT_NOERROR;
    }

    if (p_response->finalResponse == NULL)
        return AT_ERROR_INVALID_RESPONSE;


    if (isFinalResponseSuccess(p_response->finalResponse))
        return AT_NOERROR;

    p_cur = p_response->finalResponse;
    err = at_tok_start(&p_cur);
    if (err < 0)
        return merror(GENERIC_ERROR, GENERIC_ERROR_UNSPECIFIED);

    err = at_tok_nextint(&p_cur, &ret);
    if (err < 0)
        return merror(GENERIC_ERROR, GENERIC_ERROR_UNSPECIFIED);

    if(strStartsWith(p_response->finalResponse, "+CME ERROR:"))
        return merror(CME_ERROR, ret);
    else if (strStartsWith(p_response->finalResponse, "+CMS ERROR:"))
        return merror(CMS_ERROR, ret);
    else if (strStartsWith(p_response->finalResponse, "ERROR:"))
        return merror(GENERIC_ERROR, GENERIC_ERROR_RESPONSE);
    else if (strStartsWith(p_response->finalResponse, "+NO CARRIER:"))
        return merror(GENERIC_ERROR, GENERIC_NO_CARRIER_RESPONSE);
    else if (strStartsWith(p_response->finalResponse, "+NO ANSWER:"))
        return merror(GENERIC_ERROR, GENERIC_NO_ANSWER_RESPONSE);
    else if (strStartsWith(p_response->finalResponse, "+NO DIALTONE:"))
        return merror(GENERIC_ERROR, GENERIC_NO_DIALTONE_RESPONSE);
    else
        return merror(GENERIC_ERROR, GENERIC_ERROR_UNSPECIFIED);
}

/**
 * Starts AT handler on stream "fd'.
 * returns 0 on success, -1 on error.
 */
int at_reader_open(int fd, ATUnsolHandler h, int loglevel)
{
    int ret;
    pthread_attr_t attr;

    struct atcontext *context = NULL;

    if (initializeAtContext(loglevel)) {
        ALOGE("InitializeAtContext() failed!");
        goto error;
    }

    context = get_at_context();

    context->fd = fd;
    context->isInitialized = 1;
    context->unsolHandler = h;
    context->readerClosed = 0;

    context->responsePrefix = NULL;
    context->smsPDU = NULL;
    context->response = NULL;
    context->loglevel = loglevel;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    ret = pthread_create(&context->tid_reader, &attr, readerLoop, context);

    if (ret < 0) {
        MBMLOGE("pthread_create");
        goto error;
    }

    return 0;
error:
    free_at_context();
    return -1;
}

/* FIXME is it ok to call this from the reader and the command thread? */
void at_reader_close(void)
{
    struct atcontext *context = get_at_context();

    if(NULL == context) {
        ALOGW("%s, context invalid", __FUNCTION__);
        return;
    }

    ENTER;

    if (context->fd >= 0) {
        MBMLOGV("%s, closing fd=%d", __FUNCTION__, context->fd);
        if (close(context->fd) != 0)
            MBMLOGE("%s, failed to close fd %d!", __FUNCTION__, context->fd);
    } else {
        MBMLOGV("%s, fd already closed", __FUNCTION__);
        EXIT;
        return;
    }

    context->fd = -1;

    pthread_mutex_lock(&context->commandmutex);

    context->readerClosed = 1;

    pthread_cond_signal(&context->commandcond);

    pthread_mutex_unlock(&context->commandmutex);

    /* Kick readerloop. */
    write(context->readerCmdFds[1], "x", 1);

    EXIT;
}

static ATResponse *at_response_new(void)
{
    return (ATResponse *) calloc(1, sizeof(ATResponse));
}

void at_response_free(ATResponse *p_response)
{
    ATLine *p_line;

    if (p_response == NULL) return;

    p_line = p_response->p_intermediates;

    while (p_line != NULL) {
        ATLine *p_toFree;

        p_toFree = p_line;
        p_line = p_line->p_next;

        free(p_toFree->line);
        free(p_toFree);
    }

    free (p_response->finalResponse);
    free (p_response);
}

/**
 * The line reader places the intermediate responses in reverse order,
 * here we flip them back.
 */
static void reverseIntermediates(ATResponse *p_response)
{
    ATLine *pcur,*pnext;

    pcur = p_response->p_intermediates;
    p_response->p_intermediates = NULL;

    while (pcur != NULL) {
        pnext = pcur->p_next;
        pcur->p_next = p_response->p_intermediates;
        p_response->p_intermediates = pcur;
        pcur = pnext;
    }
}

/**
 * Internal send_command implementation.
 * Doesn't lock or call the timeout callback.
 *
 * timeoutMsec == 0 means infinite timeout.
 */
static int at_send_command_full_nolock (const char *command, ATCommandType type,
                    const char *responsePrefix, const char *smspdu,
                    long long timeoutMsec, ATResponse **pp_outResponse)
{
    int err = AT_NOERROR;

    struct atcontext *context = get_at_context();

    ENTER;

    /* Default to NULL, to allow caller to free securely even if
     * no response will be set below */
    if (pp_outResponse != NULL)
        *pp_outResponse = NULL;

    /* FIXME This is to prevent future problems due to calls from other threads; should be revised. */
    while (pthread_mutex_trylock(&context->requestmutex) == EBUSY)
        pthread_cond_wait(&context->requestcond, &context->commandmutex);

    if(context->response != NULL) {
        err = AT_ERROR_COMMAND_PENDING;
        goto finally;
    }

    context->type = type;
    context->responsePrefix = responsePrefix;
    context->smsPDU = smspdu;
    context->response = at_response_new();
    if (context->response == NULL) {
        err = AT_ERROR_MEMORY_ALLOCATION;
        goto finally;
    }

    err = writeline (command);

    if (err != AT_NOERROR)
        goto finally;

    while (context->response->finalResponse == NULL && context->readerClosed == 0) {
        if (timeoutMsec != 0) {
            err = pthread_cond_timeout_np(&context->commandcond, &context->commandmutex, timeoutMsec);
        } else
            err = pthread_cond_wait(&context->commandcond, &context->commandmutex);

        if (err == ETIMEDOUT) {
            err = AT_ERROR_TIMEOUT;
            goto finally;
        }
    }

    if (context->response->success == 0) {
        err = at_get_error(context->response);
    }

    if (pp_outResponse == NULL)
        at_response_free(context->response);
    else {
        /* Line reader stores intermediate responses in reverse order. */
        reverseIntermediates(context->response);
        *pp_outResponse = context->response;
    }

    context->response = NULL;

    if(context->readerClosed > 0) {
        err = AT_ERROR_CHANNEL_CLOSED;
        goto finally;
    }

finally:
    clearPendingCommand();

    pthread_cond_broadcast(&context->requestcond);
    pthread_mutex_unlock(&context->requestmutex);

    EXIT;
    return err;
}

/**
 * Internal send_command implementation.
 *
 * timeoutMsec == 0 means infinite timeout.
 */
static int at_send_command_full (const char *command, ATCommandType type,
                    const char *responsePrefix, const char *smspdu,
                    long long timeoutMsec, ATResponse **pp_outResponse, int useap, va_list ap)
{
    int err;

    struct atcontext *context = get_at_context();
    static char strbuf[BUFFSIZE];
    const char *ptr;

    ENTER;

    if (0 != pthread_equal(context->tid_reader, pthread_self())) {
        /* Cannot be called from reader thread. */
        EXIT;
        return AT_ERROR_INVALID_THREAD;
    }

    pthread_mutex_lock(&context->commandmutex);
    if (useap) {
        if (!vsnprintf(strbuf, BUFFSIZE, command, ap)) {
           pthread_mutex_unlock(&context->commandmutex);
            EXIT;
           return AT_ERROR_STRING_CREATION;
        }
        ptr = strbuf;
    } else {
        ptr = command;
    }

    err = at_send_command_full_nolock(ptr, type,
                    responsePrefix, smspdu,
                    timeoutMsec, pp_outResponse);

    pthread_mutex_unlock(&context->commandmutex);

    if (err == AT_ERROR_TIMEOUT && context->onTimeout != NULL)
        context->onTimeout();

    EXIT;
    return err;
}

/* Only call this from onTimeout, since we're not locking or anything. */
void at_send_escape (void)
{
    struct atcontext *context = get_at_context();
    int written;

    do
        written = write (context->fd, "\033" , 1);
    while ((written < 0 && errno == EINTR) || (written == 0));
}

/**
 * Issue a single normal AT command with no intermediate response expected.
 *
 * "command" should not include \r.
 * pp_outResponse can be NULL.
 *
 * if non-NULL, the resulting ATResponse * must be eventually freed with
 * at_response_free.
 */
int at_send_command (const char *command, ...)
{
    int err;

    struct atcontext *context = get_at_context();
    va_list ap;
    va_start(ap, command);

    ENTER;

    err = at_send_command_full (command, NO_RESULT, NULL,
                                    NULL, context->timeoutMsec, NULL, 1, ap);
    va_end(ap);

    if (err != AT_NOERROR)
        MBMLOGE("%s --- %s", command, at_str_err(-err));

    EXIT;
    return -err;
}

int at_send_command_raw (const char *command, ATResponse **pp_outResponse)
{
    struct atcontext *context = get_at_context();
    int err;

    ENTER;

    err = at_send_command_full (command, MULTILINE, "",
            NULL, context->timeoutMsec, pp_outResponse, 0, empty);

    /* Don't check for intermediate responses as it is unknown if any
     * intermediate responses are expected. Don't free the response, instead,
     * let calling function free the allocated response.
     */

    if (err != AT_NOERROR)
        MBMLOGE("%s --- %s", command, at_str_err(-err));

    EXIT;

    return -err;
}

int at_send_command_singleline (const char *command,
                                const char *responsePrefix,
                                 ATResponse **pp_outResponse, ...)
{
    int err;

    struct atcontext *context = get_at_context();
    va_list ap;
    va_start(ap, pp_outResponse);

    ENTER;

    err = at_send_command_full (command, SINGLELINE, responsePrefix,
                                    NULL, context->timeoutMsec, pp_outResponse, 1, ap);
    if (err == AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL
            && (*pp_outResponse)->p_intermediates == NULL)
        /* Command with pp_outResponse must have an intermediate response */
        err = AT_ERROR_INVALID_RESPONSE;

    /* Free response in case of error */
    if (err != AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL) {
         at_response_free(*pp_outResponse);
         *pp_outResponse = NULL;
    }

    va_end(ap);

    if (err != AT_NOERROR)
        MBMLOGE("%s --- %s", command, at_str_err(-err));

    EXIT;
    return -err;
}

int at_send_command_numeric (const char *command,
                                 ATResponse **pp_outResponse)
{
    int err;

    struct atcontext *context = get_at_context();

    ENTER;

    err = at_send_command_full (command, NUMERIC, NULL,
                                    NULL, context->timeoutMsec, pp_outResponse, 0, empty);

    if (err == AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL
            && (*pp_outResponse)->p_intermediates == NULL)
        /* Command with pp_outResponse must have an intermediate response */
        err = AT_ERROR_INVALID_RESPONSE;

    /* Free response in case of error */
    if (err != AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL) {
         at_response_free(*pp_outResponse);
         *pp_outResponse = NULL;
    }

    if (err != AT_NOERROR)
        MBMLOGE("%s --- %s", command, at_str_err(-err));

    EXIT;
    return -err;
}


int at_send_command_transparent (const char *command,
                                 const char *pdu,
                                 size_t length,
                                 const char *responsePrefix,
                                 ATResponse **pp_outResponse)
{
    int err;

    struct atcontext *context = get_at_context();

    ENTER;

    context->pdu_length = length;

    err = at_send_command_full (command, SINGLELINE, responsePrefix,
                                    pdu, context->timeoutMsec, pp_outResponse, 0, empty);
    if (err == AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL
            && (*pp_outResponse)->p_intermediates == NULL)
        /* Command with pp_outResponse must have an intermediate response */
        err = AT_ERROR_INVALID_RESPONSE;

    /* Free response in case of error */
    if (err != AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL) {
         at_response_free(*pp_outResponse);
         *pp_outResponse = NULL;
    }

    if (err != AT_NOERROR)
        MBMLOGE("%s --- %s", command, at_str_err(-err));

    EXIT;
    return -err;
}


int at_send_command_multiline (const char *command,
                                const char *responsePrefix,
                                 ATResponse **pp_outResponse, ...)
{
    int err;

    struct atcontext *context = get_at_context();

    ENTER;

    va_list ap;
    va_start(ap, pp_outResponse);

    err = at_send_command_full (command, MULTILINE, responsePrefix,
                                    NULL, context->timeoutMsec, pp_outResponse, 1, ap);
    va_end(ap);

    if (err == AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL
            && (*pp_outResponse)->p_intermediates == NULL)
        /* Command with pp_outResponse must have an intermediate response */
        err = AT_ERROR_INVALID_RESPONSE;

    /* Free response in case of error */
    if (err != AT_NOERROR && pp_outResponse != NULL
            && (*pp_outResponse) != NULL) {
         at_response_free(*pp_outResponse);
         *pp_outResponse = NULL;
    }

    if (err != AT_NOERROR)
        MBMLOGE("%s --- %s", command, at_str_err(-err));

    EXIT;
    return -err;
}

/**
 * Set the default timeout. Let it be reasonably high, some commands
 * take their time. Default is 10 minutes.
 */
void at_set_timeout_msec(int timeout)
{
    struct atcontext *context = get_at_context();
    ENTER;
    context->timeoutMsec = timeout;
    EXIT;
}

/** This callback is invoked on the command thread. */
void at_set_on_timeout(void (*onTimeout)(void))
{
    struct atcontext *context = get_at_context();
    ENTER;
    context->onTimeout = onTimeout;
    EXIT;
}

/*
 * This callback is invoked on the reader thread (like ATUnsolHandler), when the
 * input stream closes before you call at_close (not when you call at_close()).
 * You should still call at_close(). It may also be invoked immediately from the
 * current thread if the read channel is already closed.
 */
void at_set_on_reader_closed(void (*onClose)(void))
{
    struct atcontext *context = get_at_context();
    ENTER;
    context->onReaderClosed = onClose;
    EXIT;
}


/**
 * Periodically issue an AT command and wait for a response.
 * Used to ensure channel has start up and is active.
 */
int at_handshake(void)
{
    int i;
    int err = 0;

    struct atcontext *context = get_at_context();

    ENTER;

    if (0 != pthread_equal(context->tid_reader, pthread_self())) {
        /* Cannot be called from reader thread. */
        EXIT;
        return AT_ERROR_INVALID_THREAD;
    }

    pthread_mutex_lock(&context->commandmutex);

    for (i = 0 ; i < HANDSHAKE_RETRY_COUNT ; i++) {
        /* Some stacks start with verbose off. */
        err = at_send_command_full_nolock ("ATE0V1", NO_RESULT,
                    NULL, NULL, HANDSHAKE_TIMEOUT_MSEC, NULL);

        if (err == 0) {
            break;
        }
    }

    if (err == 0) {
        /* Pause for a bit to let the input buffer drain any unmatched OK's
           (they will appear as extraneous unsolicited responses). */
        MBMLOGD("pausing..");
        sleepMsec(HANDSHAKE_TIMEOUT_MSEC);
    }

    pthread_mutex_unlock(&context->commandmutex);

    EXIT;
    return -err;
}

AT_Error at_get_at_error(int error)
{
    error = -error;
    if (error >= AT_ERROR_BASE && error < AT_ERROR_TOP)
        return error - AT_ERROR_BASE;
    else
        return AT_ERROR_NON_AT;
}

AT_CME_Error at_get_cme_error(int error)
{
    error = -error;
    if (error >= CME_ERROR_BASE && error < CME_ERROR_TOP)
        return error - CME_ERROR_BASE;
    else
        return CME_ERROR_NON_CME;
}

AT_CMS_Error at_get_cms_error(int error)
{
    error = -error;
    if (error >= CMS_ERROR_BASE && error < CMS_ERROR_TOP)
        return error - CMS_ERROR_BASE;
    else
        return CMS_ERROR_NON_CMS;
}

AT_Generic_Error at_get_generic_error(int error)
{
    error = -error;
    if (error >= GENERIC_ERROR_BASE && error < GENERIC_ERROR_TOP)
        return error - GENERIC_ERROR_BASE;
    else
        return GENERIC_ERROR_NON_GENERIC;
}

AT_Error_type at_get_error_type(int error)
{
    error = -error;
    if (error == AT_NOERROR)
        return NONE_ERROR;

    if (error > AT_ERROR_BASE && error <= AT_ERROR_TOP)
        return AT_ERROR;

    if (error >= CME_ERROR_BASE && error <= CME_ERROR_TOP)
        return CME_ERROR;

    if (error >= CMS_ERROR_BASE && error <= CMS_ERROR_TOP)
        return CMS_ERROR;

    if (error >= GENERIC_ERROR_BASE && error <= GENERIC_ERROR_TOP)
        return GENERIC_ERROR;

    return UNKNOWN_ERROR;
}

#define quote(x) #x

char *at_str_err(int error) {
    char *s = "--UNKNOWN--";

    error = -error;
    switch(error) {
#define AT(name, num) case num + AT_ERROR_BASE: s = quote(AT_##name); break;
#define CME(name, num) case num + CME_ERROR_BASE: s = quote(CME_##name); break;
#define CMS(name, num) case num + CMS_ERROR_BASE: s = quote(CMS_##name); break;
#define GENERIC(name, num) case num + GENERIC_ERROR_BASE: s = quote(GENERIC_##name); break;
    mbm_error
#undef AT
#undef CME
#undef CMS
#undef GENERIC
    }

    return s;
}
