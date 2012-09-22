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
 *         Rickard Bellini <rickard.bellini@ericsson.com>
 */

#ifndef MBM_SERVICE_HANDLER_H
#define MBM_SERVICE_HANDLER_H 1

#define CMD_DOWNLOAD_PGPS_DATA 1
#define CMD_SERVICE_QUIT 2
#define CMD_SEND_ALL_INFO 3

int service_handler_init(int);
int service_handler_stop(void);
void service_handler_send_message (char cmd, char *msg);

#endif /* MBM_SERVICE_HANDLER_H */
