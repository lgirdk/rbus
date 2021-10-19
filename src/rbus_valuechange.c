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

/*
    Value-Change Detection:
    Simple API that allows you to add/remove parameters you wish to check value-change for.
    Uses a single thread to poll parameter values across all rbus handles.
    The thread is started on first param added and stopped on last param removed.
    A poling period (default 30 seconds) helps to limit the cpu usage.
    Runs in the provider process, so the value are got with direct callbacks and not over the network.
    The technique is simple:
    1) when a param is added, get and cache its current value.
    2) on a background thread, periodically get the latest value and compare to cached value.
    3) if the value has change, publish an event.
*/

#define _GNU_SOURCE 1 //needed for pthread_mutexattr_settype

#include "rbus_valuechange.h"
#include "rbus_config.h"
#include "rbus_handle.h"
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <rtmessage/rtVector.h>
#include <rtmessage/rtTime.h>

#define ERROR_CHECK(CMD) \
{ \
  int err; \
  if((err=CMD) != 0) \
  { \
    RBUSLOG_ERROR("Error %d:%s running command " #CMD, err, strerror(err)); \
  } \
}
#define LOCK() ERROR_CHECK(pthread_mutex_lock(&gVC->mutex))
#define UNLOCK() ERROR_CHECK(pthread_mutex_unlock(&gVC->mutex))

typedef struct ValueChangeDetector_t
{
    int              running;
    rtVector         params;
    pthread_mutex_t  mutex;
    pthread_t        thread;
    pthread_cond_t   cond;
} ValueChangeDetector_t;

typedef struct ValueChangeRecord
{
    rbusHandle_t handle;    //needed when calling rbus_getHandler and rbusEvent_Publish
    elementNode const* node;    //used to call the rbus_getHandler is contains
    rbusProperty_t property;    //the parameter with value that gets cached
} ValueChangeRecord;

ValueChangeDetector_t* gVC = NULL;

static void rbusValueChange_Init()
{
    pthread_mutexattr_t attrib;
    pthread_condattr_t cattrib;

    RBUSLOG_DEBUG("%s", __FUNCTION__);

    if(gVC)
        return;

    gVC = malloc(sizeof(struct ValueChangeDetector_t));

    gVC->running = 0;
    gVC->params = NULL;

    rtVector_Create(&gVC->params);

    ERROR_CHECK(pthread_mutexattr_init(&attrib));
    ERROR_CHECK(pthread_mutexattr_settype(&attrib, PTHREAD_MUTEX_ERRORCHECK));
    ERROR_CHECK(pthread_mutex_init(&gVC->mutex, &attrib));

    ERROR_CHECK(pthread_condattr_init(&cattrib));
    ERROR_CHECK(pthread_condattr_setclock(&cattrib, CLOCK_MONOTONIC));
    ERROR_CHECK(pthread_cond_init(&gVC->cond, &cattrib));
    ERROR_CHECK(pthread_condattr_destroy(&cattrib));
}

static void vcParams_Free(void* p)
{
    ValueChangeRecord* rec = (ValueChangeRecord*)p;
    rbusProperty_Release(rec->property);
    free(rec);
}

static ValueChangeRecord* vcParams_Find(const elementNode* paramNode)
{
    size_t i;
    for(i=0; i < rtVector_Size(gVC->params); ++i)
    {
        ValueChangeRecord* rec = (ValueChangeRecord*)rtVector_At(gVC->params, i);
        if(rec && rec->node == paramNode)
            return rec;
    }
    return NULL;
}

static void* rbusValueChange_pollingThreadFunc(void *userData)
{
    (void)(userData);
    RBUSLOG_DEBUG("%s: start", __FUNCTION__);
    LOCK();
    while(gVC->running)
    {
        size_t i;
        int err;
        rtTime_t timeout;
        rtTimespec_t ts;

        rtTime_Later(NULL, rbusConfig_Get()->valueChangePeriod, &timeout);
        
        err = pthread_cond_timedwait(&gVC->cond, 
                                    &gVC->mutex, 
                                    rtTime_ToTimespec(&timeout, &ts));

        if(err != 0 && err != ETIMEDOUT)
        {
            RBUSLOG_ERROR("Error %d:%s running command pthread_cond_timedwait", err, strerror(err));
        }
        
        if(!gVC->running)
        {
            break;
        }

        for(i=0; i < rtVector_Size(gVC->params); ++i)
        {
            rbusProperty_t property;
            rbusValue_t newVal, oldVal;

            ValueChangeRecord* rec = (ValueChangeRecord*)rtVector_At(gVC->params, i);
            if(!rec)
                continue;

            rbusProperty_Init(&property,rbusProperty_GetName(rec->property), NULL);

            rbusGetHandlerOptions_t opts;
            memset(&opts, 0, sizeof(rbusGetHandlerOptions_t));
            opts.requestingComponent = "valueChangePollThread";

            int result = rec->node->cbTable.getHandler(rec->handle, property, &opts);

            if(result != RBUS_ERROR_SUCCESS)
            {
                RBUSLOG_WARN("%s: failed to get current value of %s", __FUNCTION__, rbusProperty_GetName(property));
                continue;
            }

            char* sValue = rbusValue_ToString(rbusProperty_GetValue(property), NULL, 0);
            RBUSLOG_DEBUG("%s: %s=%s", __FUNCTION__, rbusProperty_GetName(property), sValue);
            free(sValue);

            newVal = rbusProperty_GetValue(property);
            oldVal = rbusProperty_GetValue(rec->property);

            if(rbusValue_Compare(newVal, oldVal))
            {
                rbusSubscription_t* subscription;
                rbusValue_t byVal = NULL;
                rtListItem li;

                RBUSLOG_INFO("%s: value change detected for %s", __FUNCTION__, rbusProperty_GetName(rec->property));

                /*
                    mrollins: I added a 'filter=true|false' property to the event data when a filter it triggered.
                    This would let the consumer know if their filter got triggered by the property's value crossing
                    into the threshold (filter=true) or out of the threshold (filter=false). 
                    Although useful, it creates complication if multiple consumers subscribe with different filters.
                    When we publish below with rbusEvent_Publish, all subscribers to the property will get an event.  
                    But we add this 'filter' property to the event data if a filter was triggered and this would confuse any
                    additional subscriber on this property which had a filter that wasn't triggered or triggered in the opposite way.

                    Example fail case: consumer A has filter 1 (val > 0), and consumer B has filter 2 (val < 0).  
                    Now assume filter 1 gets crossed into (val goes from 0 to 1).
                    We publish the event with a 'filter=true' (meaning it crossed into). 
                    Consumer A gets the correct event.  Consumer B gets the same event with 'filter=true' and believes
                    that the val went below 0 -- which is wrong. 

                    Note that this could be fixed by publishing to specific subscribers from here; however, if autoPublish is
                    false, we don't have anything in the API to allow a provider to know about subscribers and to publish to 
                    specific providers.
                */

                /* The "by" field is set to the component's name which made the last value change.
                   The source of a value-change could be an external component calling rbus_set or the provider internally updating
                   the value.  changeComp/changeTime are updated through the rbus_set path, but not through the provider internal path.
                   We must deduce if the provider has updated the value and reflect that change to the changeComp/changeTime, right here.
                   If we don't have a changeComp or we do but the changeTime is older then the current polling period,
                   then we know it was the provider who updated the value we are now detecting.
                */
                if(rec->node->changeComp == NULL || 
                   (rtTime_Elapsed(&rec->node->changeTime, NULL) >= rbusConfig_Get()->valueChangePeriod &&
                   strcmp(rec->handle->componentName, rec->node->changeComp) == 0))
                {
                    printf("VC detected provider-side value-change oldcomp=%s elapsed=%d period=%d\n", rec->node->changeComp, rtTime_Elapsed(&rec->node->changeTime, NULL), rbusConfig_Get()->valueChangePeriod);
                    setPropertyChangeComponent((elementNode*)rec->node, rec->handle->componentName);
                }
                rbusValue_Init(&byVal);
                rbusValue_SetString(byVal, rec->node->changeComp);
                
                rtList_GetFront(rec->node->subscriptions, &li);
                while(li)
                {
                    bool publish = true;
                    rbusValue_t filterResult = NULL;

                    rtListItem_GetData(li, (void**)&subscription);

                    /* if the subscriber has a filter we check the filter to determine if we publish.
                       if the subscriber does not have a filter, we publish always*/
                    if(subscription->filter)
                    {
                        /*We publish an event only when the value crosses the filter threshold boundary.
                          When the value crosses into the threshold we publish a single event signally the filter started matching.
                          When the value crosses out of the threshold we publish a single event signally the filter stopped matching.
                          We do not publish continuous events while the filter continues to match. The consumer can read the 'filter'
                          property from the event data to determine if the filter has started or stopped matching.  If the consumer
                          wants to get continuous value-change events, they can unsubscribe the filter and resubscribe without a filter*/

                        int newResult = rbusFilter_Apply(subscription->filter, newVal);
                        int oldResult = rbusFilter_Apply(subscription->filter, oldVal);

                        if(newResult != oldResult)
                        {
                            RBUSLOG_INFO("%s: filter matched for %s", __FUNCTION__, rbusProperty_GetName(rec->property));
                            /*set 'filter' to true/false implying that either the filter has started or stopped matching*/
                            rbusValue_Init(&filterResult);
                            rbusValue_SetBoolean(filterResult, newResult != 0);
                        }
                        else
                        {
                            publish =  false;
                        }
                    }
                    rtListItem_GetNext(li, &li);

                    if(publish)
                    {
                        rbusEvent_t event = {0};
                        rbusObject_t data;

                        rbusObject_Init(&data, NULL);
                        rbusObject_SetValue(data, "value", newVal);
                        rbusObject_SetValue(data, "oldValue", oldVal);
                        rbusObject_SetValue(data, "by", byVal);

                        if(filterResult)
                        {
                            rbusObject_SetValue(data, "filter", filterResult);
                            rbusValue_Release(filterResult);
                        }

                        RBUSLOG_INFO ("%s: value change detected for %s", __FUNCTION__, rbusProperty_GetName(rec->property));

                        event.name = rbusProperty_GetName(rec->property);
                        event.data = data;
                        event.type = RBUS_EVENT_VALUE_CHANGED;

                        result = rbusEvent_Publish(rec->handle, &event);

                        rbusObject_Release(data);

                        if(result != RBUS_ERROR_SUCCESS)
                        {
                            RBUSLOG_WARN("%s: rbusEvent_Publish failed with result=%d", __FUNCTION__, result);
                        }

                        /*
                            Break out of the node's subscriptions loop so that we only publish 1 event for this property.
                                
                            We only publish 1 time, even though there could be multiple subscribers whose filters get triggered.
                            If there are multiple providers with different filters then it can be a problem because only the first 
                            consumer who's filter triggered will have the corrent 'filter' property in the event data.  The other 
                            subscribers with different filters will get confused.   Right now we are just assuming only one subscriber
                            per property, so we may have to come back and fix the code to handle multiple subscribers correctly at some point.
                        */
                        break;
                    }
                }
            
                if(byVal)
                    rbusValue_Release(byVal);

                /*update the record's property with new value*/
                rbusProperty_SetValue(rec->property, rbusProperty_GetValue(property));
                rbusProperty_Release(property);
            }
            else
            {
                RBUSLOG_DEBUG("%s: value change not detected for %s", __FUNCTION__, rbusProperty_GetName(rec->property));
                rbusProperty_Release(property);
            }
        }
    }
    UNLOCK();
    RBUSLOG_DEBUG("%s: stop", __FUNCTION__);
    return NULL;
}

void rbusValueChange_AddPropertyNode(rbusHandle_t handle, elementNode* propNode)
{
    ValueChangeRecord* rec;

    RBUSLOG_DEBUG("%s: %s", __FUNCTION__, propNode->fullName);

    if(!gVC)
    {
        rbusValueChange_Init();
    }

    /* basic sanity tests */    
    assert(propNode);
    if(!propNode)
    {
        RBUSLOG_WARN("%s: propNode NULL error", __FUNCTION__);
        return;
    }
    assert(propNode->type == RBUS_ELEMENT_TYPE_PROPERTY);
    if(propNode->type != RBUS_ELEMENT_TYPE_PROPERTY)
    {
        RBUSLOG_WARN("%s: propNode type %d error", __FUNCTION__, propNode->type);
        return;
    }
    assert(propNode->cbTable.getHandler);
    if(!propNode->cbTable.getHandler)
    {
        RBUSLOG_WARN("%s: propNode getHandler NULL error", __FUNCTION__);
        return;
    }

    /* only add the property if its not already in the list */

    LOCK();//############ LOCK ############

    rec = vcParams_Find(propNode);

    UNLOCK();//############ UNLOCK ############

    if(!rec)
    {
        rec = (ValueChangeRecord*)malloc(sizeof(ValueChangeRecord));
        rec->handle = handle;
        rec->node = propNode;

        rbusProperty_Init(&rec->property, propNode->fullName, NULL);

        rbusGetHandlerOptions_t opts;
        memset(&opts, 0, sizeof(rbusGetHandlerOptions_t));
        opts.requestingComponent = "valueChangePollThread";
        /*get and cache the current value
          the polling thread will periodically re-get and compare to detect value changes*/
        int result = propNode->cbTable.getHandler(handle, rec->property, &opts);

        if(result != RBUS_ERROR_SUCCESS)
        {
            RBUSLOG_WARN("%s: failed to get current value for %s as the node is not found", __FUNCTION__, propNode->fullName);
            vcParams_Free(rec);
            rec = NULL;
            return;
        }

        char* sValue;
        RBUSLOG_DEBUG("%s: %s=%s", __FUNCTION__, propNode->fullName, (sValue = rbusValue_ToString(rbusProperty_GetValue(rec->property), NULL, 0)));
        free(sValue);

        LOCK();//############ LOCK ############

        rtVector_PushBack(gVC->params, rec);

        /* start polling thread if needed */

        if(!gVC->running)
        {
            gVC->running = 1;
            pthread_create(&gVC->thread, NULL, rbusValueChange_pollingThreadFunc, NULL);
        }

        UNLOCK();//############ UNLOCK ############
    }
}

void rbusValueChange_RemovePropertyNode(rbusHandle_t handle, elementNode* propNode)
{
    ValueChangeRecord* rec;
    bool stopThread = false;

    (void)(handle);

    RBUSLOG_DEBUG("%s: %s", __FUNCTION__, propNode->fullName);

    if(!gVC)
    {
        return;
    }

    LOCK();//############ LOCK ############
    rec = vcParams_Find(propNode);
    if(rec)
    {
        rtVector_RemoveItem(gVC->params, rec, vcParams_Free);
        /* if there's nothing left to poll then shutdown the polling thread */
        if(gVC->running && rtVector_Size(gVC->params) == 0)
        {
            stopThread = true;
            gVC->running = 0;
        }
        else 
        {
            stopThread = false;
        }
    }
    else
    {
        RBUSLOG_WARN("%s: value change param not found: %s", __FUNCTION__, propNode->fullName);
    }
    UNLOCK();//############ UNLOCK ############
    if(stopThread)
    {
        ERROR_CHECK(pthread_cond_signal(&gVC->cond));
        ERROR_CHECK(pthread_join(gVC->thread, NULL));
    }
}

void rbusValueChange_CloseHandle(rbusHandle_t handle)
{
    RBUSLOG_DEBUG("%s", __FUNCTION__);

    if(!gVC)
    {
        return;
    }

    //remove all params for this bus handle
    size_t i = 0;
    while(i < rtVector_Size(gVC->params))
    {
        ValueChangeRecord* rec = (ValueChangeRecord*)rtVector_At(gVC->params, i);
        if(rec && rec->handle == handle)
        {
            rtVector_RemoveItem(gVC->params, rec, vcParams_Free);
        }
        else
        {
            //only i++ here because rtVector_RemoveItem does a right shift on all the elements after remove index
            i++; 
        }
    }

    //clean up everything once all params are removed
    //but check the size to ensure we do not clean up if params for other rbus handles exist
    if(rtVector_Size(gVC->params) == 0)
    {
        if(gVC->running)
        {
            gVC->running = 0;
            ERROR_CHECK(pthread_cond_signal(&gVC->cond));
            ERROR_CHECK(pthread_join(gVC->thread, NULL));
        }
        ERROR_CHECK(pthread_mutex_destroy(&gVC->mutex));
        ERROR_CHECK(pthread_cond_destroy(&gVC->cond));
        rtVector_Destroy(gVC->params, NULL);
        gVC->params = NULL;
        free(gVC);
        gVC = NULL;
    }
}

