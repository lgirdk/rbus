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

#include <rbus.h>
#include <rtmessage/rtTime.h>
#include <rbus_config.h>
#include "../common/test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

static int gDuration = 10;

int gEventCounts[3] = {0,0,0}; /* shared with subscribeEx.c*/

void printPassFail(int pass)
{
    printf("$$$$$$$$$$$$$$$$$$$\n$ %s\n$$$$$$$$$$$$$$$$$$$\n", pass ? "PASSED" : "FAILED");
}

bool testSubscribeHandleEvent( /*also shared with subscribeEx.c*/
    char* label,
    int eventIndex,
    rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t valBuff;
    rbusValue_t valIndex;
    char expectedBuff[32];
    bool pass;

    PRINT_TEST_EVENT(label, event, subscription);

    snprintf(expectedBuff, 32, "event %d data %d", eventIndex, gEventCounts[eventIndex]);

    valBuff = rbusObject_GetValue(event->data, "buffer");
    if(!valBuff)
    {
        printf("%s FAIL: value 'buffer' NULL\n", label);
        return false;
    }

    valIndex = rbusObject_GetValue(event->data, "index");
    if(!valIndex)
    {
        printf("%s FAIL: value 'index' NULL\n", label);
        return false;
    }

    pass = (strcmp(rbusValue_GetString(valBuff, NULL), expectedBuff) == 0 && 
            rbusValue_GetInt32(valIndex) == gEventCounts[eventIndex]);

    printf("%s %s: expect=[buffer:\"%s\" index:%d] actual=[buffer:\"%s\" index:%d]\n",
        label, pass ? "PASS" : "FAIL", expectedBuff, gEventCounts[eventIndex], 
        rbusValue_GetString(valBuff, NULL), rbusValue_GetInt32(valIndex));

    printPassFail(pass);

    gEventCounts[eventIndex]++;

    return pass;
}

static void handler1(
    rbusHandle_t handle,
    rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    (void)(handle);
    TALLY(testSubscribeHandleEvent("_test_Subscribe handle1", 0, event, subscription));
}

static void handler2(
    rbusHandle_t handle,
    rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    (void)(handle);
    TALLY(testSubscribeHandleEvent("_test_Subscribe handle2", 1, event, subscription));
}

static void handlerProviderNotFound1(
    rbusHandle_t handle,
    rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    (void)(handle);
    PRINT_TEST_EVENT("handlerProviderNotFound1", event, subscription);
    gEventCounts[2]++;
}


static rtTime_t asyncStartTime;
static int asyncExpectedElapseMin;
static int asyncExpectedElapseMax;
static rbusError_t asyncExpectedError;
static int asyncSuccess;

static void handlerAsyncSub(
    rbusHandle_t handle, 
    rbusEventSubscription_t* subscription,
    rbusError_t error)
{
    int elapsed = rtTime_Elapsed(&asyncStartTime, NULL);
    int success;

    if(error == asyncExpectedError &&
       elapsed >= asyncExpectedElapseMin &&
       elapsed <= asyncExpectedElapseMax)
    {
        success = 1;
    }
    else
    {
        success = 0;
    }

    printf("_test_Subscribe async handler %s with timeout %d elapsed=%d rc=%d %s\n", 
        subscription->eventName, asyncExpectedElapseMin/1000, elapsed, error, success ? "PASS" : "FAIL");
    
    asyncSuccess = success;

    (void)(handle);
}


int getDurationSubscribe()
{
    return gDuration;
}

void testSubscribe(rbusHandle_t handle, int* countPass, int* countFail)
{
    int rc = RBUS_ERROR_SUCCESS;
    char* data[2] = { "My Data 1", "My Data2" };
    int countDown = gDuration;
    rtTime_t tm;
    int elapsed;
    int success;
    const int timeout = 30;

    //changing from default so the takes doesn't take 10 minutes for async sub to complete
    rbusConfig_Get()->subscribeTimeout = timeout * 1000; /*convert seconds to miliseconds*/

    printf("\nTEST subscribe to event1");
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.Event1!", handler1, data[0], 0);
    TALLY(success = rc == RBUS_ERROR_SUCCESS);
    printf("_test_Subscribe rbusEvent_Subscribe Event1 %s rc=%d\n", success ? "PASS":"FAIL", rc);
    printPassFail(success);

    printf("\nTEST subscribe to event2");
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.Event2!", handler2, data[1], 0);
    TALLY(success = rc == RBUS_ERROR_SUCCESS);
    printf("_test_Subscribe rbusEvent_Subscribe Event2 %s rc=%d\n", success ? "PASS":"FAIL", rc);
    printPassFail(success);

    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.ErrorSubHandlerEvent!", handler2, data[1], 0);
    TALLY(success = rc == RBUS_ERROR_ACCESS_NOT_ALLOWED);
    printf("_test_Subscribe rbusEvent_Subscribe Event2 %s rc=%d\n", success ? "PASS":"FAIL", rc);
    printPassFail(success);

    while(countDown > gDuration/2)
    {
        sleep(1);
        countDown--;
    }

    rc = rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event2!");
    TALLY(success = rc == RBUS_ERROR_SUCCESS);
    printf("_test_Subscribe rbusEvent_Unsubscribe Event2 %s rc=%d\n", success ? "PASS" : "FAIL", rc);
    printPassFail(success);

    while(countDown > 0)
    {
        sleep(1);
        countDown--;
    }
 
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event1!");
    TALLY(success = rc == RBUS_ERROR_SUCCESS);
    printf("_test_Subscribe rbusEvent_Unsubscribe Event1 %s rc=%d\n", success ? "PASS" : "FAIL", rc);
    printPassFail(success);

    /*test negative cases*/
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.NonExistingEvent1!", handler1, data[0], 0);
    TALLY(success = rc != RBUS_ERROR_SUCCESS);
    printf("_test_Subscribe rbusEvent_Subscribe NonExistingEvent1 %s rc=%d\n", success ? "PASS" : "FAIL", rc);
    printPassFail(success);

    rc = rbusEvent_Unsubscribe(handle, "Device.TestProvider.NonExistingEvent1!");
    TALLY(success = rc != RBUS_ERROR_SUCCESS);
    printf("_test_Subscribe rbusEvent_Unsubscribe NonExistingEvent1 %s rc=%d\n", success ? "PASS" : "FAIL", rc);
    printPassFail(success);

    /*
     *  Test passing timeout value
     */

    printf("\nTEST subscribe with timeout that should succeed and return immediately");
    rtTime_Now(&tm);
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.Event1!", handler1, data[0], 20);
    elapsed = rtTime_Elapsed(&tm, NULL);
    success = rc == RBUS_ERROR_SUCCESS && elapsed < 500;//giving it 500 miliseconds to succeed
    printf("_test_Subscribe rbusEvent_Subscribe Event1 with timeout 20 elapsed=%d rc=%d %s\n", elapsed, rc, success ? "PASS" : "FAIL");
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event1!");
    TALLY(success);
    printPassFail(success);

    printf("\nTEST subscribe with default timeout that should succeed and return immediately");
    rtTime_Now(&tm);
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.Event1!", handler1, data[0], -1);
    elapsed = rtTime_Elapsed(&tm, NULL);
    success = rc == RBUS_ERROR_SUCCESS && elapsed < 500;//giving it 500 miliseconds to succeed
    printf("_test_Subscribe rbusEvent_Subscribe Event1 with default timeout 20 elapsed=%d rc=%d %s\n", elapsed, rc, success ? "PASS" : "FAIL");
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event1!");
    TALLY(success);
    printPassFail(success);

    printf("\nTEST can get provider specific error using default timeout");
    rtTime_Now(&tm);
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.ErrorSubHandlerEvent!", handler1, data[0], -1);
    elapsed = rtTime_Elapsed(&tm, NULL);
    success = rc == RBUS_ERROR_ACCESS_NOT_ALLOWED && elapsed < 500;//giving it 500 miliseconds to succeed
    printf("_test_Subscribe rbusEvent_Subscribe Event1 with default timeout 20 elapsed=%d rc=%d %s\n", elapsed, rc, success ? "PASS" : "FAIL");
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.ErrorSubHandlerEvent!");
    TALLY(success);
    printPassFail(success);

    printf("\nTEST subscribe with timeout that should fail after the timeout reached");
    rtTime_Now(&tm);
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.NonExistingEvent1!", handler1, data[0], timeout);
    elapsed = rtTime_Elapsed(&tm, NULL);
    success = rc == RBUS_ERROR_TIMEOUT && elapsed >= (timeout * 1000) && elapsed <= ((timeout+1) * 1000);/*between 20 and 21 seconds is ok*/
    printf("_test_Subscribe rbusEvent_Subscribe NonExistingEvent1 with timeout 20 elapsed=%d rc=%d %s\n", elapsed, rc, success ? "PASS" : "FAIL");
    TALLY(success);
    printPassFail(success);

    printf("\nTEST subscribe with default timeout that should fail after the default timeout reached");
    rtTime_Now(&tm);
    rc = rbusEvent_Subscribe(handle, "Device.TestProvider.NonExistingEvent1!", handler1, data[0], -1);
    elapsed = rtTime_Elapsed(&tm, NULL);
    success = rc == RBUS_ERROR_TIMEOUT && elapsed >= (timeout * 1000) && elapsed <= ((timeout+1) * 1000);/*between 30 and 31 seconds is ok*/
    printf("_test_Subscribe rbusEvent_Subscribe NonExistingEvent1 with default timeout 30 elapsed=%d rc=%d %s\n", elapsed, rc, success ? "PASS" : "FAIL");
    TALLY(success);
    printPassFail(success);

    /*
     *  Test async subscribe with timeout value
     */

    printf("\nTEST async subscribe with timeout succeeds and returns immediately\n");
    rtTime_Now(&asyncStartTime);
    asyncExpectedElapseMin = 0;
    asyncExpectedElapseMax = 100;
    asyncExpectedError = RBUS_ERROR_SUCCESS;
    asyncSuccess = -1;
    rc = rbusEvent_SubscribeAsync(handle, "Device.TestProvider.Event1!", handler1, handlerAsyncSub, data[0], 20);
    while(asyncSuccess == -1)
        usleep(100);
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event1!");
    TALLY(asyncSuccess);
    printPassFail(success);
    
    printf("\nTEST async subscribe with default timeout succeeds and returns immediately\n");
    rtTime_Now(&asyncStartTime);
    asyncExpectedElapseMin = 0;
    asyncExpectedElapseMax = 100;
    asyncExpectedError = RBUS_ERROR_SUCCESS;
    asyncSuccess = -1;
    rc = rbusEvent_SubscribeAsync(handle, "Device.TestProvider.Event1!", handler1, handlerAsyncSub, data[0], -1);
    while(asyncSuccess == -1)
        usleep(100);
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event1!");
    TALLY(asyncSuccess);
    printPassFail(success);

    printf("\nTEST async subscribe can get provider specific error\n");
    rtTime_Now(&asyncStartTime);
    asyncExpectedElapseMin = 0;
    asyncExpectedElapseMax = 100;
    asyncExpectedError = RBUS_ERROR_ACCESS_NOT_ALLOWED;
    asyncSuccess = -1;
    rc = rbusEvent_SubscribeAsync(handle, "Device.TestProvider.ErrorSubHandlerEvent!", handler1, handlerAsyncSub, data[0], -1);
    while(asyncSuccess == -1)
        usleep(100);
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.ErrorSubHandlerEvent!");
    TALLY(asyncSuccess);
    printPassFail(success);

    printf("\nTEST subscribe with default timeout fails after the default timeout reached\n");
    rtTime_Now(&asyncStartTime);
    asyncExpectedElapseMin = timeout * 1000;
    asyncExpectedElapseMax = (timeout+1) * 1000;
    asyncExpectedError = RBUS_ERROR_TIMEOUT;
    asyncSuccess = -1;
    rc = rbusEvent_SubscribeAsync(handle, "Device.TestProvider.NonExistingEvent1!", handler1, handlerAsyncSub, data[0], -1);
    while(asyncSuccess == -1)
        usleep(100);
    TALLY(asyncSuccess);
    printPassFail(success);

    printf("\nTEST subscribe with timeout fails after the default timeout reached\n");
    rtTime_Now(&asyncStartTime);
    asyncExpectedElapseMin = timeout * 1000;
    asyncExpectedElapseMax = (timeout+1) * 1000;
    asyncExpectedError = RBUS_ERROR_TIMEOUT;
    asyncSuccess = -1;
    rc = rbusEvent_SubscribeAsync(handle, "Device.TestProvider.NonExistingEvent1!", handler1, handlerAsyncSub, data[0], timeout);
    while(asyncSuccess == -1)
        usleep(100);
    TALLY(asyncSuccess);     
    printPassFail(success);

    printf("\nTEST async subscribe can succeed after provider comes up\n");
    rbus_setInt(handle, "Device.TestProvider.TestProviderNotFound", 1);
    rtTime_Now(&asyncStartTime);
    asyncExpectedElapseMin = 20000; /*rbusTestProvider will register the event after 20 seconds*/
    asyncExpectedElapseMax = 31000; /*retrier might be sleeping when it happens but should wake up by the timeout*/
    asyncExpectedError = RBUS_ERROR_SUCCESS;
    asyncSuccess = -1;
    rc = rbusEvent_SubscribeAsync(handle, "Device.TestProvider.ProviderNotFoundEvent1!", handlerProviderNotFound1, handlerAsyncSub, data[0], timeout);
    while(asyncSuccess == -1)
        usleep(100);
    TALLY(asyncSuccess);
    printPassFail(success);
    sleep(5);
    rbusEvent_Unsubscribe(handle, "Device.TestProvider.ProviderNotFoundEvent1!");
    TALLY(success = gEventCounts[2] == 5);
    printPassFail(success);

    rbusEvent_Unsubscribe(handle, "Device.TestProvider.Event1");

    *countPass = gCountPass;
    *countFail = gCountFail;
    PRINT_TEST_RESULTS("test_Subscribe");
}
