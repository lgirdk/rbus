/*
 * If not stated otherwise in this file or this component's Licenses.txt file
 * the following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#ifndef __RBUS_EVENTS_H
#define __RBUS_EVENTS_H

#include "rbus_subscriptions.h"

#ifdef __cplusplus
extern "C" {
#endif

void rbusValueChange_SetPollingPeriod(int seconds);
void rbusValueChange_AddPropertyNode(rbusHandle_t handle, elementNode* propNode);
void rbusValueChange_RemovePropertyNode(rbusHandle_t handle, elementNode* propNode);
void rbusValueChange_Close(rbusHandle_t handle);

#ifdef __cplusplus
}
#endif
#endif