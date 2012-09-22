/*
 * Copyright 2010 Ericsson AB
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
 * Author: Indrek Peri <Indrek.Peri@ericsson.com>  */

#ifndef _LIBMBMGPS_LOG_H
#define _LIBMBMGPS_LOG_H 1

#ifndef LOG_TAG
#define  LOG_TAG  "libmbm-gps"
#endif

#include <cutils/log.h>

/*
   Checks to see whether or not a log for the specified tag is
   loggable at the specified level. The default level of any
   tag is set to INFO. This means that any level above and
   including INFO will be logged. Before you make any calls
   to a logging method you should check to see if the message
   should be logged. You can change the default level by
   setting a system property: 'setprop mbm.gps.config.loglevel <LEVEL>'
   Where level is either VERBOSE, DEBUG, INFO, WARN, ERROR
   or SILENT. SILENT will turn off all logging
   for the library. You can also create a local.prop file
   that with the following in it: 'mbm.gps.config.loglevel=<LEVEL>'
   and place that in /data/local.prop.
*/

#define MBMLOGE(...) do { if ((NULL != context) && (context->loglevel <= ANDROID_LOG_ERROR)) ALOGE(__VA_ARGS__); }while(0)
#define MBMLOGW(...) do { if ((NULL != context) && (context->loglevel <= ANDROID_LOG_WARN)) ALOGW(__VA_ARGS__); }while(0)
#define MBMLOGI(...) do { if ((NULL != context) && (context->loglevel <= ANDROID_LOG_INFO)) ALOGI(__VA_ARGS__); }while(0)
#define MBMLOGD(...) do { if ((NULL != context) && (context->loglevel <= ANDROID_LOG_DEBUG)) ALOGD(__VA_ARGS__); }while(0)
#define MBMLOGV(...) do { if ((NULL != context) && (context->loglevel <= ANDROID_LOG_VERBOSE)) ALOGV(__VA_ARGS__); }while(0)

#define ENTER MBMLOGV("%s: enter", __FUNCTION__)
#define EXIT MBMLOGV("%s: exit", __FUNCTION__)

#endif                          /* end _LIBMBMGPS_LOG_H_ */

