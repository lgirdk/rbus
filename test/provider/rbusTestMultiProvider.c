/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <rbus.h>
#include "../common/runningParamHelper.h"

int subscribes[5] = {-1,-1,-1,-1,-1};

rbusError_t getHandler(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t* opts)
{
    static int counter = 0;

    char const* name = rbusProperty_GetName(property);

    (void)handle;
    (void)opts;

    if( strcmp(name, "Device.MultiProvider1.Param") == 0 ||
        strcmp(name, "Device.MultiProvider2.Param") == 0 ||
        strcmp(name, "Device.MultiProvider3.Param") == 0 ||
        strcmp(name, "Device.MultiProvider4.Param") == 0 ||
        strcmp(name, "Device.MultiProvider5.Param") == 0)
    {
        char sVal[256];
        snprintf(sVal, 256, "param=%s counter=%d", name, ++counter);

        rbusValue_t value;
        rbusValue_Init(&value);
        rbusValue_SetString(value, sVal);
        rbusProperty_SetValue(property, value);
        rbusValue_Release(value);

        printf("_test_:getHandler result:SUCCESS value:'%s'\n", sVal);
        return RBUS_ERROR_SUCCESS;
    }
    else
    {
        printf("_test_:getHandler result:FAIL error:'unexpected param' param:'%s'\n", name);
    }

    return RBUS_ERROR_BUS_ERROR;
}

rbusError_t setHandler(rbusHandle_t handle, rbusProperty_t property, rbusSetHandlerOptions_t* opts)
{
    char const* name;
    char* sVal;
    rbusValue_t value;

    (void)handle;
    (void)opts;

    name = rbusProperty_GetName(property);
    value = rbusProperty_GetValue(property);

    if(value)
    {
        if( strcmp(name, "Device.MultiProvider1.Param") == 0 ||
            strcmp(name, "Device.MultiProvider2.Param") == 0 ||
            strcmp(name, "Device.MultiProvider3.Param") == 0 ||
            strcmp(name, "Device.MultiProvider4.Param") == 0 ||
            strcmp(name, "Device.MultiProvider5.Param") == 0)
        {
            if(rbusValue_GetType(value) == RBUS_STRING)
            {
                sVal=rbusValue_ToString(value, 0,0);
                printf("_test_:setHandler result:SUCCESS param='%s' value='%s'\n", name, sVal);
                free(sVal);
                return RBUS_ERROR_SUCCESS;
            }
            else
            {
                printf("_test_:setHandler result:FAIL error:'unexpected type %d'\n", rbusValue_GetType(value));
            }
        }
        else
        {
            printf("_test_:getHandler result:FAIL error:'no param matched' name='%s'\n", name);
        }
    }
    else
    {
        printf("_test_:getHandler result:FAIL value=NULL name='%s'\n", name);
    }

    return RBUS_ERROR_BUS_ERROR;
}

rbusError_t eventSubHandler(rbusHandle_t handle, rbusEventSubAction_t action, const char* eventName, rbusFilter_t filter, int32_t interval, bool* autoPublish)
{
    (void)handle;
    (void)filter;
    (void)interval;
    (void)autoPublish;

    if(strcmp(eventName, "Device.MultiProvider1.Event!") == 0)
    {   
        subscribes[0] = action;
    }
    else
    if(strcmp(eventName, "Device.MultiProvider2.Event!") == 0)
    {
        subscribes[1] = action;
    }
    else
    if(strcmp(eventName, "Device.MultiProvider3.Event!") == 0)
    {
        subscribes[2] = action;
    }
    else
    if(strcmp(eventName, "Device.MultiProvider4.Event!") == 0)
    {
        subscribes[3] = action;
    }
    else
    if(strcmp(eventName, "Device.MultiProvider5.Event!") == 0)
    {
        subscribes[4] = action;
    }

    printf("_test_:eventSubHandler event=%s action=%s\n", eventName, action == RBUS_EVENT_ACTION_SUBSCRIBE ? "subscribe" : "unsubscribe");

    return RBUS_ERROR_SUCCESS;
}

int main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    rbusHandle_t handles[5] = {NULL};

    /*create 5 components which act as provider for 1 param and 1 event each*/
    char* componentNames[] = {
        "MultiProvider1",
        "MultiProvider2",
        "MultiProvider3",
        "MultiProvider4",
        "MultiProvider5"
    };

    rbusDataElement_t dataElements[5][2] = {
        {
            {"Device.MultiProvider1.Param", RBUS_ELEMENT_TYPE_PROPERTY, {getHandler,setHandler,NULL,NULL,NULL,NULL}},
            {"Device.MultiProvider1.Event!", RBUS_ELEMENT_TYPE_EVENT, {NULL,NULL,NULL,NULL,eventSubHandler,NULL}}
        },
        {
            {"Device.MultiProvider2.Param", RBUS_ELEMENT_TYPE_PROPERTY, {getHandler,setHandler,NULL,NULL,NULL,NULL}},
            {"Device.MultiProvider2.Event!", RBUS_ELEMENT_TYPE_EVENT, {NULL,NULL,NULL,NULL,eventSubHandler,NULL}}
        },
        {
            {"Device.MultiProvider3.Param", RBUS_ELEMENT_TYPE_PROPERTY, {getHandler,setHandler,NULL,NULL,NULL,NULL}},
            {"Device.MultiProvider3.Event!", RBUS_ELEMENT_TYPE_EVENT, {NULL,NULL,NULL,NULL,eventSubHandler,NULL}}
        },
        {
            {"Device.MultiProvider4.Param", RBUS_ELEMENT_TYPE_PROPERTY, {getHandler,setHandler,NULL,NULL,NULL,NULL}},
            {"Device.MultiProvider4.Event!", RBUS_ELEMENT_TYPE_EVENT, {NULL,NULL,NULL,NULL,eventSubHandler,NULL}}
        },
        {
            {"Device.MultiProvider5.Param", RBUS_ELEMENT_TYPE_PROPERTY, {getHandler,setHandler,NULL,NULL,NULL,NULL}},
            {"Device.MultiProvider5.Event!", RBUS_ELEMENT_TYPE_EVENT, {NULL,NULL,NULL,NULL,eventSubHandler,NULL}}
        }
    };

    int rc = RBUS_ERROR_SUCCESS;
    int i;
    int eventCount = 0;

    printf("provider: opening 5 handles\n");

    for(i = 0; i < 5; ++i)
    {
        rc = rbus_open(&handles[i], componentNames[i]);
        printf("provider: rbus_open %s=%d\n", componentNames[i], rc);
        if(rc == RBUS_ERROR_SUCCESS)
        {
            printf("_test_:rbus_open result:SUCCESS component:%s\n", componentNames[i]);
        }
        else
        {
            printf("_test_:rbus_open result:FAIL component:%s rc:%d\n", componentNames[i], rc);
            handles[i] = NULL;
            if(i == 0)
            {
                printf("provider: failed to open 1st handle\n");
                return 1;
            }
            else
            {
                continue;
            }
        }

        rc = rbus_regDataElements(handles[i], 2, dataElements[i]);
        printf("provider: rbus_regDataElements %s=%d\n", componentNames[i], rc);
        if(rc == RBUS_ERROR_SUCCESS)
        {
            printf("_test_:rbus_regDataElements result:SUCCESS component:%s\n", componentNames[i]);
        }
        else
        {
            printf("_test_:rbus_regDataElements result:FAIL component:%s rc:%d\n", componentNames[i], rc);
        }
    }

    if(runningParamProvider_Init(handles[0], "Device.MultiProvider.TestRunning") != RBUS_ERROR_SUCCESS)
    {
        printf("provider: failed to register running param\n");
        goto exit1;
    }
    printf("provider: waiting for consumer to start\n");
    while (!runningParamProvider_IsRunning())
    {
        usleep(10000);
    }

    printf("provider: running\n");
    while (runningParamProvider_IsRunning())
    {
        char buffer[64];
        eventCount++;
        sleep(1);

        printf("provider: publishing events for 5 handles\n");
        for(i = 0; i < 5; ++i)
        {
            if(handles[i])
            {
                snprintf(buffer, 64, "from=%s count=%d", componentNames[i], eventCount);

                rbusValue_t value;
                rbusObject_t data;

                rbusValue_Init(&value);
                rbusValue_SetString(value, buffer);

                rbusObject_Init(&data, NULL);
                rbusObject_SetValue(data, "value", value);

                rbusEvent_t event = {0};
                event.name = dataElements[i][1].name;
                event.data = data;
                event.type = RBUS_EVENT_GENERAL;

                rc = rbusEvent_Publish(handles[i], &event);

                rbusValue_Release(value);
                rbusObject_Release(data);
                
                printf("provider: rbusEvent_Publish %s=%d\n", dataElements[i][1].name, rc);
                if(rc == RBUS_ERROR_SUCCESS)
                {
                    printf("_test_:rbusEvent_Publish result:SUCCESS event:%s\n", dataElements[i][1].name);
                }
                else
                {
                    printf("_test_:rbusEvent_Publish result:FAIL event:%s rc=%d\n", dataElements[i][1].name, rc);
                }
            }
        }
    }

exit1:

    printf("provider: closing 5 handles\n");

    for(i = 0; i < 5; ++i)
    {
        if(handles[i])
        {
            rc = rbus_unregDataElements(handles[i], 2, dataElements[i]);
            printf("provider: rbusEventProvider_Unregister %s=%d\n", componentNames[i], rc);
            if(rc == RBUS_ERROR_SUCCESS)
            {
                printf("_test_:rbusEventProvider_Unregister result:SUCCESS component:%s\n", componentNames[i]);
            }
            else
            {
                printf("_test_:rbusEventProvider_Unregister result:FAIL component:%s rc:%d\n", componentNames[i], rc);
            }

            rc = rbus_close(handles[i]);
            printf("provider: rbus_close %s=%d\n", componentNames[i], rc);
            if(rc == RBUS_ERROR_SUCCESS)
            {
                printf("_test_:rbus_close result:SUCCESS component:%s\n", componentNames[i]);
            }
            else
            {
                printf("_test_:rbus_close result:FAIL component:%s rc:%d\n", componentNames[i], rc);
            }
        }
    }

    return rc;
}
