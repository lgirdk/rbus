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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <rtVector.h>
#include <rbus_core.h>
#include <rbus_marshalling.h>
#include <rbus_session_mgr.h>
#include <rbus.h>
#include "rbus_buffer.h"
#include "rbus_element.h"
#include "rbus_valuechange.h"
#include "rbus_subscriptions.h"

//******************************* MACROS *****************************************//
#define UNUSED1(a)              (void)(a)
#define UNUSED2(a,b)            UNUSED1(a),UNUSED1(b)
#define UNUSED3(a,b,c)          UNUSED1(a),UNUSED2(b,c)
#define UNUSED4(a,b,c,d)        UNUSED1(a),UNUSED3(b,c,d)
#define UNUSED5(a,b,c,d,e)      UNUSED1(a),UNUSED4(b,c,d,e)
#define UNUSED6(a,b,c,d,e,f)    UNUSED1(a),UNUSED5(b,c,d,e,f)

#define MAX_COMPS_PER_PROCESS           5
#define FALSE                           0
#define TRUE                            1
#define VALUE_CHANGE_POLLING_PERIOD     2 //seconds
//********************************************************************************//


//******************************* STRUCTURES *************************************//

struct _rbusHandle_t
{
    int             inUse;
    char*           componentName;
    elementNode*    elementRoot;
    rtVector        eventSubs; /* consumer side subscriptions FIXME - this needs to be an associative map instead of list/vector*/
    rbusSubscriptions_t subscriptions; /*provider side subscriptions */
};

#define comp_info struct _rbusHandle_t

comp_info comp_array[MAX_COMPS_PER_PROCESS] = {
    {0,"", NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL}
};

typedef enum _rbus_legacy_support
{
    RBUS_LEGACY_STRING = 0,    /**< Null terminated string                                           */
    RBUS_LEGACY_INT,           /**< Integer (2147483647 or -2147483648) as String                    */
    RBUS_LEGACY_UNSIGNEDINT,   /**< Unsigned Integer (ex: 4,294,967,295) as String                   */
    RBUS_LEGACY_BOOLEAN,       /**< Boolean as String (ex:"true", "false"                            */
    RBUS_LEGACY_DATETIME,      /**< ISO-8601 format (YYYY-MM-DDTHH:MM:SSZ) as String                 */
    RBUS_LEGACY_BASE64,        /**< Base64 representation of data as String                          */
    RBUS_LEGACY_LONG,          /**< Long (ex: 9223372036854775807 or -9223372036854775808) as String */
    RBUS_LEGACY_UNSIGNEDLONG,  /**< Unsigned Long (ex: 18446744073709551615) as String               */
    RBUS_LEGACY_FLOAT,         /**< Float (ex: 1.2E-38 or 3.4E+38) as String                         */
    RBUS_LEGACY_DOUBLE,        /**< Double (ex: 2.3E-308 or 1.7E+308) as String                      */
    RBUS_LEGACY_BYTE,
    RBUS_LEGACY_NONE
} rbusLegacyDataType_t;

typedef enum _rbus_legacy_returns {
    RBUS_LEGACY_ERR_SUCCESS = 100,
    RBUS_LEGACY_ERR_MEMORY_ALLOC_FAIL = 101,
    RBUS_LEGACY_ERR_FAILURE = 102,
    RBUS_LEGACY_ERR_NOT_CONNECT = 190,
    RBUS_LEGACY_ERR_TIMEOUT = 191,
    RBUS_LEGACY_ERR_NOT_EXIST = 192,
    RBUS_LEGACY_ERR_NOT_SUPPORT = 193,
    RBUS_LEGACY_ERR_OTHERS
} rbusLegacyReturn_t;

//********************************************************************************//

//******************************* INTERNAL FUNCTIONS *****************************//
static void rbusEventSubscription_free(void* p)
{
    rbusEventSubscription_t* sub = (rbusEventSubscription_t*)p;
    free((void*)sub->eventName);
    free(sub);
}

static rbusEventSubscription_t* rbusEventSubscription_find(rtVector eventSubs, char const* eventName)
{
    /*FIXME - convert to map */
    size_t i;
    for(i=0; i < rtVector_Size(eventSubs); ++i)
    {
        rbusEventSubscription_t* sub = (rbusEventSubscription_t*)rtVector_At(eventSubs, i);
        if(sub && !strcmp(sub->eventName, eventName))
            return sub;
    }
    rtLog_Warn("rbusEventSubscription_find error: can't find %s", eventName);
    return NULL;
}

static void _parse_rbusData_to_value (char const* pBuff, rbusLegacyDataType_t legacyType, rbusValue_t value)
{
    if (pBuff && value)
    {
        switch (legacyType)
        {
            case RBUS_LEGACY_STRING:
            {
                rbusValue_SetFromString(value, RBUS_STRING, pBuff);
                break;
            }
            case RBUS_LEGACY_INT:
            {
                rbusValue_SetFromString(value, RBUS_INT32, pBuff);
                break;
            }
            case RBUS_LEGACY_UNSIGNEDINT:
            {
                rbusValue_SetFromString(value, RBUS_UINT32, pBuff);
                break;
            }
            case RBUS_LEGACY_BOOLEAN:
            {
                rbusValue_SetFromString(value, RBUS_BOOLEAN, pBuff);
                break;
            }
            case RBUS_LEGACY_LONG:
            {
                rbusValue_SetFromString(value, RBUS_INT64, pBuff);
                break;
            }
            case RBUS_LEGACY_UNSIGNEDLONG:
            {
                rbusValue_SetFromString(value, RBUS_UINT64, pBuff);
                break;
            }
            case RBUS_LEGACY_FLOAT:
            {
                rbusValue_SetFromString(value, RBUS_SINGLE, pBuff);
                break;
            }
            case RBUS_LEGACY_DOUBLE:
            {
                rbusValue_SetFromString(value, RBUS_DOUBLE, pBuff);
                break;
            }
            case RBUS_LEGACY_BYTE:
            {
                rbusValue_SetBytes(value, (uint8_t*)pBuff, strlen(pBuff));
                break;
            }
            case RBUS_LEGACY_DATETIME:
            {
                rbusValue_SetFromString(value, RBUS_DATETIME, pBuff);
                break;
            }
            case RBUS_LEGACY_BASE64:
            {
                rtLog_Warn("RBUS_LEGACY_BASE64_TYPE: Base64 type was never used in CCSP so far. So, Rbus did not support it till now. Since this is the first Base64 query, please report to get it fixed.");
                rbusValue_SetString(value, pBuff);
                break;
            }
            default:
                break;
        }
    }
}

//*************************** SERIALIZE/DERIALIZE FUNCTIONS ***************************//
#define DEBUG_SERIALIZER 0

static void rbusValue_initFromMessage(rbusValue_t* value, rtMessage msg);
static void rbusValue_appendToMessage(char const* name, rbusValue_t value, rtMessage msg);
static void rbusProperty_initFromMessage(rbusProperty_t* property, rtMessage msg);
static void rbusPropertyList_initFromMessage(rbusProperty_t* prop, rtMessage msg);
static void rbusPropertyList_appendToMessage(rbusProperty_t prop, rtMessage msg);
static void rbusObject_initFromMessage(rbusObject_t* obj, rtMessage msg);
static void rbusObject_appendToMessage(rbusObject_t obj, rtMessage msg);
static void rbusEvent_updateFromMessage(rbusEvent_t* event, rtMessage msg);
static void rbusEvent_appendToMessage(rbusEvent_t* event, rtMessage msg);

static void rbusValue_initFromMessage(rbusValue_t* value, rtMessage msg)
{
    void const* data;
    uint32_t length;
    int type;
    char const* pBuffer = NULL;

    rbusValue_Init(value);

    rbus_PopInt32(msg, (int*) &type);
#if DEBUG_SERIALIZER
    rtLog_Info("> value pop type=%d", type);
#endif
    if(type>=RBUS_LEGACY_STRING && type<=RBUS_LEGACY_NONE)
    {
        rbus_PopString(msg, &pBuffer);
        rtLog_Debug("Received Param Value in string : [%s]", pBuffer);
        _parse_rbusData_to_value (pBuffer, type, *value);
    }
    else
    {
        if(type == RBUS_OBJECT)
        {
            rbusObject_t obj;
            rbusObject_initFromMessage(&obj, msg);
            rbusValue_SetObject(*value, obj);
            rbusObject_Release(obj);
        }
        else
        if(type == RBUS_PROPERTY)
        {
            rbusProperty_t prop;
            rbusPropertyList_initFromMessage(&prop, msg);
            rbusValue_SetProperty(*value, prop);
            rbusProperty_Release(prop);
        }
        else
        {
            int32_t ival;
            double fval;
            struct timeval tv;

            switch(type)
            {
                case RBUS_INT16:
                    rbus_PopInt32(msg, &ival);
                    rbusValue_SetInt16(*value, (int16_t)ival);
                    break;
                case RBUS_UINT16:
                    rbus_PopInt32(msg, &ival);
                    rbusValue_SetUInt16(*value, (uint16_t)ival);
                    break;
                case RBUS_INT32:
                    rbus_PopInt32(msg, &ival);
                    rbusValue_SetInt32(*value, (int32_t)ival);
                    break;
                case RBUS_UINT32:
                    rbus_PopInt32(msg, &ival);
                    rbusValue_SetUInt32(*value, (uint32_t)ival);
                    break;
                case RBUS_INT64:
                {
                    union UNION64
                    {
                        int32_t i32[2];
                        int64_t i64;
                    };
                    union UNION64 u;
                    rbus_PopInt32(msg, &u.i32[0]);
                    rbus_PopInt32(msg, &u.i32[1]);
                    rbusValue_SetInt64(*value, u.i64);
                    break;
                }
                case RBUS_UINT64:
                {
                    union UNION64
                    {
                        int32_t i32[2];
                        uint64_t u64;
                    };
                    union UNION64 u;
                    rbus_PopInt32(msg, &u.i32[0]);
                    rbus_PopInt32(msg, &u.i32[1]);
                    rbusValue_SetUInt64(*value, u.u64);
                    break;
                }
                case RBUS_SINGLE:
                    rbus_PopDouble(msg, &fval);
                    rbusValue_SetSingle(*value, (float)fval);
                    break;
                case RBUS_DOUBLE:
                    rbus_PopDouble(msg, &fval);
                    rbusValue_SetDouble(*value, (double)fval);
                    break;
                case RBUS_DATETIME:
                    rbus_PopInt32(msg, &ival);
                    tv.tv_sec = ival;
                    rbus_PopInt32(msg, &ival);
                    tv.tv_usec = ival;
                    rbusValue_SetTime(*value, &tv);
                    break;
                default:
                    rbus_PopBinaryData(msg, &data, &length);
                    rbusValue_SetTLV(*value, type, length, data);
                    break;
            }

#if DEBUG_SERIALIZER
            char* sv = rbusValue_ToString(*value,0,0);
            rtLog_Info("> value pop data=%s", sv);
            free(sv);
#endif
        }
    }
}

static void rbusProperty_initFromMessage(rbusProperty_t* property, rtMessage msg)
{
    char const* name;
    rbusValue_t value;

    rbus_PopString(msg, (char const**) &name);
#if DEBUG_SERIALIZER
    rtLog_Info("> prop pop name=%s", name);
#endif
    rbusProperty_Init(property, name, NULL);
    rbusValue_initFromMessage(&value, msg);
    rbusProperty_SetValue(*property, value);
    rbusValue_Release(value);
}

static void rbusEvent_updateFromMessage(rbusEvent_t* event, rtMessage msg)
{
    char const* name;
    int type;
    rbusObject_t data;

    rbus_PopString(msg, (char const**) &name);
    rbus_PopInt32(msg, (int*) &type);
#if DEBUG_SERIALIZER
    rtLog_Info("> event pop name=%s type=%d", name, type);
#endif

    rbusObject_initFromMessage(&data, msg);

    event->name = name;
    event->type = type;
    event->data = data;/*caller must call rbusValue_Release*/
}

static void rbusPropertyList_appendToMessage(rbusProperty_t prop, rtMessage msg)
{
    int numProps = 0;
    rbusProperty_t first = prop;
    while(prop)
    {
        numProps++;
        prop = rbusProperty_GetNext(prop);
    }
    rbus_AppendInt32(msg, numProps);/*property count*/
#if DEBUG_SERIALIZER
    rtLog_Info("> prop add numProps=%d", numProps);
#endif
    prop = first;
    while(prop)
    {
        rbusValue_appendToMessage(rbusProperty_GetName(prop), rbusProperty_GetValue(prop), msg);
        prop = rbusProperty_GetNext(prop);
    }
}

static void rbusPropertyList_initFromMessage(rbusProperty_t* prop, rtMessage msg)
{
    rbusProperty_t previous = NULL, first = NULL;
    int numProps = 0;
    rbus_PopInt32(msg, (int*) &numProps);
#if DEBUG_SERIALIZER
    rtLog_Info("> prop pop numProps=%d", numProps);
#endif
    while(--numProps >= 0)
    {
        rbusProperty_t prop;
        rbusProperty_initFromMessage(&prop, msg);
        if(first == NULL)
            first = prop;
        if(previous != NULL)
        {
            rbusProperty_SetNext(previous, prop);
            rbusProperty_Release(prop);
        }
        previous = prop;
    }
    /*TODO we need to release the props we inited*/
    *prop = first;
}

static void rbusObject_appendToMessage(rbusObject_t obj, rtMessage msg)
{
    int numChild = 0;
    rbusObject_t child;

    rbus_AppendString(msg, rbusObject_GetName(obj));/*object name*/
    rbus_AppendInt32(msg, rbusObject_GetType(obj));/*object type*/
#if DEBUG_SERIALIZER
    rtLog_Info("> object add name=%s type=%d", rbusObject_GetName(obj), rbusObject_GetType(obj));
#endif
    rbusPropertyList_appendToMessage(rbusObject_GetProperties(obj), msg);

    child = rbusObject_GetChildren(obj);
    numChild = 0;
    while(child)
    {
        numChild++;
        child = rbusObject_GetNext(child);
    }
    rbus_AppendInt32(msg, numChild);/*object child object count*/
#if DEBUG_SERIALIZER
    rtLog_Info("> object add numChild=%d", numChild);
#endif
    child = rbusObject_GetChildren(obj);
    while(child)
    {
        rbusObject_appendToMessage(child, msg);/*object child object*/
        child = rbusObject_GetNext(child);
    }
}


static void rbusObject_initFromMessage(rbusObject_t* obj, rtMessage msg)
{
    char const* name;
    int type;
    int numChild = 0;
    rbusProperty_t prop;
    rbusObject_t children=NULL, previous=NULL;

    rbus_PopString(msg, &name);
    rbus_PopInt32(msg, &type);
#if DEBUG_SERIALIZER
    rtLog_Info("> object pop name=%s type=%d", name, type);
#endif

    rbusPropertyList_initFromMessage(&prop, msg);

    rbus_PopInt32(msg, &numChild);
#if DEBUG_SERIALIZER
    rtLog_Info("> object pop numChild=%d", numChild);
#endif

    while(--numChild >= 0)
    {
        rbusObject_t next;
        rbusObject_initFromMessage(&next, msg);/*object child object*/
        if(children == NULL)
            children = next;
        if(previous != NULL)
        {
            rbusObject_SetNext(previous, next);
            rbusObject_Release(next);
        }
        previous = next;
    }

    if(type == RBUS_OBJECT_MULTI_INSTANCE)
        rbusObject_InitMultiInstance(obj, name);
    else
        rbusObject_Init(obj, name);

    rbusObject_SetProperties(*obj, prop);
    rbusProperty_Release(prop);
    rbusObject_SetChildren(*obj, children);
    rbusObject_Release(children);
}

static void rbusValue_appendToMessage(char const* name, rbusValue_t value, rtMessage msg)
{
    rbusValueType_t type = rbusValue_GetType(value);

    rbus_AppendString(msg, name);
    rbus_AppendInt32(msg, type);
#if DEBUG_SERIALIZER
    rtLog_Info("> value add name=%s type=%d", name, type);
#endif
    if(type == RBUS_OBJECT)
    {
        rbusObject_appendToMessage(rbusValue_GetObject(value), msg);
    }
    else if(type == RBUS_PROPERTY)
    {
        rbusPropertyList_appendToMessage(rbusValue_GetProperty(value), msg);
    }
    else
    {
        switch(type)
        {
            case RBUS_INT16:
                rbus_AppendInt32(msg, (int32_t)rbusValue_GetInt16(value));
                break;
            case RBUS_UINT16:
                rbus_AppendInt32(msg, (int32_t)rbusValue_GetUInt16(value));
                break;
            case RBUS_INT32:
                rbus_AppendInt32(msg, (int32_t)rbusValue_GetInt32(value));
                break;
            case RBUS_UINT32:
                rbus_AppendInt32(msg, (int32_t)rbusValue_GetUInt32(value));
                break;
            case RBUS_INT64:
            {
                union UNION64
                {
                    int32_t i32[2];
                    int64_t i64;
                };
                union UNION64 u;
                u.i64 = rbusValue_GetInt64(value);
                rbus_AppendInt32(msg, (int32_t)u.i32[0]);
                rbus_AppendInt32(msg, (int32_t)u.i32[1]);
                break;
            }
            case RBUS_UINT64:
            {
                union UNION64
                {
                    int32_t i32[2];
                    int64_t u64;
                };
                union UNION64 u;
                u.u64 = rbusValue_GetUInt64(value);
                rbus_AppendInt32(msg, (int32_t)u.i32[0]);
                rbus_AppendInt32(msg, (int32_t)u.i32[1]);
                break;
            }
            case RBUS_SINGLE:
                rbus_AppendDouble(msg, (double)rbusValue_GetSingle(value));
                break;
            case RBUS_DOUBLE:
                rbus_AppendDouble(msg, (double)rbusValue_GetDouble(value));
                break;
            case RBUS_DATETIME:
                rbus_AppendInt32(msg, (int32_t)rbusValue_GetTime(value)->tv_sec);
                rbus_AppendInt32(msg, (int32_t)rbusValue_GetTime(value)->tv_usec);
                break;
            default:
                rbus_AppendBinaryData(msg, rbusValue_GetV(value), rbusValue_GetL(value));
                break;
        }
#if DEBUG_SERIALIZER
        char* sv = rbusValue_ToString(value,0,0);
        rtLog_Info("> value add data=%s", sv);
        free(sv);
#endif

    }
}

static void rbusEvent_appendToMessage(rbusEvent_t* event, rtMessage msg)
{
    rbus_AppendString(msg, event->name);
    rbus_AppendInt32(msg, event->type);
#if DEBUG_SERIALIZER
    rtLog_Info("> event add name=%s type=%d", event->name, event->type);
#endif
    rbusObject_appendToMessage(event->data, msg);
}

bool _is_valid_get_query(char const* name)
{
    /* 1. Find whether the query ends with `!` to find out Event is being queried */
    /* 2. Find whether the query ends with `()` to find out method is being queried */
    if (name != NULL)
    {
        int length = strlen (name);
        int temp = 0;
        temp = length - 1;
        if (('!' == name[temp]) ||
            (')' == name[temp]) ||
            (NULL != strstr (name, "(")))
        {
            rtLog_Debug("Event or Method is Queried");
            return false;
        }
    }
    else
    {
        rtLog_Debug("Null Pointer sent for Query");
        return false;
    }

    return true;

}
bool _is_wildcard_query(char const* name)
{
    if (name != NULL)
    {
        /* 1. Find whether the query ends with `.` to find out object level query */
        /* 2. Find whether the query has `*` to find out multiple items are being queried */
        int length = strlen (name);
        int temp = 0;
        temp = length - 1;

        if (('.' == name[temp]) || (NULL != strstr (name, "*")))
        {
            rtLog_Debug("The Query is having wildcard.. ");
            return true;
        }
    }
    else
    {
        rtLog_Debug("Null Pointer sent for Query");
        return true;
    }

    return false;
}

char const* getLastTokenInName(char const* name)
{
    if(name == NULL)
        return NULL;

    int len = (int)strlen(name);

    if(len == 0)
        return name;

    len--;

    if(name[len] == '.')
        len--;

    while(len != 0 && name[len] != '.')
        len--;
        
    if(name[len] == '.')
        return &name[len+1];
    else
        return name;
}
/*  Recurse row elements Adding or Removing value change properties
 *  when adding a row, call this after subscriptions are added to the row element
 *  when removing a row, call this before subscriptions are removed from the row element
 */
void valueChangeTableRowUpdate(rbusHandle_t handle, elementNode* rowNode, bool added)
{
    if(rowNode)
    {
        elementNode* child = rowNode->child;

        while(child)
        {
            if(child->type == RBUS_ELEMENT_TYPE_PROPERTY)
            {
                /*avoid calling ValueChange if there's no subs on it*/
                if(elementHasAutoPubSubscriptions(child, NULL))
                {
                    if(added)
                    {
                        rbusValueChange_AddPropertyNode(handle, child);
                    }
                    else
                    {
                        rbusValueChange_RemovePropertyNode(handle, child);
                    }
                }
            }

            /*recurse into children that are not row templates*/
            if( child->child && !(child->parent->type == RBUS_ELEMENT_TYPE_TABLE && strcmp(child->name, "{i}") == 0) )
            {
                valueChangeTableRowUpdate(handle, child, added);
            }

            child = child->nextSibling;
        }
    }
}

//******************************* CALLBACKS *************************************//


static int _event_subscribe_callback_handler(char const* object,  char const* eventName, char const* listener, int added, void* userData)
{
    rbusHandle_t handle = (rbusHandle_t)userData;
    comp_info* ci = (comp_info*)userData;

    UNUSED2(object,listener);

    rtLog_Debug("%s: event subscribe callback for [%s] event!", __FUNCTION__, eventName);

    elementNode* el = retrieveElement(ci->elementRoot, eventName);

    if(el)
    {
        rbusError_t err = RBUS_ERROR_SUCCESS;
        bool autoPublish = true;
        rbusSubscription_t* subscription = NULL;

        rtLog_Debug("%s: found element of type %d", __FUNCTION__, el->type);

        /* call the provider subHandler first to see if it overrides autoPublish */
        if(el->cbTable.eventSubHandler)
        {
            rbusEventSubAction_t action;
            if(added)
                action = RBUS_EVENT_ACTION_SUBSCRIBE;
            else
                action = RBUS_EVENT_ACTION_UNSUBSCRIBE;

            err = el->cbTable.eventSubHandler(handle, action, eventName, NULL, &autoPublish);
        }

        
        if(err == RBUS_ERROR_SUCCESS)
        {
            /*TODO do we care about error from callback ?
            return 0;
            */
        }
        

        if(added)
        {
            subscription = rbusSubscriptions_addSubscription(ci->subscriptions, listener, eventName, NULL/*TODO filter*/, autoPublish, el);
        }
        else
        {
            subscription = rbusSubscriptions_getSubscription(ci->subscriptions, listener, eventName, NULL/*TODO filter*/);
        }

        if(!subscription)
        {
            rtLog_Warn("%s: subscription null", __FUNCTION__);
            return 0;
        }

        /* update ValueChange with subscription's properties
           unless the provider has overridden autoPublish */
        if(el->type == RBUS_ELEMENT_TYPE_PROPERTY && subscription->autoPublish)
        {
            rtListItem item;
            rtList_GetFront(subscription->instances, &item);
            while(item)
            {
                elementNode* node;

                rtListItem_GetData(item, (void**)&node);

                /* Check if the node has other subscribers or not.  If it has other
                   subs then we don't need to either add or remove it from ValueChange */
                if(!elementHasAutoPubSubscriptions(node, subscription))
                {
                    rtLog_Info("%s: ValueChange %s event=%s prop=%s", __FUNCTION__, 
                        added ? "Add" : "Remove", subscription->eventName, node->fullName);

                    if(added)
                    {
                        rbusValueChange_AddPropertyNode(handle, node);
                    }
                    else
                    {
                        rbusValueChange_RemovePropertyNode(handle, node);
                    }
                }

                rtListItem_GetNext(item, &item);
            }   
        }

        /*remove subscription only after handling its ValueChange properties above*/
        if(!added)
        {
            rbusSubscriptions_removeSubscription(ci->subscriptions, subscription);
        }
    }
    else
    {
        rtLog_Warn("event subscribe callback: unexpected! element not found");
    }
    return 0;
}

static int _event_callback_handler (char const* objectName, char const* eventName, rtMessage message, void* userData)
{
    rbusEventSubscription_t* subscription;
    rbusEventHandler_t handler;
    rbusEvent_t event;

    rtLog_Debug("Received event callback: objectName=%s eventName=%s", 
        objectName, eventName);

    subscription = (rbusEventSubscription_t*)userData;

    if(!subscription || !subscription->handle || !subscription->handler)
    {
        return RBUS_ERROR_BUS_ERROR;
    }

    handler = (rbusEventHandler_t)subscription->handler;

    rbusEvent_updateFromMessage(&event, message);
    
    (*handler)(subscription->handle, &event, subscription);

    rbusObject_Release(event.data);

    return 0;
}

static void _set_callback_handler (rbusHandle_t handle, rtMessage request, rtMessage *response)
{
    rbusError_t rc = 0;
    int sessionId = 0;
    int numVals = 0;
    int loopCnt = 0;
    char* pCompName = NULL;
    char* pIsCommit = NULL;
    char const* pFailedElement = NULL;
    rbusProperty_t* pProperties = NULL;
    comp_info* pCompInfo = (comp_info*)handle;
    rbusSetHandlerOptions_t opts;

    memset(&opts, 0, sizeof(opts));

    rbus_PopInt32(request, &sessionId);
    rbus_PopString(request, (char const**) &pCompName);
    rbus_PopInt32(request, &numVals);

    if(numVals > 0)
    {
        /* Update the Get Handler input options */
        opts.sessionId = sessionId;
        opts.requestingComponent = pCompName;

        elementNode* el = NULL;

        pProperties = (rbusProperty_t*)malloc(numVals*sizeof(rbusProperty_t));
        for (loopCnt = 0; loopCnt < numVals; loopCnt++)
        {
            rbusProperty_initFromMessage(&pProperties[loopCnt], request);
        }

        rbus_PopString(request, (char const**) &pIsCommit);

        /* Since we set as string, this needs to compared with string..
         * Otherwise, just the #define in the top for TRUE/FALSE should be used.
         */
        if (strncasecmp("TRUE", pIsCommit, 4) == 0)
            opts.commit = true;

        for (loopCnt = 0; loopCnt < numVals; loopCnt++)
        {
            /* Retrive the element node */
            char const* paramName = rbusProperty_GetName(pProperties[loopCnt]);
            el = retrieveElement(pCompInfo->elementRoot, paramName);
            if(el != NULL)
            {
                if(el->cbTable.setHandler)
                {
                    rc = el->cbTable.setHandler(handle, pProperties[loopCnt], &opts);
                    if (rc != RBUS_ERROR_SUCCESS)
                    {
                        rtLog_Warn("Set Failed for %s; Component Owner returned Error", paramName);
                        pFailedElement = paramName;
                        break;
                    }
                }
                else
                {
                    rtLog_Warn("Set Failed for %s; No Handler found", paramName);
                    rc = RBUS_ERROR_ACCESS_NOT_ALLOWED;
                    pFailedElement = paramName;
                    break;
                }
            }
            else
            {
                rtLog_Warn("Set Failed for %s; No Element registered", paramName);
                rc = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
                pFailedElement = paramName;
                break;
            }
        }
    }
    else
    {
        rtLog_Warn("Set Failed as %s did not send any input", pCompName);
        rc = RBUS_ERROR_INVALID_INPUT;
        pFailedElement = pCompName;
    }

    rtMessage_Create(response);
    rbus_AppendInt32(*response, (int) rc);
    if (pFailedElement)
        rbus_AppendString(*response, pFailedElement);

    if(pProperties)
    {
        for (loopCnt = 0; loopCnt < numVals; loopCnt++)
        {
            rbusProperty_Release(pProperties[loopCnt]);
        }
        free(pProperties);
    }

    return;
}

void _get_recursive_wildcard_handler(elementNode* wildQueryElemNode, rbusHandle_t handle, const char* pRequestingComp, rbusProperty_t properties, int *pCount)
{
    rbusGetHandlerOptions_t options;
    memset(&options, 0, sizeof(options));

    /* Update the Get Handler input options */
    options.requestingComponent = pRequestingComp;

    if (wildQueryElemNode != NULL)
    {
        elementNode* child = wildQueryElemNode->child;

        while(child)
        {
            if((child->type == RBUS_ELEMENT_TYPE_PROPERTY) && (child->cbTable.getHandler))
            {
                rbusProperty_t tmpProperties;
                rbusProperty_Init(&tmpProperties, child->fullName, NULL);
                child->cbTable.getHandler(handle, tmpProperties, &options);
                rbusProperty_PushBack(properties, tmpProperties);
                rbusProperty_Release(tmpProperties);
                *pCount += 1;
            }

            /*recurse into children that are not row templates*/
            if( child->child && !(child->parent->type == RBUS_ELEMENT_TYPE_TABLE && strcmp(child->name, "{i}") == 0) )
            {
                _get_recursive_wildcard_handler(child, handle, pRequestingComp, properties, pCount);
            }

            child = child->nextSibling;
        }
    }
}

void _get_callback_wildcard_handler(rbusHandle_t handle, const char* pParameterName, const char* pRequestingComp, rtMessage *response)
{
    /* Lets find all the elements registered by the Component */
    rtMessage eResponse;
    const char *pElementNames= NULL;
    int numOfElements = 0;
    int numOfGets = 0;
    comp_info* ci = (comp_info*)handle;
    rbusProperty_t properties, first, last;
    int firstElement = 0;
    int i = 0, j = 0, length = 0;
    rbusGetHandlerOptions_t options;

    memset(&options, 0, sizeof(options));

    /* Update the Get Handler input options */
    options.requestingComponent = pRequestingComp;

    if (RTMESSAGE_BUS_SUCCESS == rbus_GetElementsAddedByObject(ci->componentName, &eResponse))
    {
        rbus_PopInt32(eResponse, &numOfElements);
        if (numOfElements > 0)
            numOfElements -= 1;
        rtLog_Debug("Number of Entries in %s component is %d", ci->componentName, numOfElements);

        length = strlen(pParameterName);
        for(j = 0; j < numOfElements; j++)
        {
            rbus_PopString(eResponse, &pElementNames);
            rtLog_Debug("The names is, %s", pElementNames);
            if (strncmp(pElementNames, pParameterName, length) == 0)
            {
                elementNode* el = NULL;
                el = retrieveElement(ci->elementRoot, pElementNames);
                if(el != NULL)
                {
                    rtLog_Debug("Retrieved [%s]", pElementNames);
                    if(el->cbTable.getHandler)
                    {
                        numOfGets++;
                        rtLog_Debug("Table and CB exists for [%s], call the CB!", pElementNames);
                        if (0 == firstElement)
                        {
                            firstElement = 1;
                            rbusProperty_Init(&properties, pElementNames, NULL);
                            el->cbTable.getHandler(handle, properties, &options);
                            last = properties;
                        }
                        else
                        {
                            rbusProperty_t tmpProperties;
                            rbusProperty_Init(&tmpProperties, pElementNames, NULL);
                            el->cbTable.getHandler(handle, tmpProperties, &options);
                            rbusProperty_SetNext(last, tmpProperties);
                            rbusProperty_Release(tmpProperties);
                            last = tmpProperties;
                        }
                    }
                }
            }
        }
        rtMessage_Release(eResponse);

        rtLog_Info ("We have identified %d entries that are matching the request and got the value. Lets return it.", numOfGets);

        rtMessage_Create(response);
        if (numOfGets > 0)
        {
            first = properties;
            rbus_AppendInt32(*response, (int) RBUS_ERROR_SUCCESS);
            rbus_AppendInt32(*response, numOfGets);
            for(i = 0; i < numOfGets; i++)
            {
                rbusValue_appendToMessage(rbusProperty_GetName(first), rbusProperty_GetValue(first), *response);
                first = rbusProperty_GetNext(first);
            }
            /* Release the memory */
            rbusProperty_Release(properties);
        }
        else
        {
            rbus_AppendInt32(*response, (int) RBUS_ERROR_ELEMENT_DOES_NOT_EXIST);
        }
    }
    else
    {
        rtMessage_Create(response);
        rbus_AppendInt32(*response, (int) RBUS_ERROR_ELEMENT_DOES_NOT_EXIST);
    }

    return;
}

static void _get_callback_handler (rbusHandle_t handle, rtMessage request, rtMessage *response)
{
    comp_info* ci = (comp_info*)handle;
    int paramSize = 1, i = 0;
    rbusError_t result = RBUS_ERROR_SUCCESS;
    char const *parameterName = NULL;
    char const *pCompName = NULL;
    rbusProperty_t* properties = NULL;
    rbusGetHandlerOptions_t options;

    memset(&options, 0, sizeof(options));
    rbus_PopString(request, &pCompName);
    rbus_PopInt32(request, &paramSize);

    rtLog_Debug("Param Size [%d]", paramSize);

    if(paramSize > 0)
    {
        /* Update the Get Handler input options */
        options.requestingComponent = pCompName;

        properties = malloc(paramSize*sizeof(rbusProperty_t));

        for(i = 0; i < paramSize; i++)
        {
            rbusProperty_Init(&properties[i], NULL, NULL);
        }

        for(i = 0; i < paramSize; i++)
        {
            elementNode* el = NULL;

            parameterName = NULL;
            rbus_PopString(request, &parameterName);

            rtLog_Debug("Param Name [%d]:[%s]", i, parameterName);

            rbusProperty_SetName(properties[i], parameterName);

            /* Check for wildcard query */
            int length = strlen(parameterName) - 1;
            if (parameterName[length] == '.')
            {
                rtLog_Info ("handle the wildcard request..");
#if 1
                el = retrieveInstanceElement(ci->elementRoot, parameterName);
                if (el != NULL)
                {
                    rbusProperty_t xproperties, first;
                    rbusValue_t xtmp;
                    int count = 0;

                    rbusValue_Init(&xtmp);
                    rbusValue_SetString(xtmp, "tmpValue");
                    rbusProperty_Init(&xproperties, "tmpProp", xtmp);
                    _get_recursive_wildcard_handler(el, handle, pCompName, xproperties, &count);
                    rtLog_Info ("We have identified %d entries that are matching the request and got the value. Lets return it.", count);

                    rtMessage_Create(response);
                    if (count > 0)
                    {
                        first = rbusProperty_GetNext(xproperties);
                        rbus_AppendInt32(*response, (int) RBUS_ERROR_SUCCESS);
                        rbus_AppendInt32(*response, count);
                        for(i = 0; i < count; i++)
                        {
                            rbusValue_appendToMessage(rbusProperty_GetName(first), rbusProperty_GetValue(first), *response);
                            first = rbusProperty_GetNext(first);
                        }
                        /* Release the memory */
                        rbusProperty_Release(xproperties);
                    }
                    else
                    {
                        rbus_AppendInt32(*response, (int) RBUS_ERROR_ELEMENT_DOES_NOT_EXIST);
                    }
                }
#else
                _get_callback_wildcard_handler(handle, parameterName, pCompName, response);
#endif

                /* Free the memory, regardless of success or not.. */
                for (i = 0; i < paramSize; i++)
                {
                    rbusProperty_Release(properties[i]);
                }
                free (properties);

                return;
            }

            //Do a look up and call the corresponding method
            el = retrieveElement(ci->elementRoot, parameterName);
            if(el != NULL)
            {
                rtLog_Debug("Retrieved [%s]", parameterName);

                if(el->cbTable.getHandler)
                {
                    rtLog_Debug("Table and CB exists for [%s], call the CB!", parameterName);

                    result = el->cbTable.getHandler(handle, properties[i], &options);

                    if (result != RBUS_ERROR_SUCCESS)
                    {
                        rtLog_Warn("called CB with result [%d]", result);
                        break;
                    }
                }
                else
                {
                    rtLog_Warn("Element retrieved, but no cb installed for [%s]!", parameterName);
                    result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
                    break;
                }
            }
            else
            {
                rtLog_Warn("Not able to retrieve element [%s]", parameterName);
                result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
                break;
            }
        }

        rtMessage_Create(response);
        rbus_AppendInt32(*response, (int) result);
        if (result == RBUS_ERROR_SUCCESS)
        {
            rbus_AppendInt32(*response, paramSize);
            for(i = 0; i < paramSize; i++)
            {
                rbusValue_appendToMessage(rbusProperty_GetName(properties[i]), rbusProperty_GetValue(properties[i]), *response);
            }
        }
       
        /* Free the memory, regardless of success or not.. */
        for (i = 0; i < paramSize; i++)
        {
            rbusProperty_Release(properties[i]);
        }
        free (properties);
    }
    else
    {
        rtLog_Warn("Get Failed as %s did not send any input", pCompName);
        result = RBUS_ERROR_INVALID_INPUT;
        rtMessage_Create(response);
        rbus_AppendInt32(*response, (int) result);
    }

    return;
}

static void _table_add_row_callback_handler (rbusHandle_t handle, rtMessage request, rtMessage* response)
{
    comp_info* ci = (comp_info*)handle;
    rbusError_t result = RBUS_ERROR_BUS_ERROR;
    int sessionId;
    char const* tableName;
    char const* aliasName = NULL;
    int err;
    uint32_t instNum = 0;

    rbus_PopInt32(request, &sessionId);
    rbus_PopString(request, &tableName);
    err = rbus_PopString(request, &aliasName); /*this presumes rbus_updateTable sent the alias.  
                                                 if CCSP/dmcli is calling us, then this will be NULL*/
    if(err != RT_OK || (aliasName && strlen(aliasName)==0))
        aliasName = NULL;

    rtLog_Info("%s table [%s] alias [%s] err [%d]", __FUNCTION__, tableName, aliasName, err);

    elementNode* tableRegElem = retrieveElement(ci->elementRoot, tableName);
    elementNode* tableInstElem = retrieveInstanceElement(ci->elementRoot, tableName);

    if(tableRegElem && tableInstElem)
    {
        if(tableRegElem->cbTable.tableAddRowHandler)
        {
            rtLog_Info("%s calling tableAddRowHandler table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);

            result = tableRegElem->cbTable.tableAddRowHandler(handle, tableName, aliasName, &instNum);

            if (result == RBUS_ERROR_SUCCESS)
            {
                elementNode* rowElem;

                rtLog_Info("%s tableAddRowHandler success table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);

                rowElem = instantiateTableRow(tableInstElem, instNum, aliasName);

                rbusSubscriptions_onTableRowAdded(ci->subscriptions, rowElem);

                /*update ValueChange after rbusSubscriptions_onTableRowAdded */
                valueChangeTableRowUpdate(handle, rowElem, true);

                /*send OBJECT_CREATED event after we create the row*/
                {
                    rbusEvent_t event;
                    rbusError_t respub;
                    rbusObject_t data;
                    rbusValue_t instNumVal;
                    rbusValue_t aliasVal;
                    rbusValue_t rowNameVal;

                    rbusValue_Init(&rowNameVal);
                    rbusValue_Init(&instNumVal);
                    rbusValue_Init(&aliasVal);

                    rbusValue_SetString(rowNameVal, rowElem->fullName);
                    rbusValue_SetUInt32(instNumVal, instNum);
                    rbusValue_SetString(aliasVal, aliasName);

                    rbusObject_Init(&data, NULL);
                    rbusObject_SetValue(data, "rowName", rowNameVal);
                    rbusObject_SetValue(data, "instNum", instNumVal);
                    rbusObject_SetValue(data, "alias", aliasVal);

                    event.name = tableName;
                    event.type = RBUS_EVENT_OBJECT_CREATED;
                    event.data = data;

                    rtLog_Info("%s publishing ObjectCreated table=%s rowName=%s", __FUNCTION__, tableName, rowElem->fullName);
                    respub = rbusEvent_Publish(handle, &event);

                    if(respub != RBUS_ERROR_SUCCESS)
                    {
                        rtLog_Warn("failed to publish ObjectCreated event err:%d", respub);
                    }

                    rbusValue_Release(rowNameVal);
                    rbusValue_Release(instNumVal);
                    rbusValue_Release(aliasVal);
                    rbusObject_Release(data);
                }
            }
            else
            {
                rtLog_Warn("%s tableAddRowHandler failed table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);
            }
        }
        else
        {
            rtLog_Warn("%s tableAddRowHandler not registered table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);
            result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
        }
    }
    else
    {
        rtLog_Warn("%s no element found table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);
        result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    rtMessage_Create(response);
    rbus_AppendInt32(*response, result);
    rbus_AppendInt32(*response, (int32_t)instNum);
}

static void _table_remove_row_callback_handler (rbusHandle_t handle, rtMessage request, rtMessage* response)
{
    comp_info* ci = (comp_info*)handle;
    rbusError_t result = RBUS_ERROR_BUS_ERROR;
    int sessionId;
    char const* rowName;

    rbus_PopInt32(request, &sessionId);
    rbus_PopString(request, &rowName);

    rtLog_Info("%s row [%s]", __FUNCTION__, rowName);

    /*get the element for the row */
    elementNode* rowRegElem = retrieveElement(ci->elementRoot, rowName);
    elementNode* rowInstElem = retrieveInstanceElement(ci->elementRoot, rowName);


    if(rowRegElem && rowInstElem)
    {
        /*switch to the row's table */
        elementNode* tableRegElem = rowRegElem->parent;
        elementNode* tableInstElem = rowInstElem->parent;

        if(tableRegElem && tableInstElem)
        {
            if(tableRegElem->cbTable.tableRemoveRowHandler)
            {
                rtLog_Info("%s calling tableRemoveRowHandler row [%s]", __FUNCTION__, rowName);

                result = tableRegElem->cbTable.tableRemoveRowHandler(handle, rowName);

                if (result == RBUS_ERROR_SUCCESS)
                {
                    rtLog_Info("%s tableRemoveRowHandler success row [%s]", __FUNCTION__, rowName);

                    char* rowInstName = strdup(rowInstElem->fullName); /*must dup because we are deleting the instance*/

                    /*update ValueChange before rbusSubscriptions_onTableRowRemoved */
                    valueChangeTableRowUpdate(handle, rowInstElem, false);

                    rbusSubscriptions_onTableRowRemoved(ci->subscriptions, rowInstElem);

                    deleteTableRow(rowInstElem);

                    /*send OBJECT_DELETED event after we delete the row*/
                    {
                        rbusEvent_t event;
                        rbusError_t respub;
                        rbusValue_t rowNameVal;
                        rbusObject_t data;
                        char tableName[RBUS_MAX_NAME_LENGTH];

                        /*must end the table name with a dot(.)*/
                        snprintf(tableName, RBUS_MAX_NAME_LENGTH, "%s.", tableInstElem->fullName);

                        rbusValue_Init(&rowNameVal);
                        rbusValue_SetString(rowNameVal, rowInstName);

                        rbusObject_Init(&data, NULL);
                        rbusObject_SetValue(data, "rowName", rowNameVal);

                        event.name = tableName;
                        event.data = data;
                        event.type = RBUS_EVENT_OBJECT_DELETED;
                        rtLog_Info("%s publishing ObjectDeleted table=%s rowName=%s", __FUNCTION__, tableInstElem->fullName, rowName);
                        respub = rbusEvent_Publish(handle, &event);

                        rbusValue_Release(rowNameVal);
                        rbusObject_Release(data);
                        free(rowInstName);

                        if(respub != RBUS_ERROR_SUCCESS)
                        {
                            rtLog_Warn("failed to publish ObjectDeleted event err:%d", respub);
                        }
                    }
                }
                else
                {
                    rtLog_Warn("%s tableRemoveRowHandler failed row [%s]", __FUNCTION__, rowName);
                }
            }
            else
            {
                rtLog_Info("%s tableRemoveRowHandler not registered row [%s]", __FUNCTION__, rowName);
                result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
            }
        }
        else
        {
            rtLog_Warn("%s no parent element found row [%s]", __FUNCTION__, rowName);
            result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
    }
    else
    {
        rtLog_Warn("%s no element found row [%s]", __FUNCTION__, rowName);
        result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    rtMessage_Create(response);
    rbus_AppendInt32(*response, result);
}

static int _callback_handler(char const* destination, char const* method, rtMessage request, void* userData, rtMessage* response)
{
    rbusHandle_t handle = (rbusHandle_t)userData;

    rtLog_Debug("Received callback for [%s]", destination);

    if(!strcmp(method, METHOD_GETPARAMETERVALUES))
    {
        _get_callback_handler (handle, request, response);
    }
    else if(!strcmp(method, METHOD_SETPARAMETERVALUES))
    {
        _set_callback_handler (handle, request, response);
    }
    else if(!strcmp(method, METHOD_ADDTBLROW))
    {
        _table_add_row_callback_handler (handle, request, response);
    }
    else if(!strcmp(method, METHOD_DELETETBLROW))
    {
        _table_remove_row_callback_handler (handle, request, response);
    }
    else
    {
        rtLog_Warn("unhandled callback for [%s] method!", method);
    }

    return 0;
}

//******************************* Bus Initialization *****************************//
rbusError_t rbus_open(rbusHandle_t* handle, char *componentName)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    int foundIndex = -1;
    int  i = 0;

    if((handle == NULL) || (componentName == NULL))
    {
        return RBUS_ERROR_INVALID_INPUT;
    }

    *handle = NULL;

    rtLog_SetLevel(RT_LOG_INFO);
    /*
        Per spec: If a component calls this API more than once, any previous busHandle 
        and all previous data element registrations will be canceled.
    */
    for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
    {
        if(comp_array[i].inUse && strcmp(componentName, comp_array[i].componentName) == 0)
        {
            rbus_close(&comp_array[i]);
        }
    }

    /*  Find open item in array: 
        TODO why can't this just be a rtVector we push/remove from? */
    for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
    {
        if(!comp_array[i].inUse)
        {
            foundIndex = i;
            break;
        }
    }

    if(foundIndex == -1)
    {
        rtLog_Error("<%s>: Exceeded the allowed number of components per process!", __FUNCTION__);
        errorcode = RBUS_ERROR_OUT_OF_RESOURCES;
    }
    else
    {
        /*
            Per spec: the first component that calls rbus_open will establishes a new socket connection to the bus broker.
            Note: rbus_openBrokerConnection returns RTMESSAGE_BUS_ERROR_INVALID_STATE if a connection is already established.
            We cannot expect our 1st call to rbus_openBrokerConnection to succeed, as another library in this process
            may have already called rbus_openBrokerConnection.  This would happen if the ccsp_message_bus is already 
            running with rbus-core in the same process.  Thus we must call again and check the return code.
        */
        err = rbus_openBrokerConnection(componentName);
        
        if( err != RTMESSAGE_BUS_SUCCESS && 
            err != RTMESSAGE_BUS_ERROR_INVALID_STATE/*connection already opened. which is allowed*/)
        {
            rtLog_Error("<%s>: rbus_openBrokerConnection() fails with %d", __FUNCTION__, err);
            errorcode = RBUS_ERROR_BUS_ERROR;
        }
        else
        {
            rbusHandle_t tmpHandle = &comp_array[foundIndex];

            rtLog_Info("<%s>: rbus_openBrokerConnection() Success!", __FUNCTION__);
            rtLog_Debug("<%s>: Try rbus_registerObj() for component base object [%s]!", __FUNCTION__, componentName);

            if((err = rbus_registerObj(componentName, _callback_handler, tmpHandle)) != RTMESSAGE_BUS_SUCCESS)
            {
                /*Note that this will fail if a previous rbus_open was made with the same componentName
                  because rbus_registerObj doesn't allow the same name to be registered twice.  This would
                  also fail if ccsp using rbus-core has registered the same object name */
                rtLog_Error("<%s>: rbus_registerObj() fails with %d", __FUNCTION__, err);
                errorcode = RBUS_ERROR_BUS_ERROR;
            }
            else
            {
                rtLog_Debug("<%s>: rbus_registerObj() Success!", __FUNCTION__);

                if((err = rbus_registerSubscribeHandler(componentName, _event_subscribe_callback_handler, tmpHandle)) != RTMESSAGE_BUS_SUCCESS)
                {
                    rtLog_Error("<%s>: rbus_registerSubscribeHandler() Failed!", __FUNCTION__);
                    errorcode = RBUS_ERROR_BUS_ERROR;
                }
                else
                {
                    rtLog_Debug("<%s>: rbus_registerSubscribeHandler() Success!", __FUNCTION__);

                    comp_array[foundIndex].inUse = 1;
                    comp_array[foundIndex].componentName = strdup(componentName);
                    *handle = tmpHandle;
                    rtVector_Create(&comp_array[foundIndex].eventSubs);

                    /*you really only need to call once per process but it doesn't hurt to call here*/
                    rbusValueChange_SetPollingPeriod(VALUE_CHANGE_POLLING_PERIOD);
#if 0 /*my test*/
    {

        rtLog_Info("Running test:");
        elementNode* root = getEmptyElementNode(), * node;
        root->name = strdup("componentA");
        rbusDataElement_t el[5] = {
            {"Device.WiFi.AccessPoint.{i}.", RBUS_ELEMENT_TYPE_TABLE, {NULL} },
            {"Device.WiFi.AccessPoint.{i}.Prop1", RBUS_ELEMENT_TYPE_PROPERTY, {NULL} },
            {"Device.WiFi.AccessPoint.{i}.OtherObject.Property2", RBUS_ELEMENT_TYPE_PROPERTY, {NULL} },
            {"Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.", RBUS_ELEMENT_TYPE_TABLE, {NULL} },
            {"Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.SignalStrength", RBUS_ELEMENT_TYPE_PROPERTY, {NULL} }
        };
        int i = 0; 
        for(i = 0; i < 5; ++i)
            insertElement(&root, &el[i]);
        printRegisteredElements(root, 0);
        instantiateTableRow(retrieveElement(root, "Device.WiFi.AccessPoint.{i}."), 1, "doghnut");
        printRegisteredElements(root, 0);
        instantiateTableRow(retrieveElement(root, "Device.WiFi.AccessPoint.1.AssociatedDevice.{i}."), 1, "apple");
        printRegisteredElements(root, 0);
        instantiateTableRow(retrieveElement(root, "Device.WiFi.AccessPoint.1.AssociatedDevice.{i}."), 2 , "orange");
        printRegisteredElements(root, 0);
        exit(0);
    }
#endif
                }
            }

        }
    }

    return errorcode;
}

rbusError_t rbus_close(rbusHandle_t handle)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    comp_info* ci = (comp_info*)handle;

    if(handle == NULL)
    {
        return RBUS_ERROR_INVALID_INPUT;
    }

    if(ci->eventSubs)
    {
        while(rtVector_Size(ci->eventSubs))
        {
            rbusEventSubscription_t* sub = (rbusEventSubscription_t*)rtVector_At(ci->eventSubs, 0);
            if(sub)
            {
                rbusEvent_Unsubscribe(handle, sub->eventName);
            }
        }
        rtVector_Destroy(ci->eventSubs, NULL);
        ci->eventSubs = NULL;
    }

    rbusValueChange_Close(handle);//called before freeAllElements below

    if(ci->elementRoot)
    {
        freeAllElements(&(ci->elementRoot));
        //free(ci->elementRoot); valgrind reported this and I saw that freeAllElements actually frees this . leaving comment so others won't wonder why this is gone
        ci->elementRoot = NULL;
    }

    if(ci->subscriptions != NULL)
    {
        rbusSubscriptions_destroy(ci->subscriptions);
        ci->subscriptions = NULL;
    }


    if((err = rbus_unregisterObj(ci->componentName)) != RTMESSAGE_BUS_SUCCESS) //FIXME: shouldn't rbus_closeBrokerConnection be called even if this fails ?
    {
        rtLog_Warn("<%s>: rbus_unregisterObj() for [%s] fails with %d", __FUNCTION__, ci->componentName, err);
        errorcode = RBUS_ERROR_INVALID_HANDLE;
    }
    else
    {
        int canClose = 1;
        int i;

        rtLog_Debug("<%s>: rbus_unregisterObj() for [%s] Success!!", __FUNCTION__, ci->componentName);
        free(ci->componentName);
        ci->componentName = NULL;
        ci->inUse = 0;

        for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
        {
            if(comp_array[i].inUse)
            {
                canClose = 0;
                break;
            }
        }

        if(canClose)
        {
            if((err = rbus_closeBrokerConnection()) != RTMESSAGE_BUS_SUCCESS)
            {
                rtLog_Warn("<%s>: rbus_closeBrokerConnection() fails with %d", __FUNCTION__, err);
                errorcode = RBUS_ERROR_BUS_ERROR;
            }
            else
            {
                rtLog_Info("<%s>: rbus_closeBrokerConnection() Success!!", __FUNCTION__);
            }
        }
    }

    return errorcode;
}

rbusError_t rbus_regDataElements(
    rbusHandle_t handle,
    int numDataElements,
    rbusDataElement_t *elements)
{
    int i;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    comp_info* ci = (comp_info*)handle;
    for(i=0; i<numDataElements; ++i)
    {
        char* name = elements[i].name;
        rbus_error_t err = RTMESSAGE_BUS_SUCCESS;

        rtLog_Debug("rbus_getDataElements: %s", name);

        if(ci->elementRoot == NULL)
        {
            rtLog_Debug("First Time, create the root node for [%s]!", ci->componentName);
            ci->elementRoot = getEmptyElementNode();
            ci->elementRoot->name = strdup(ci->componentName);
            rtLog_Debug("Root node created for [%s]", ci->elementRoot->name);
        }

        if(ci->subscriptions == NULL)
        {
            rbusSubscriptions_create(&ci->subscriptions, handle, ci->elementRoot);
        }

        elementNode* node;
        if((node = insertElement(&(ci->elementRoot), &elements[i])) == NULL)
        {
            rtLog_Error("<%s>: failed to insert element [%s]!!", __FUNCTION__, name);
            rc = RBUS_ERROR_OUT_OF_RESOURCES;
            break;
        }
        else
        {
            if((err = rbus_addElement(ci->componentName, name)) != RTMESSAGE_BUS_SUCCESS)
            {
                rtLog_Error("<%s>: failed to add element with core [%s] err=%d!!", __FUNCTION__, name, err);
                removeElement(&(ci->elementRoot), name);
                rc = RBUS_ERROR_OUT_OF_RESOURCES;
                break;
            }
            else
            {
                char eventName[RBUS_MAX_NAME_LENGTH];

                rtLog_Info("%s inserted successfully!", name);

                strcpy(eventName, name);
                
                if(elements[i].type == RBUS_ELEMENT_TYPE_TABLE)
                {
                    /*for tables we need to strip of the '{i}.' at the end before registering it as an event*/
                    char const* last = getLastTokenInName(name);
                    if(strncmp(last, "{i}", 3) == 0)
                    {
                        ptrdiff_t offset = last-name;
                        strncpy(eventName, name, offset);
                        eventName[offset] = 0;
                    }
                }
/*
                if((err = rbus_registerEvent(ci->componentName, eventName, _event_subscribe_callback_handler, handle)) != RTMESSAGE_BUS_SUCCESS)
                {
                    rtLog_Info("<%s>: failed to register event with core [%s] err=%d!!", __FUNCTION__, eventName, err);
                    rbus_removeElement(ci->componentName, eventName);
                    removeElement(&(ci->elementRoot), eventName);
                    rc = RBUS_ERROR_OUT_OF_RESOURCES;
                    break;
                }
                else
                {
                    rtLog_Info("Event registered successfully! %s", eventName);
                }
*/
            }
        }
    }

    /*TODO: need to review if this is how we should handle any failed register.
      To avoid a provider having a half registered data model, and to avoid
      the complexity of returning a list of error codes for each element in the list,
      we treat rbus_regDataElements as a transaction.  If any element from the elements list
      fails to register, we abort the whole thing.  We do this as follows: As soon 
      as 1 fail occurs above, we break out of loop and we unregister all the 
      successfully registered elements that happened during this call, before we failed.
      Thus we unregisters elements 0 to i (i was when we broke from loop above).*/
    if(rc != RBUS_ERROR_SUCCESS && i > 0)
        rbus_unregDataElements(handle, i, elements);

    return rc;
}

rbusError_t rbus_unregDataElements(
    rbusHandle_t handle,
    int numDataElements,
    rbusDataElement_t *elements)
{
    comp_info* ci = (comp_info*)handle;
    int i;
    for(i=0; i<numDataElements; ++i)
    {
        char const* name = elements[i].name;
/*
        if(rbus_unregisterEvent(ci->componentName, name) != RTMESSAGE_BUS_SUCCESS)
            rtLog_Info("<%s>: failed to remove event [%s]!!", __FUNCTION__, name);
*/
        if(rbus_removeElement(ci->componentName, name) != RTMESSAGE_BUS_SUCCESS)
            rtLog_Warn("<%s>: failed to remove element from core [%s]!!", __FUNCTION__, name);

/*      TODO: we need to remove all instance elements that this registration element instantiated
        rbusValueChange_RemoveParameter(handle, NULL, name);
*/
        removeElement(&(ci->elementRoot), name);
    }
    return RBUS_ERROR_SUCCESS;
}

//************************* Discovery related Operations *******************//
rbusError_t rbus_discoverComponentName (rbusHandle_t handle,
                            int numElements, char** elementNames,
                            int *numComponents, char ***componentName)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    if(handle == NULL)
    {
        return RBUS_ERROR_INVALID_INPUT;
    }
    *numComponents = 0;
    *componentName = 0;
    char **output = NULL;
    if(RTMESSAGE_BUS_SUCCESS == rbus_findMatchingObjects((char const**)elementNames,numElements,&output))
    {
        *componentName = output;
        *numComponents = numElements;    
    }
    else
    {
         rtLog_Warn("return from rbus_findMatchingObjects is not success");
    }
  
    return errorcode;
}

rbusError_t rbus_discoverComponentDataElements (rbusHandle_t handle,
                            char* name, bool nextLevel,
                            int *numElements, char*** elementNames)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;

    char** val = NULL;
    rtMessage response;
    *numElements = 0;
    *elementNames = 0;
    UNUSED1(nextLevel);
    if((handle == NULL) || (name == NULL))
    {
        return RBUS_ERROR_INVALID_INPUT;
    }

    rbus_error_t ret = RTMESSAGE_BUS_SUCCESS;
    ret = rbus_GetElementsAddedByObject(name, &response);
    if(ret == RTMESSAGE_BUS_SUCCESS)
    {
        char const *comp = NULL;
        rbus_PopInt32(response, numElements);
        if(*numElements > 0)
            *numElements = *numElements-1;  //Fix this. We need a better way to ignore the component name as element name.
        if(*numElements)
        {
            int i;
            val = (char**)calloc(*numElements,  sizeof(char*));
            memset(val, 0, *numElements * sizeof(char*));
            for(i = 0; i < *numElements; i++)
            {
                comp = NULL;
                rbus_PopString(response, &comp);
                val[i] = strdup(comp);
            }
        }
        rtMessage_Release(response);
        *elementNames = val;
    }
    return errorcode;
}

//************************* Parameters related Operations *******************//
rbusError_t rbus_get(rbusHandle_t handle, char const* name, rbusValue_t* value)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rtMessage request, response;
    int ret = -1;
    comp_info* pCompInfo = (comp_info*) handle;

    if (_is_wildcard_query(name))
    {
        rtLog_Warn("%s This method does not support wildcard query", __FUNCTION__);
        return RBUS_ERROR_ACCESS_NOT_ALLOWED;
    }

    /* Is it a valid Query */
    if (!_is_valid_get_query(name))
    {
        rtLog_Warn("%s This method is only to get Parameters", __FUNCTION__);
        return RBUS_ERROR_INVALID_INPUT;
    }

    rtMessage_Create(&request);
    /* Set the Component name that invokes the set */
    rbus_AppendString(request, pCompInfo->componentName);
    /* Param Size */
    rbus_AppendInt32(request, (int32_t)1);
    rbus_AppendString(request, name);

    rtLog_Debug("Calling rbus_invokeRemoteMethod for [%s]", name);

    if((err = rbus_invokeRemoteMethod(name, METHOD_GETPARAMETERVALUES, request, 6000, &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        rtLog_Error("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        errorcode = RBUS_ERROR_BUS_ERROR;
    }
    else
    {
        int valSize;
        rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;

        rtLog_Debug("Received response for remote method invocation!");

        rbus_PopInt32(response, &ret);

        rtLog_Debug("Response from the remote method is [%d]!",ret);
        errorcode = (rbusError_t) ret;
        legacyRetCode = (rbusLegacyReturn_t) ret;

        if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
        {
            errorcode = RBUS_ERROR_SUCCESS;
            rtLog_Debug("Received valid response!");
            rbus_PopInt32(response, &valSize);
            if(1/*valSize*/)
            {
                char const *buff = NULL;

                //Param Name
                rbus_PopString(response, &buff);
                if(buff && (strcmp(name, buff) == 0))
                {
                    rbusValue_initFromMessage(value, response);
                }
                else
                {
                    rtLog_Warn("Param mismatch!");
                    rtLog_Warn("Requested param: [%s], Received Param: [%s]", name, buff);
                }
            }
        }
        else
        {
            rtLog_Warn("Response from remote method indicates the call failed!!");
            errorcode = RBUS_ERROR_BUS_ERROR;
        }

        rtMessage_Release(response);
    }
    return errorcode;
}

rbusError_t _getExt_response_parser(rtMessage response, int *numValues, rbusProperty_t* retProperties)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
    int numOfVals = 0;
    int ret = -1;
    int i = 0;
    rtLog_Debug("Received response for remote method invocation!");

    rbus_PopInt32(response, &ret);
    rtLog_Debug("Response from the remote method is [%d]!",ret);

    errorcode = (rbusError_t) ret;
    legacyRetCode = (rbusLegacyReturn_t) ret;

    *numValues = 0;
    if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
    {
        errorcode = RBUS_ERROR_SUCCESS;
        rtLog_Debug("Received valid response!");
        rbus_PopInt32(response, &numOfVals);
        *numValues = numOfVals;
        rtLog_Debug("Number of return params = %d", numOfVals);

        if(numOfVals)
        {
            rbusProperty_t last;
            for(i = 0; i < numOfVals; i++)
            {
                /* For the first instance, lets use the given pointer */
                if (0 == i)
                {
                    rbusProperty_initFromMessage(retProperties, response);
                    last = *retProperties;
                }
                else
                {
                    rbusProperty_t tmpProperties;
                    rbusProperty_initFromMessage(&tmpProperties, response);
                    rbusProperty_SetNext(last, tmpProperties);
                    rbusProperty_Release(tmpProperties);
                    last = tmpProperties;
                }
            }
        }
    }
    else
    {
        rtLog_Error("Response from remote method indicates the call failed!!");
    }
    rtMessage_Release(response);

    return errorcode;
}

rbusError_t rbus_getExt(rbusHandle_t handle, int paramCount, char const** pParamNames, int *numValues, rbusProperty_t* retProperties)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    int i = 0;
    comp_info* pCompInfo = (comp_info*) handle;

    if ((1 == paramCount) && (_is_wildcard_query(pParamNames[0])))
    {
        rtMessage wResponse;
        int numOfEntries = 0;
        const char* pEntryName = NULL;
        //int length = strlen(pParamNames[0]);

        err = rbus_resolveWildcardDestination(pParamNames[0], &numOfEntries, &wResponse);
        if (RTMESSAGE_BUS_SUCCESS == err)
        {
            rtLog_Debug("Query for expression %s was successful. See result below:", pParamNames[0]);
            rbusProperty_t last;
            *numValues = 0;
            if (0 == numOfEntries)
            {
                rtLog_Debug("It is possibly a table entry from single component.");
                rtMessage_Release(wResponse);
            }
            else
            {
                for(i = 0; i < numOfEntries; i++)
                {
                    int tmpNumOfValues = 0;
                    rtMessage request, response;
                    rbus_PopString(wResponse, &pEntryName);
                    rtLog_Debug("Destination %d is %s", i, pEntryName);

                    /* Get the query sent to each component identified */
                    rtMessage_Create(&request);
                    /* Set the Component name that invokes the set */
                    rbus_AppendString(request, pCompInfo->componentName);
                    rbus_AppendInt32(request, 1);
                    rbus_AppendString(request, pParamNames[0]);
                    /* Invoke the method */
                    if((err = rbus_invokeRemoteMethod(pEntryName, METHOD_GETPARAMETERVALUES, request, 60000, &response)) != RTMESSAGE_BUS_SUCCESS)
                    {
                        rtLog_Error("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
                        errorcode = RBUS_ERROR_BUS_ERROR;
                    }
                    else
                    {
                        if (0 == i)
                        {
                            errorcode = _getExt_response_parser(response, &tmpNumOfValues, retProperties);
                            last = *retProperties;
                        }
                        else
                        {
                            rbusProperty_t tmpProperties;
                            errorcode = _getExt_response_parser(response, &tmpNumOfValues, &tmpProperties);
                            rbusProperty_PushBack(last, tmpProperties);
                            last = tmpProperties;
                        }
                    }
                    if (errorcode != RBUS_ERROR_SUCCESS)
                    {
                        rtLog_Warn("Failed to get the data from %s Component", pEntryName);
                        break;
                    }
                    else
                    {
                        *numValues += tmpNumOfValues;
                    }
                }
                rtMessage_Release(wResponse);
                return errorcode;
            }
        }
        else
        {
            rtLog_Debug("Query for expression %s was not successful.", pParamNames[0]);
            return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
    }

    {
        rtMessage request, response;
        rtMessage_Create(&request);
        /* Set the Component name that invokes the set */
        rbus_AppendString(request, pCompInfo->componentName);
        rbus_AppendInt32(request, paramCount);
        for (i = 0; i < paramCount; i++)
            rbus_AppendString(request, pParamNames[i]);

        /* Invoke the method */
        if((err = rbus_invokeRemoteMethod(pParamNames[0], METHOD_GETPARAMETERVALUES, request, 6000, &response)) != RTMESSAGE_BUS_SUCCESS)
        {
            rtLog_Error("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
            errorcode = RBUS_ERROR_BUS_ERROR;
        }
        else
            errorcode = _getExt_response_parser(response, numValues, retProperties);
    }
    return errorcode;
}

static rbusError_t rbus_getByType(rbusHandle_t handle, char const* paramName, void* paramVal, rbusValueType_t type)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;

    if (paramVal && paramName)
    {
        rbusValue_t value;
        
        errorcode = rbus_get(handle, paramName, &value);

        if (errorcode == RBUS_ERROR_SUCCESS)
        {
            if (rbusValue_GetType(value) == type)
            {
                switch(type)
                {
                    case RBUS_INT32:
                        *((int*)paramVal) = rbusValue_GetInt32(value);
                        break;
                    case RBUS_UINT32:
                        *((unsigned int*)paramVal) = rbusValue_GetUInt32(value);
                        break;
                    case RBUS_STRING:
                        *((char**)paramVal) = strdup(rbusValue_GetString(value,NULL));
                        break;
                    default:
                        rtLog_Warn("%s unexpected type param %d", __FUNCTION__, type);
                        break;
                }

                rbusValue_Release(value);
            }
            else
            {
                rtLog_Error("%s rbus_get type missmatch. expected %d. got %d", __FUNCTION__, type, rbusValue_GetType(value));
                errorcode = RBUS_ERROR_BUS_ERROR;
            }
        }
    }
    return errorcode;
}

rbusError_t rbus_getInt(rbusHandle_t handle, char const* paramName, int* paramVal)
{
    return rbus_getByType(handle, paramName, paramVal, RBUS_INT32);
}

rbusError_t rbus_getUint (rbusHandle_t handle, char const* paramName, unsigned int* paramVal)
{
    return rbus_getByType(handle, paramName, paramVal, RBUS_UINT32);
}

rbusError_t rbus_getStr (rbusHandle_t handle, char const* paramName, char** paramVal)
{
    return rbus_getByType(handle, paramName, paramVal, RBUS_STRING);
}

rbusError_t rbus_set(rbusHandle_t handle, char const* name,rbusValue_t value, rbusSetOptions_t* opts)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rtMessage setRequest, setResponse;
    comp_info* pCompInfo = (comp_info*) handle;

    if (value != NULL)
    {
        rtMessage_Create(&setRequest);
        /* Set the Session ID first */
        if ((opts) && (opts->sessionId != 0))
            rbus_AppendInt32(setRequest, opts->sessionId);
        else
            rbus_AppendInt32(setRequest, 0);

        /* Set the Component name that invokes the set */
        rbus_AppendString(setRequest, pCompInfo->componentName);
        /* Set the Size of params */
        rbus_AppendInt32(setRequest, 1);

        /* Set the params in details */
        rbusValue_appendToMessage(name, value, setRequest);

        /* Set the Commit value; FIXME: Should we use string? */
        rbus_AppendString(setRequest, (!opts || opts->commit) ? "TRUE" : "FALSE");

        if((err = rbus_invokeRemoteMethod(name, METHOD_SETPARAMETERVALUES, setRequest, 6000, &setResponse)) != RTMESSAGE_BUS_SUCCESS)
        {
            rtLog_Error("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
            errorcode = RBUS_ERROR_BUS_ERROR;
        }
        else
        {
            rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
            int ret = -1;
            char const* pErrorReason = NULL;
            rbus_PopInt32(setResponse, &ret);

            rtLog_Debug("Response from the remote method is [%d]!", ret);
            errorcode = (rbusError_t) ret;
            legacyRetCode = (rbusLegacyReturn_t) ret;

            if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
            {
                errorcode = RBUS_ERROR_SUCCESS;
                rtLog_Debug("Successfully Set the Value");
            }
            else
            {
                rbus_PopString(setResponse, &pErrorReason);
                rtLog_Warn("Failed to Set the Value for %s", pErrorReason);
            }

            /* Release the reponse message */
            rtMessage_Release(setResponse);
        }
    }
    return errorcode;
}

rbusError_t rbus_setMulti(rbusHandle_t handle, int numProps, rbusProperty_t properties, rbusSetOptions_t* opts)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rtMessage setRequest, setResponse;
    comp_info* pCompInfo = (comp_info*) handle;
    rbusProperty_t current;

    if (numProps > 0 && properties != NULL)
    {
        rtMessage_Create(&setRequest);

        /* Set the Session ID first */
        if ((opts) && (opts->sessionId != 0))
            rbus_AppendInt32(setRequest, opts->sessionId);
        else
            rbus_AppendInt32(setRequest, 0);

        /* Set the Component name that invokes the set */
        rbus_AppendString(setRequest, pCompInfo->componentName);
        /* Set the Size of params */
        rbus_AppendInt32(setRequest, numProps);

        current = properties;
        while(current)
        {
            rbusValue_appendToMessage(rbusProperty_GetName(current), rbusProperty_GetValue(current), setRequest);
            current = rbusProperty_GetNext(current);
        }

        /* Set the Commit value; FIXME: Should we use string? */
        rbus_AppendString(setRequest, (!opts || opts->commit) ? "TRUE" : "FALSE");

        /* TODO: At this point in time, only given Table/Component can be updated with SET/GET..
         * So, passing the elementname as first arg is not a issue for now..
         * We must enhance the rbus in such a way that we shd be able to set across components. Lets revist this area at that time.
         */
#if 0
        /* TODO: First step towards the above comment. When we enhace to support acorss components, this following has to be looped or appropriate count will be passed */
        char const* pElementNames[] = {values[0].name, NULL};
        char** pComponentName = NULL;
        err = rbus_findMatchingObjects(pElementNames, 1, &pComponentName);
        if (err != RTMESSAGE_BUS_SUCCESS)
        {
            rtLog_Info ("Element not found");
            errorcode = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
        else
        {
            rtLog_Info ("Component name is, %s", pComponentName[0]);
            free (pComponentName[0]);
        }
#endif
        if((err = rbus_invokeRemoteMethod(rbusProperty_GetName(properties), METHOD_SETPARAMETERVALUES, setRequest, 6000, &setResponse)) != RTMESSAGE_BUS_SUCCESS)
        {
            rtLog_Error("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
            errorcode = RBUS_ERROR_BUS_ERROR;
        }
        else
        {
            char const* pErrorReason = NULL;
            rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
            int ret = -1;
            rbus_PopInt32(setResponse, &ret);

            rtLog_Debug("Response from the remote method is [%d]!", ret);
            errorcode = (rbusError_t) ret;
            legacyRetCode = (rbusLegacyReturn_t) ret;

            if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
            {
                errorcode = RBUS_ERROR_SUCCESS;
                rtLog_Debug("Successfully Set the Value");
            }
            else
            {
                rbus_PopString(setResponse, &pErrorReason);
                rtLog_Warn("Failed to Set the Value for %s", pErrorReason);
            }

            /* Release the reponse message */
            rtMessage_Release(setResponse);
        }
    }
    return errorcode;
}

#if 0
rbusError_t rbus_setMulti(rbusHandle_t handle, int numValues,
        char const** valueNames, rbusValue_t* values, rbusSetOptions_t* opts)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rtMessage setRequest, setResponse;
    int loopCnt = 0;
    comp_info* pCompInfo = (comp_info*) handle;

    if (values != NULL)
    {
        rtMessage_Create(&setRequest);
        /* Set the Session ID first */
        rbus_AppendInt32(setRequest, 0);
        /* Set the Component name that invokes the set */
        rbus_AppendString(setRequest, pCompInfo->componentName);
        /* Set the Size of params */
        rbus_AppendInt32(setRequest, numValues);

        /* Set the params in details */
        for (loopCnt = 0; loopCnt < numValues; loopCnt++)
        {
            rbusValue_appendToMessage(valueNames[loopCnt], values[loopCnt], setRequest);
        }

        /* Set the Commit value; FIXME: Should we use string? */
        rbus_AppendString(setRequest, (!opts || opts->commit) ? "TRUE" : "FALSE");

        /* TODO: At this point in time, only given Table/Component can be updated with SET/GET..
         * So, passing the elementname as first arg is not a issue for now..
         * We must enhance the rbus in such a way that we shd be able to set across components. Lets revist this area at that time.
         */
#if 0
        /* TODO: First step towards the above comment. When we enhace to support acorss components, this following has to be looped or appropriate count will be passed */
        char const* pElementNames[] = {values[0].name, NULL};
        char** pComponentName = NULL;
        err = rbus_findMatchingObjects(pElementNames, 1, &pComponentName);
        if (err != RTMESSAGE_BUS_SUCCESS)
        {
            rtLog_Info ("Element not found");
            errorcode = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
        else
        {
            rtLog_Info ("Component name is, %s", pComponentName[0]);
            free (pComponentName[0]);
        }
#endif
        if((err = rbus_invokeRemoteMethod(valueNames[0], METHOD_SETPARAMETERVALUES, setRequest, 6000, &setResponse)) != RTMESSAGE_BUS_SUCCESS)
        {
            rtLog_Info("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
            errorcode = RBUS_ERROR_BUS_ERROR;
        }
        else
        {
            char const* pErrorReason = NULL;
            rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
            int ret = -1;
            rbus_PopInt32(setResponse, &ret);

            rtLog_Info("Response from the remote method is [%d]!", ret);
            errorcode = (rbusError_t) ret;
            legacyRetCode = (rbusLegacyReturn_t) ret;

            if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
            {
                errorcode = RBUS_ERROR_SUCCESS;
                rtLog_Info("Successfully Set the Value");
            }
            else
            {
                rbus_PopString(setResponse, &pErrorReason);
                rtLog_Info("Failed to Set the Value for %s", pErrorReason);
            }

            /* Release the reponse message */
            rtMessage_Release(setResponse);
        }
    }
    return errorcode;
}
#endif
rbusError_t rbus_setByType(rbusHandle_t handle, char const* paramName, void const* paramVal, rbusValueType_t type)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;

    if (paramName != NULL)
    {
        rbusValue_t value;

        rbusValue_Init(&value);

        switch(type)
        {
            case RBUS_INT32:
                rbusValue_SetInt32(value, *((int*)paramVal));
                break;
            case RBUS_UINT32:
                rbusValue_SetUInt32(value, *((unsigned int*)paramVal));
                break;
            case RBUS_STRING:
                rbusValue_SetString(value, (char*)paramVal);
                break;
            default:
                rtLog_Warn("%s unexpected type param %d", __FUNCTION__, type);
                break;
        }

        errorcode = rbus_set(handle, paramName, value, NULL);

        rbusValue_Release(value);

    }
    return errorcode;
}

rbusError_t rbus_setInt(rbusHandle_t handle, char const* paramName, int paramVal)
{
    return rbus_setByType(handle, paramName, &paramVal, RBUS_INT32);
}

rbusError_t rbus_setUint(rbusHandle_t handle, char const* paramName, unsigned int paramVal)
{
    return rbus_setByType(handle, paramName, &paramVal, RBUS_UINT32);
}

rbusError_t rbus_setStr(rbusHandle_t handle, char const* paramName, char const* paramVal)
{
    return rbus_setByType(handle, paramName, paramVal, RBUS_STRING);
}

rbusError_t rbusTable_addRow(
    rbusHandle_t handle,
    char const* tableName,
    char const* aliasName,
    uint32_t* instNum)
{
    (void)handle;
    rbus_error_t err;
    int returnCode = 0;
    int32_t instanceId = 0;
    rtMessage request, response;

    rtLog_Info("%s: %s %s", __FUNCTION__, tableName, aliasName);

    rtMessage_Create(&request);
    rbus_AppendInt32(request, 0);/*TODO: this should be the session ID*/
    rbus_AppendString(request, tableName);
    if(aliasName)
        rbus_AppendString(request, aliasName);
    else
        rbus_AppendString(request, "");

    if((err = rbus_invokeRemoteMethod(
        tableName, /*as taken from ccsp_base_api.c, this was the destination component ID, but to locate the route, the table name can be used
                     because the broker simlpy looks at the top level nodes that are owned by a component route.  maybe this breaks if the broker changes*/
        METHOD_ADDTBLROW, 
        request, 
        6000, 
        &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        rtLog_Info("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        rtMessage_Release(response);
        return RBUS_ERROR_BUS_ERROR;
    }

    rbus_PopInt32(response, &returnCode);
    rbus_PopInt32(response, &instanceId); 
    rtMessage_Release(response);

    if(instNum)
        *instNum = (uint32_t)instanceId;/*FIXME we need an rbus_PopUInt32 to avoid loosing a bit */

    rtLog_Info("%s rbus_invokeRemoteMethod success response returnCode:%d instanceId:%d", __FUNCTION__, returnCode, instanceId);

    return returnCode;
}

rbusError_t rbusTable_removeRow(
    rbusHandle_t handle,
    char const* rowName)
{
    (void)handle;
    rbus_error_t err;
    int returnCode = 0;
    rtMessage request, response;

    rtLog_Info("%s: %s", __FUNCTION__, rowName);

    rtMessage_Create(&request);
    rbus_AppendInt32(request, 0);/*TODO: this should be the session ID*/
    rbus_AppendString(request, rowName);

    if((err = rbus_invokeRemoteMethod(
        rowName,
        METHOD_DELETETBLROW, 
        request, 
        6000, 
        &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        rtLog_Info("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        rtMessage_Release(response);
        return RBUS_ERROR_BUS_ERROR;
    }

    rbus_PopInt32(response, &returnCode); //TODO: should we handle this ?
    rtMessage_Release(response);

    rtLog_Info("%s rbus_invokeRemoteMethod success response returnCode:%d", __FUNCTION__, returnCode);

    return returnCode;
}


//************************** Events ****************************//

rbusError_t  rbusEvent_Subscribe(
    rbusHandle_t        handle,
    char const*         eventName,
    rbusEventHandler_t  handler,
    void*               userData)
{
    comp_info* ci = (comp_info*)handle;
    rbus_error_t err;
    rbusEventSubscription_t* sub;

    rtLog_Debug("%s: %s", __FUNCTION__, eventName);

    sub = (rbusEventSubscription_t*)calloc(1, sizeof(rbusEventSubscription_t));
    sub->handle = handle;
    sub->handler = handler;
    sub->userData = userData;
    sub->eventName = strdup(eventName);

    err = rbus_subscribeToEvent(NULL, eventName, _event_callback_handler, sub);

    if(err == RTMESSAGE_BUS_SUCCESS)
    {
        rtVector_PushBack(ci->eventSubs, sub);
    }
    else
    {   
        rbusEventSubscription_free(sub);
        rtLog_Error("rbusEvent_Subscribe failed err=%d", err);
    }

    return err == RTMESSAGE_BUS_SUCCESS ? RBUS_ERROR_SUCCESS: RBUS_ERROR_BUS_ERROR;
}

rbusError_t rbusEvent_Unsubscribe(
    rbusHandle_t        handle,
    char const*         eventName)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    comp_info* ci = (comp_info*)handle;
    rbusEventSubscription_t* sub;

    rtLog_Debug("%s: %s", __FUNCTION__, eventName);

    rbus_unsubscribeFromEvent(NULL, eventName);

    /*the use of rtVector is inefficient here.  I have to loop through the vector to find the sub by name, 
        then call RemoveItem, which loops through again to find the item by address to destroy */
    sub = rbusEventSubscription_find(ci->eventSubs, eventName);
    if(sub)
    {
        rtVector_RemoveItem(ci->eventSubs, sub, rbusEventSubscription_free);
    }
    else
    {
        rtLog_Error("rbusEvent_Unsubscribe unexpected -- we should have found a sub but didn't");

    }
    return errorcode;
}

rbusError_t rbusEvent_SubscribeEx(
    rbusHandle_t                handle,
    rbusEventSubscription_t*    subscription,
    int                         numSubscriptions)
{
    comp_info* ci = (comp_info*)handle;
    rbus_error_t err;
    rbusEventSubscription_t* sub;
    int i, j;

    for(i = 0; i < numSubscriptions; ++i)
    {
        /*
            I'm thinking its best to make a copy of the subscription being passed by user because:
            1. its safer because what if the caller mucks around with the contents later: .e.g. tries to reuse the subscription and changes the eventName
            2. its easier to manage memory because for both rbusEvent_Subscribe and rbusEvent_SubscribeEx because we own the memory and know when to free it
        */
        rtLog_Info("%s: %s", __FUNCTION__, subscription[i].eventName);

        sub = (rbusEventSubscription_t*)calloc(1, sizeof(rbusEventSubscription_t));
        sub->handle = handle;
        sub->handler = subscription[i].handler;
        sub->userData = subscription[i].userData;
        sub->eventName = strdup(subscription[i].eventName);

        err = rbus_subscribeToEvent(NULL, sub->eventName, _event_callback_handler, sub);

        if(err == RTMESSAGE_BUS_SUCCESS)
        {
            rtVector_PushBack(ci->eventSubs, sub);
        }
        else
        {   
            rtLog_Warn("rbusEvent_SubscribeEx failed err=%d", err);

            rbusEventSubscription_free(sub);

            /* So here I'm thinking its best to treat SubscribeEx like a transaction because
                if any subs fails, how will the user know which ones succeeded and which failed ?
                So, as a transaction, we just undo everything, which are all those from 0 to i-1.
            */
            for(j = 0; j < i; ++j)
            {
                rbusEvent_Unsubscribe(handle, subscription[i].eventName);
            }
            return RBUS_ERROR_BUS_ERROR;
        }
    }

    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbusEvent_UnsubscribeEx(
    rbusHandle_t                handle,
    rbusEventSubscription_t*    subscription,
    int                         numSubscriptions)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    comp_info* ci = (comp_info*)handle;
    rbusEventSubscription_t* our_copy;
    int i;

    for(i = 0; i < numSubscriptions; ++i)
    {
        rtLog_Info("%s: %s", __FUNCTION__, subscription[i].eventName);

        rbus_unsubscribeFromEvent(NULL, subscription[i].eventName);

        /*the use of rtVector is inefficient here.  I have to loop through the vector to find the sub by name, 
            then call RemoveItem, which loops through again to find the item by address to destroy */
        our_copy = rbusEventSubscription_find(ci->eventSubs, subscription[i].eventName);
        if(our_copy)
        {
            rtVector_RemoveItem(ci->eventSubs, our_copy, rbusEventSubscription_free);
        }
        else
        {
            rtLog_Warn("rbusEvent_Unsubscribe unexpected -- we should have found a sub but didn't");

        }
    }
    return errorcode;
}

rbusError_t  rbusEvent_Publish(
  rbusHandle_t          handle,
  rbusEvent_t*          eventData)
{
    comp_info* ci = (comp_info*)handle;
    rbus_error_t err, errOut = RTMESSAGE_BUS_SUCCESS;
    rtMessage msg;
    rtListItem listItem;
    rbusSubscription_t* subscription;

    rtLog_Info("%s: %s", __FUNCTION__, eventData->name);

    /*get the node and walk its subscriber list, 
      publishing event to each subscriber*/
    elementNode* el = retrieveInstanceElement(ci->elementRoot, eventData->name);

    if(!el)
    {
        rtLog_Info("rbusEvent_Publish failed: retrieveElement return NULL for %s", eventData->name);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    if(!el->subscriptions)/*nobody subscribed yet*/
    {
        return RTMESSAGE_BUS_SUCCESS;
    }

    rtMessage_Create(&msg);
    rbusEvent_appendToMessage(eventData, msg);

    rtList_GetFront(el->subscriptions, &listItem);
    while(listItem)
    {
        rtListItem_GetData(listItem, (void**)&subscription);
        if(!subscription || !subscription->eventName || !subscription->listener)
        {
            rtLog_Info("rbusEvent_Publish failed: null subscriber data");
            rtMessage_Release(msg);
            return RBUS_ERROR_BUS_ERROR;
        }

        rtLog_Info("rbusEvent_Publish: publising event %s", subscription->eventName);
        err = rbus_publishSubscriberEvent(
            ci->componentName,  
            subscription->eventName/*use the same eventName the consumer subscribed with; not event instance name eventData->name*/, 
            subscription->listener, 
            msg);

        if(err != RTMESSAGE_BUS_SUCCESS)
        {
            if(errOut == RTMESSAGE_BUS_SUCCESS)
                errOut = err;
            rtLog_Info("rbusEvent_Publish faild: rbus_publishSubscriberEvent return error %d", err);
        }
        rtListItem_GetNext(listItem, &listItem);
    }

    rtMessage_Release(msg);

    return errOut == RTMESSAGE_BUS_SUCCESS ? RBUS_ERROR_SUCCESS: RBUS_ERROR_BUS_ERROR;
}

rbusError_t rbus_createSession(rbusHandle_t handle, uint32_t *pSessionId)
{
    (void)handle;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rtMessage response;
    if (pSessionId)
    {
        *pSessionId = 0;
        if((err = rbus_invokeRemoteMethod(RBUS_SMGR_DESTINATION_NAME, RBUS_SMGR_METHOD_REQUEST_SESSION_ID, NULL, 6000, &response)) == RTMESSAGE_BUS_SUCCESS)
        {
            rbus_GetInt32(response, MESSAGE_FIELD_RESULT, (int*) &err);
            if(RTMESSAGE_BUS_SUCCESS != err)
            {
                rtLog_Error("Session manager reports internal error %d.", err);
                rc = RBUS_ERROR_SESSION_ALREADY_EXIST;
            }
            else
            {
                rbus_GetInt32(response, MESSAGE_FIELD_PAYLOAD, (int*) pSessionId);
                rtLog_Info("Received new session id %u", *pSessionId);
            }
        }
        else
        {
            rtLog_Error("Failed to communicated with session manager.");
            rc = RBUS_ERROR_BUS_ERROR;
        }
    }
    else
    {
        rtLog_Warn("Invalid Input passed..");
        rc = RBUS_ERROR_INVALID_INPUT;
    }
    return rc;
}

rbusError_t rbus_getCurrentSession(rbusHandle_t handle, uint32_t *pSessionId)
{
    (void)handle;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rtMessage response;

    if (pSessionId)
    {
        *pSessionId = 0;
        if((err = rbus_invokeRemoteMethod(RBUS_SMGR_DESTINATION_NAME, RBUS_SMGR_METHOD_GET_CURRENT_SESSION_ID, NULL, 6000, &response)) == RTMESSAGE_BUS_SUCCESS)
        {
            rbus_GetInt32(response, MESSAGE_FIELD_RESULT, (int*) &err);
            if(RTMESSAGE_BUS_SUCCESS != err)
            {
                rtLog_Error("Session manager reports internal error %d.", err);
                rc = RBUS_ERROR_SESSION_ALREADY_EXIST;
            }
            else
            {
                rbus_GetInt32(response, MESSAGE_FIELD_PAYLOAD, (int*) pSessionId);
                rtLog_Info("Received new session id %u", *pSessionId);
            }
        }
        else
        {
            rtLog_Error("Failed to communicated with session manager.");
            rc = RBUS_ERROR_BUS_ERROR;
        }
    }
    else
    {
        rtLog_Warn("Invalid Input passed..");
        rc = RBUS_ERROR_INVALID_INPUT;
    }
    return rc;
}

rbusError_t rbus_closeSession(rbusHandle_t handle, uint32_t sessionId)
{
    (void)handle;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;

    if (0 != sessionId)
    {
        rtMessage inputSession;
        rtMessage response;

        rtMessage_Create(&inputSession);
        rbus_SetInt32(inputSession, MESSAGE_FIELD_PAYLOAD, sessionId);
        if((err = rbus_invokeRemoteMethod(RBUS_SMGR_DESTINATION_NAME, RBUS_SMGR_METHOD_END_SESSION, inputSession, 6000, &response)) == RTMESSAGE_BUS_SUCCESS)
        {
            rbus_GetInt32(response, MESSAGE_FIELD_RESULT, (int*) &err);
            if(RTMESSAGE_BUS_SUCCESS != err)
            {
                rtLog_Error("Session manager reports internal error %d.", err);
                rc = RBUS_ERROR_SESSION_ALREADY_EXIST;
            }
            else
                rtLog_Info("Successfully ended session %u.", sessionId);
        }
        else
        {
            rtLog_Error("Failed to communicated with session manager.");
            rc = RBUS_ERROR_BUS_ERROR;
        }
    }
    else
    {
        rtLog_Warn("Invalid Input passed..");
        rc = RBUS_ERROR_INVALID_INPUT;
    }

    return rc;
}

rbusStatus_t rbus_checkStatus(void)
{
    rtLog_SetLevel(RT_LOG_INFO);
    rbuscore_bus_status_t busStatus = rbuscore_checkBusStatus();

    return (rbusStatus_t) busStatus;
}

rbusError_t rbus_registerLogHandler(rbusLogHandler logHandler)
{
    if (logHandler)
    {
        rtLogSetLogHandler ((rtLogHandler) logHandler);
        return RBUS_ERROR_SUCCESS;
    }
    else
    {
        rtLog_Warn("Invalid Input passed..");
        return RBUS_ERROR_INVALID_INPUT;
    }
}

