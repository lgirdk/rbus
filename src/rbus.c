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
#include <pthread.h>
#include <unistd.h>
#include <rtmessage/rtVector.h>
#include <rbus_core.h>
#include <rbus_session_mgr.h>
#include <rbus.h>
#include "rbus_buffer.h"
#include "rbus_element.h"
#include "rbus_valuechange.h"
#include "rbus_subscriptions.h"
#include "rbus_asyncsubscribe.h"
#include "rbus_config.h"
#include "rbus_log.h"
#include "rbus_handle.h"

//******************************* MACROS *****************************************//
#define UNUSED1(a)              (void)(a)
#define UNUSED2(a,b)            UNUSED1(a),UNUSED1(b)
#define UNUSED3(a,b,c)          UNUSED1(a),UNUSED2(b,c)
#define UNUSED4(a,b,c,d)        UNUSED1(a),UNUSED3(b,c,d)
#define UNUSED5(a,b,c,d,e)      UNUSED1(a),UNUSED4(b,c,d,e)
#define UNUSED6(a,b,c,d,e,f)    UNUSED1(a),UNUSED5(b,c,d,e,f)

#define MAX_COMPS_PER_PROCESS               5
#ifndef FALSE
#define FALSE                               0
#endif
#ifndef TRUE
#define TRUE                                1
#endif
//********************************************************************************//


//******************************* STRUCTURES *************************************//

struct _rbusMethodAsyncHandle
{
    rtMessageHeader hdr;
};

struct _rbusHandle handle_array[MAX_COMPS_PER_PROCESS] = {
    {0,"", NULL, NULL, NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL, NULL, NULL},
    {0,"", NULL, NULL, NULL, NULL, NULL}
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
    RBUS_LEGACY_ERR_RESOURCE_EXCEEDED = 9004,
    RBUS_LEGACY_ERR_INVALID_PARAMETER_NAME = 9005,
    RBUS_LEGACY_ERR_INVALID_PARAMETER_TYPE = 9006,
    RBUS_LEGACY_ERR_INVALID_PARAMETER_VALUE = 9007,
    RBUS_LEGACY_ERR_NOT_WRITABLE = 9008,
} rbusLegacyReturn_t;

//********************************************************************************//

//******************************* INTERNAL FUNCTIONS *****************************//
static rbusError_t rbuscoreError_to_rbusError(rtError e)
{
  rbusError_t err;
  switch (e)
  {
    case RTMESSAGE_BUS_SUCCESS:
      err = RBUS_ERROR_SUCCESS;
      break;
    case RTMESSAGE_BUS_ERROR_GENERAL:
      err = RBUS_ERROR_BUS_ERROR;
      break;
    case RTMESSAGE_BUS_ERROR_INVALID_PARAM:
      err = RBUS_ERROR_INVALID_INPUT;
      break;
    case RTMESSAGE_BUS_ERROR_INVALID_STATE:
      err = RBUS_ERROR_SESSION_ALREADY_EXIST;
      break;
    case RTMESSAGE_BUS_ERROR_INSUFFICIENT_MEMORY:
      err = RBUS_ERROR_OUT_OF_RESOURCES;
      break;
    case RTMESSAGE_BUS_ERROR_REMOTE_END_DECLINED_TO_RESPOND:
      err = RBUS_ERROR_DESTINATION_RESPONSE_FAILURE;
      break;
    case RTMESSAGE_BUS_ERROR_REMOTE_END_FAILED_TO_RESPOND:
      err = RBUS_ERROR_DESTINATION_RESPONSE_FAILURE;
      break;
    case RTMESSAGE_BUS_ERROR_REMOTE_TIMED_OUT:
      err = RBUS_ERROR_TIMEOUT;
      break;
    case RTMESSAGE_BUS_ERROR_MALFORMED_RESPONSE:
      err = RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION;
      break;
    case RTMESSAGE_BUS_ERROR_UNSUPPORTED_METHOD:
      err = RBUS_ERROR_INVALID_METHOD;
      break;
    case RTMESSAGE_BUS_ERROR_UNSUPPORTED_EVENT:
      err = RBUS_ERROR_INVALID_EVENT;
      break;
    case RTMESSAGE_BUS_ERROR_OUT_OF_RESOURCES:
      err = RBUS_ERROR_OUT_OF_RESOURCES;
      break;
    case RTMESSAGE_BUS_ERROR_DESTINATION_UNREACHABLE:
      err = RBUS_ERROR_DESTINATION_NOT_REACHABLE;
      break;
    case RTMESSAGE_BUS_SUCCESS_ASYNC:
      err = RBUS_ERROR_ASYNC_RESPONSE;
      break;
    case RTMESSAGE_BUS_SUBSCRIBE_NOT_HANDLED:
      err = RBUS_ERROR_INVALID_OPERATION;
      break;
    default:
      err = RBUS_ERROR_BUS_ERROR;
      break;
  }
  return err;
}

static rbusError_t CCSPError_to_rbusError(rtError e)
{
  rbusError_t err;
  switch (e)
  {
    case RBUS_LEGACY_ERR_SUCCESS:
      err = RBUS_ERROR_SUCCESS;
      break;
    case RBUS_LEGACY_ERR_MEMORY_ALLOC_FAIL:
      err = RBUS_ERROR_OUT_OF_RESOURCES;
      break;
    case RBUS_LEGACY_ERR_FAILURE:
      err = RBUS_ERROR_BUS_ERROR;
      break;
    case RBUS_LEGACY_ERR_NOT_CONNECT:
      err = RBUS_ERROR_OUT_OF_RESOURCES;
      break;
    case RBUS_LEGACY_ERR_TIMEOUT:
      err = RBUS_ERROR_TIMEOUT;
      break;
    case RBUS_LEGACY_ERR_NOT_EXIST:
      err = RBUS_ERROR_DESTINATION_NOT_FOUND;
      break;
    case RBUS_LEGACY_ERR_NOT_SUPPORT:
      err = RBUS_ERROR_BUS_ERROR;
      break;
    case RBUS_LEGACY_ERR_RESOURCE_EXCEEDED:
      err = RBUS_ERROR_OUT_OF_RESOURCES;
      break;
    case RBUS_LEGACY_ERR_INVALID_PARAMETER_NAME:
      err = RBUS_ERROR_INVALID_INPUT;
      break;
    case RBUS_LEGACY_ERR_INVALID_PARAMETER_TYPE:
      err = RBUS_ERROR_INVALID_INPUT;
      break;
    case RBUS_LEGACY_ERR_INVALID_PARAMETER_VALUE:
     err = RBUS_ERROR_INVALID_INPUT;
     break;
    case RBUS_LEGACY_ERR_NOT_WRITABLE:
     err = RBUS_ERROR_INVALID_OPERATION;
     break;
    default:
      err = RBUS_ERROR_BUS_ERROR;
      break;
  }
  return err;
}

static void rbusEventSubscription_free(void* p)
{
    rbusEventSubscription_t* sub = (rbusEventSubscription_t*)p;
    free((void*)sub->eventName);
    if(sub->filter)
    {
        rbusFilter_Release(sub->filter);
    }
    free(sub);
}

static rbusEventSubscription_t* rbusEventSubscription_find(rtVector eventSubs, char const* eventName, rbusFilter_t filter)
{
    /*FIXME - convert to map */
    size_t i;
    for(i=0; i < rtVector_Size(eventSubs); ++i)
    {
        rbusEventSubscription_t* sub = (rbusEventSubscription_t*)rtVector_At(eventSubs, i);
        if(sub && !strcmp(sub->eventName, eventName) && !rbusFilter_Compare(sub->filter, filter))
            return sub;
    }
    RBUSLOG_WARN("rbusEventSubscription_find error: can't find %s", eventName);
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
                RBUSLOG_WARN("RBUS_LEGACY_BASE64_TYPE: Base64 type was never used in CCSP so far. So, Rbus did not support it till now. Since this is the first Base64 query, please report to get it fixed.");
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

void rbusValue_initFromMessage(rbusValue_t* value, rbusMessage msg);
void rbusValue_appendToMessage(char const* name, rbusValue_t value, rbusMessage msg);
void rbusProperty_initFromMessage(rbusProperty_t* property, rbusMessage msg);
void rbusPropertyList_initFromMessage(rbusProperty_t* prop, rbusMessage msg);
void rbusPropertyList_appendToMessage(rbusProperty_t prop, rbusMessage msg);
void rbusObject_initFromMessage(rbusObject_t* obj, rbusMessage msg);
void rbusObject_appendToMessage(rbusObject_t obj, rbusMessage msg);
void rbusEvent_updateFromMessage(rbusEvent_t* event, rbusMessage msg);
void rbusEvent_appendToMessage(rbusEvent_t* event, rbusMessage msg);
void rbusFilter_AppendToMessage(rbusFilter_t filter, rbusMessage msg);
void rbusFilter_InitFromMessage(rbusFilter_t* filter, rbusMessage msg);

void rbusValue_initFromMessage(rbusValue_t* value, rbusMessage msg)
{
    uint8_t const* data;
    uint32_t length;
    int type;
    char const* pBuffer = NULL;

    rbusValue_Init(value);

    rbusMessage_GetInt32(msg, (int*) &type);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> value pop type=%d", type);
#endif
    if(type>=RBUS_LEGACY_STRING && type<=RBUS_LEGACY_NONE)
    {
        rbusMessage_GetString(msg, &pBuffer);
        RBUSLOG_DEBUG("Received Param Value in string : [%s]", pBuffer);
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

            switch(type)
            {
                case RBUS_INT16:
                    rbusMessage_GetInt32(msg, &ival);
                    rbusValue_SetInt16(*value, (int16_t)ival);
                    break;
                case RBUS_UINT16:
                    rbusMessage_GetInt32(msg, &ival);
                    rbusValue_SetUInt16(*value, (uint16_t)ival);
                    break;
                case RBUS_INT32:
                    rbusMessage_GetInt32(msg, &ival);
                    rbusValue_SetInt32(*value, (int32_t)ival);
                    break;
                case RBUS_UINT32:
                    rbusMessage_GetInt32(msg, &ival);
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
                    rbusMessage_GetInt32(msg, &u.i32[0]);
                    rbusMessage_GetInt32(msg, &u.i32[1]);
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
                    rbusMessage_GetInt32(msg, &u.i32[0]);
                    rbusMessage_GetInt32(msg, &u.i32[1]);
                    rbusValue_SetUInt64(*value, u.u64);
                    break;
                }
                case RBUS_SINGLE:
                    rbusMessage_GetDouble(msg, &fval);
                    rbusValue_SetSingle(*value, (float)fval);
                    break;
                case RBUS_DOUBLE:
                    rbusMessage_GetDouble(msg, &fval);
                    rbusValue_SetDouble(*value, (double)fval);
                    break;
                case RBUS_DATETIME:
                    rbusMessage_GetBytes(msg, &data, &length);
                    rbusValue_SetTLV(*value, type, length, data);
                    break;
                default:
                    rbusMessage_GetBytes(msg, &data, &length);
                    rbusValue_SetTLV(*value, type, length, data);
                    break;
            }

#if DEBUG_SERIALIZER
            char* sv = rbusValue_ToString(*value,0,0);
            RBUSLOG_INFO("> value pop data=%s", sv);
            free(sv);
#endif
        }
    }
}

void rbusProperty_initFromMessage(rbusProperty_t* property, rbusMessage msg)
{
    char const* name;
    rbusValue_t value;

    rbusMessage_GetString(msg, (char const**) &name);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> prop pop name=%s", name);
#endif
    rbusProperty_Init(property, name, NULL);
    rbusValue_initFromMessage(&value, msg);
    rbusProperty_SetValue(*property, value);
    rbusValue_Release(value);
}

void rbusEvent_updateFromMessage(rbusEvent_t* event, rbusMessage msg)
{
    char const* name;
    int type;
    rbusObject_t data;

    rbusMessage_GetString(msg, (char const**) &name);
    rbusMessage_GetInt32(msg, (int*) &type);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> event pop name=%s type=%d", name, type);
#endif

    rbusObject_initFromMessage(&data, msg);

    event->name = name;
    event->type = type;
    event->data = data;/*caller must call rbusValue_Release*/
}

void rbusPropertyList_appendToMessage(rbusProperty_t prop, rbusMessage msg)
{
    int numProps = 0;
    rbusProperty_t first = prop;
    while(prop)
    {
        numProps++;
        prop = rbusProperty_GetNext(prop);
    }
    rbusMessage_SetInt32(msg, numProps);/*property count*/
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> prop add numProps=%d", numProps);
#endif
    prop = first;
    while(prop)
    {
        rbusValue_appendToMessage(rbusProperty_GetName(prop), rbusProperty_GetValue(prop), msg);
        prop = rbusProperty_GetNext(prop);
    }
}

void rbusPropertyList_initFromMessage(rbusProperty_t* prop, rbusMessage msg)
{
    rbusProperty_t previous = NULL, first = NULL;
    int numProps = 0;
    rbusMessage_GetInt32(msg, (int*) &numProps);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> prop pop numProps=%d", numProps);
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

void rbusObject_appendToMessage(rbusObject_t obj, rbusMessage msg)
{
    int numChild = 0;
    rbusObject_t child;

    rbusMessage_SetString(msg, rbusObject_GetName(obj));/*object name*/
    rbusMessage_SetInt32(msg, rbusObject_GetType(obj));/*object type*/
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> object add name=%s type=%d", rbusObject_GetName(obj), rbusObject_GetType(obj));
#endif
    rbusPropertyList_appendToMessage(rbusObject_GetProperties(obj), msg);

    child = rbusObject_GetChildren(obj);
    numChild = 0;
    while(child)
    {
        numChild++;
        child = rbusObject_GetNext(child);
    }
    rbusMessage_SetInt32(msg, numChild);/*object child object count*/
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> object add numChild=%d", numChild);
#endif
    child = rbusObject_GetChildren(obj);
    while(child)
    {
        rbusObject_appendToMessage(child, msg);/*object child object*/
        child = rbusObject_GetNext(child);
    }
}


void rbusObject_initFromMessage(rbusObject_t* obj, rbusMessage msg)
{
    char const* name;
    int type;
    int numChild = 0;
    rbusProperty_t prop;
    rbusObject_t children=NULL, previous=NULL;

    rbusMessage_GetString(msg, &name);
    rbusMessage_GetInt32(msg, &type);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> object pop name=%s type=%d", name, type);
#endif

    rbusPropertyList_initFromMessage(&prop, msg);

    rbusMessage_GetInt32(msg, &numChild);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> object pop numChild=%d", numChild);
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

void rbusValue_appendToMessage(char const* name, rbusValue_t value, rbusMessage msg)
{
    rbusValueType_t type = RBUS_NONE;

    rbusMessage_SetString(msg, name);

    if(value)
        type = rbusValue_GetType(value);
    rbusMessage_SetInt32(msg, type);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> value add name=%s type=%d", name, type);
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
                rbusMessage_SetInt32(msg, (int32_t)rbusValue_GetInt16(value));
                break;
            case RBUS_UINT16:
                rbusMessage_SetInt32(msg, (int32_t)rbusValue_GetUInt16(value));
                break;
            case RBUS_INT32:
                rbusMessage_SetInt32(msg, (int32_t)rbusValue_GetInt32(value));
                break;
            case RBUS_UINT32:
                rbusMessage_SetInt32(msg, (int32_t)rbusValue_GetUInt32(value));
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
                rbusMessage_SetInt32(msg, (int32_t)u.i32[0]);
                rbusMessage_SetInt32(msg, (int32_t)u.i32[1]);
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
                rbusMessage_SetInt32(msg, (int32_t)u.i32[0]);
                rbusMessage_SetInt32(msg, (int32_t)u.i32[1]);
                break;
            }
            case RBUS_SINGLE:
                rbusMessage_SetDouble(msg, (double)rbusValue_GetSingle(value));
                break;
            case RBUS_DOUBLE:
                rbusMessage_SetDouble(msg, (double)rbusValue_GetDouble(value));
                break;
            case RBUS_DATETIME:
                rbusMessage_SetBytes(msg, rbusValue_GetV(value), rbusValue_GetL(value));
                break;
            default:
            {
                uint8_t const* buff = NULL;
                uint32_t len = 0;
                if(value)
                {
                    buff = rbusValue_GetV(value);
                    len = rbusValue_GetL(value);
                }
                rbusMessage_SetBytes(msg, buff, len);
                break;
            }
        }
#if DEBUG_SERIALIZER
        char* sv = rbusValue_ToString(value,0,0);
        RBUSLOG_INFO("> value add data=%s", sv);
        free(sv);
#endif

    }
}

void rbusEvent_appendToMessage(rbusEvent_t* event, rbusMessage msg)
{
    rbusMessage_SetString(msg, event->name);
    rbusMessage_SetInt32(msg, event->type);
#if DEBUG_SERIALIZER
    RBUSLOG_INFO("> event add name=%s type=%d", event->name, event->type);
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
            RBUSLOG_DEBUG("Event or Method is Queried");
            return false;
        }
    }
    else
    {
        RBUSLOG_DEBUG("Null Pointer sent for Query");
        return false;
    }

    return true;

}

void rbusFilter_AppendToMessage(rbusFilter_t filter, rbusMessage msg)
{
    rbusMessage_SetInt32(msg, rbusFilter_GetType(filter));
    if(rbusFilter_GetType(filter) == RBUS_FILTER_EXPRESSION_RELATION)
    {
        rbusMessage_SetInt32(msg, rbusFilter_GetRelationOperator(filter));
        rbusValue_appendToMessage("filter", rbusFilter_GetRelationValue(filter), msg);
    }
    else if(rbusFilter_GetType(filter) == RBUS_FILTER_EXPRESSION_LOGIC)
    {
        rbusMessage_SetInt32(msg, rbusFilter_GetLogicOperator(filter));
        rbusFilter_AppendToMessage(rbusFilter_GetLogicLeft(filter), msg);
        if(rbusFilter_GetLogicOperator(filter) != RBUS_FILTER_OPERATOR_NOT)
            rbusFilter_AppendToMessage(rbusFilter_GetLogicRight(filter), msg);
    }
}

void rbusFilter_InitFromMessage(rbusFilter_t* filter, rbusMessage msg)
{
    int32_t type;
    int32_t op;

    rbusMessage_GetInt32(msg, &type);

    if(type == RBUS_FILTER_EXPRESSION_RELATION)
    {
        char const* name;
        rbusValue_t val;
        rbusMessage_GetInt32(msg, &op);
        rbusMessage_GetString(msg, &name);
        rbusValue_initFromMessage(&val, msg);
        rbusFilter_InitRelation(filter, op, val);
        rbusValue_Release(val);
    }
    else if(type == RBUS_FILTER_EXPRESSION_LOGIC)
    {
        rbusFilter_t left = NULL, right = NULL;
        rbusMessage_GetInt32(msg, &op);
        rbusFilter_InitFromMessage(&left, msg);
        if(op != RBUS_FILTER_OPERATOR_NOT)
            rbusFilter_InitFromMessage(&right, msg);
        rbusFilter_InitLogic(filter, op, left, right);
        rbusFilter_Release(left);
        if(right)
            rbusFilter_Release(right);
    }
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
            RBUSLOG_DEBUG("The Query is having wildcard.. ");
            return true;
        }
    }
    else
    {
        RBUSLOG_DEBUG("Null Pointer sent for Query");
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

int subscribeHandlerImpl(rbusHandle_t handle, bool added, elementNode* el, char const* eventName, char const* listener, int32_t interval, int32_t duration, rbusFilter_t filter)
{
    rbusSubscription_t* subscription = NULL;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    bool autoPublish = true;

    /* call the provider subHandler first to see if it overrides autoPublish */
    if(el->cbTable.eventSubHandler)
    {
        rbusError_t err;
        rbusEventSubAction_t action;
        if(added)
            action = RBUS_EVENT_ACTION_SUBSCRIBE;
        else
            action = RBUS_EVENT_ACTION_UNSUBSCRIBE;

        err = el->cbTable.eventSubHandler(handle, action, eventName, filter, interval, &autoPublish);

        if(err != RBUS_ERROR_SUCCESS)
        {
            RBUSLOG_DEBUG("%s provider subHandler return err=%d", __FUNCTION__, err);

            return err;
        }
    }

    if(added)
    {
        subscription = rbusSubscriptions_addSubscription(handleInfo->subscriptions, listener, eventName, filter, interval, duration, autoPublish, el);

        if(!subscription)
        {
            return RTMESSAGE_BUS_ERROR_INVALID_STATE; /*unexpected*/
        }
    }
    else
    {
        subscription = rbusSubscriptions_getSubscription(handleInfo->subscriptions, listener, eventName, filter);

        if(!subscription)
        {
            RBUSLOG_INFO("unsubscribing from event which isn't currectly subscribed to event=%s listener=%s", eventName, listener);
            return RTMESSAGE_BUS_ERROR_INVALID_PARAM; /*unsubscribing from event which isn't currectly subscribed to*/
        }
    }

    /* if autoPublish and its a property being subscribed to
       then update rbusValueChange to handle the property */
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
                RBUSLOG_INFO("%s: ValueChange %s event=%s prop=%s", __FUNCTION__, 
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
        rbusSubscriptions_removeSubscription(handleInfo->subscriptions, subscription);
    }
    return RTMESSAGE_BUS_SUCCESS;
}

static void registerTableRow (rbusHandle_t handle, elementNode* tableInstElem, char const* tableName, char const* aliasName, uint32_t instNum)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    elementNode* rowElem;

    RBUSLOG_DEBUG("%s table [%s] alias [%s] instNum [%u]", __FUNCTION__, tableName, aliasName, instNum);

    rowElem = instantiateTableRow(tableInstElem, instNum, aliasName);

    rbusSubscriptions_onTableRowAdded(handleInfo->subscriptions, rowElem);

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
        rbusValue_SetString(aliasVal, aliasName ? aliasName : "");

        rbusObject_Init(&data, NULL);
        rbusObject_SetValue(data, "rowName", rowNameVal);
        rbusObject_SetValue(data, "instNum", instNumVal);
        rbusObject_SetValue(data, "alias", aliasVal);

        event.name = tableName;
        event.type = RBUS_EVENT_OBJECT_CREATED;
        event.data = data;

        RBUSLOG_INFO("%s publishing ObjectCreated table=%s rowName=%s", __FUNCTION__, tableName, rowElem->fullName);
        respub = rbusEvent_Publish(handle, &event);

        if(respub != RBUS_ERROR_SUCCESS)
        {
            RBUSLOG_WARN("failed to publish ObjectCreated event err:%d", respub);
        }

        rbusValue_Release(rowNameVal);
        rbusValue_Release(instNumVal);
        rbusValue_Release(aliasVal);
        rbusObject_Release(data);
    }
}

static void unregisterTableRow (rbusHandle_t handle, elementNode* rowInstElem)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    char* rowInstName = strdup(rowInstElem->fullName); /*must dup because we are deleting the instance*/
    elementNode* tableInstElem = rowInstElem->parent;

    RBUSLOG_DEBUG("%s [%s]", __FUNCTION__, rowInstElem->fullName);

    /*update ValueChange before rbusSubscriptions_onTableRowRemoved */
    valueChangeTableRowUpdate(handle, rowInstElem, false);

    rbusSubscriptions_onTableRowRemoved(handleInfo->subscriptions, rowInstElem);

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
        RBUSLOG_INFO("%s publishing ObjectDeleted table=%s rowName=%s", __FUNCTION__, tableInstElem->fullName, rowInstName);
        respub = rbusEvent_Publish(handle, &event);

        rbusValue_Release(rowNameVal);
        rbusObject_Release(data);
        free(rowInstName);

        if(respub != RBUS_ERROR_SUCCESS)
        {
            RBUSLOG_WARN("failed to publish ObjectDeleted event err:%d", respub);
        }
    }
}
//******************************* CALLBACKS *************************************//
static int _event_subscribe_callback_handler(char const* object,  char const* eventName, char const* listener, int added, const rbusMessage payload, void* userData)
{
    rbusHandle_t handle = (rbusHandle_t)userData;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)userData;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;

    UNUSED1(object);

    RBUSLOG_DEBUG("%s: event subscribe callback for [%s] event!", __FUNCTION__, eventName);

    elementNode* el = retrieveInstanceElement(handleInfo->elementRoot, eventName);

    if(el)
    {
        int32_t interval = 0;
        int32_t duration = 0;
        rbusFilter_t filter = NULL;

        /* copy the optional filter */
        if(payload)
        {
            int hasFilter;
            rbusMessage_GetInt32(payload, &interval);
            rbusMessage_GetInt32(payload, &duration);
            rbusMessage_GetInt32(payload, &hasFilter);
            if(hasFilter)
            {
                rbusFilter_InitFromMessage(&filter, payload);
            }
        }

        RBUSLOG_DEBUG("%s: found element of type %d", __FUNCTION__, el->type);

        err = subscribeHandlerImpl(handle, added, el, eventName, listener, interval, duration, filter);

        if(filter)
        {
            rbusFilter_Release(filter);
        }
    }
    else
    {
        RBUSLOG_WARN("event subscribe callback: unexpected! element not found");
        err = RTMESSAGE_BUS_ERROR_UNSUPPORTED_EVENT;
    }
    return err;
}

static void _client_disconnect_callback_handler(const char * listener)
{
    int i;
    for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
    {
        if(handle_array[i].inUse)
        {
            if(handle_array[i].subscriptions)
            {
                rbusSubscriptions_handleClientDisconnect(&handle_array[i], handle_array[i].subscriptions, listener);
            }
        }
    }

}

void _subscribe_async_callback_handler(rbusHandle_t handle, rbusEventSubscription_t* subscription, rbusError_t error)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;

    subscription->asyncHandler(subscription->handle, subscription, error);

    if(error == RBUS_ERROR_SUCCESS)
    {
        rtVector_PushBack(handleInfo->eventSubs, subscription);
    }
    else
    {
        rbusEventSubscription_free(subscription);
    }
}

int _event_callback_handler (char const* objectName, char const* eventName, rbusMessage message, void* userData)
{
    rbusEventSubscription_t* subscription;
    rbusEventHandler_t handler;
    rbusEvent_t event;

    RBUSLOG_DEBUG("Received event callback: objectName=%s eventName=%s", 
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

static void _set_callback_handler (rbusHandle_t handle, rbusMessage request, rbusMessage *response)
{
    rbusError_t rc = 0;
    int sessionId = 0;
    int numVals = 0;
    int loopCnt = 0;
    char* pCompName = NULL;
    char* pIsCommit = NULL;
    char const* pFailedElement = NULL;
    rbusProperty_t* pProperties = NULL;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    rbusSetHandlerOptions_t opts;

    memset(&opts, 0, sizeof(opts));

    rbusMessage_GetInt32(request, &sessionId);
    rbusMessage_GetString(request, (char const**) &pCompName);
    rbusMessage_GetInt32(request, &numVals);

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

        rbusMessage_GetString(request, (char const**) &pIsCommit);

        /* Since we set as string, this needs to compared with string..
         * Otherwise, just the #define in the top for TRUE/FALSE should be used.
         */
        if (strncasecmp("TRUE", pIsCommit, 4) == 0)
            opts.commit = true;

        for (loopCnt = 0; loopCnt < numVals; loopCnt++)
        {
            /* Retrive the element node */
            char const* paramName = rbusProperty_GetName(pProperties[loopCnt]);
            el = retrieveInstanceElement(handleInfo->elementRoot, paramName);
            if(el != NULL)
            {
                if(el->cbTable.setHandler)
                {
                    rc = el->cbTable.setHandler(handle, pProperties[loopCnt], &opts);
                    if (rc != RBUS_ERROR_SUCCESS)
                    {
                        RBUSLOG_WARN("Set Failed for %s; Component Owner returned Error", paramName);
                        pFailedElement = paramName;
                        break;
                    }
                    else
                    {
                        setPropertyChangeComponent(el, pCompName);
                    }
                }
                else
                {
                    RBUSLOG_WARN("Set Failed for %s; No Handler found", paramName);
                    rc = RBUS_ERROR_INVALID_OPERATION;
                    pFailedElement = paramName;
                    break;
                }
            }
            else
            {
                RBUSLOG_WARN("Set Failed for %s; No Element registered", paramName);
                rc = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
                pFailedElement = paramName;
                break;
            }
        }
    }
    else
    {
        RBUSLOG_WARN("Set Failed as %s did not send any input", pCompName);
        rc = RBUS_ERROR_INVALID_INPUT;
        pFailedElement = pCompName;
    }

    rbusMessage_Init(response);
    rbusMessage_SetInt32(*response, (int) rc);
    if (pFailedElement)
        rbusMessage_SetString(*response, pFailedElement);

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

/*
    convert a registration element name to a instance name based on the instance numbers in the original 
    partial path query
    example:
    registration name like: Device.Services.VoiceService.{i}.X_BROADCOM_COM_Announcement.ServerAddress
    and query name like   : Device.Services.VoiceService.1.X_BROADCOM_COM_Announcement.
    final name should be  : Device.Services.VoiceService.1.X_BROADCOM_COM_Announcement.ServerAddress
 */
static char const* _convert_reg_name_to_instance_name(char const* registrationName, char const* query, char* buffer)
{
    int idx = 0;
    char const* preg = registrationName;
    char const* pinst= query;

    while(*preg != 0 && *pinst != 0)
    {
        while(*preg == *pinst)
        {
            buffer[idx++] = *preg;
            preg++;
            pinst++;
        }
        
        if(*preg == '{')
        {
            while(*pinst && *pinst != '.')
                buffer[idx++] = *pinst++;

            while(*preg && *preg != '.')
                preg++;
        }

        if(*pinst == 0) /*end of partial path but continue to write out the full name of child*/
        {
            while(*preg)
            {
                buffer[idx++] = *preg++;
            }
        }
        /*
         *---------------------------------------------------------------------------------------------------
         * In the Registered Name, whereever {i} is present, must be replaced with the number from the Query.
         * We should not be changing the registered name with additional info from queries.
         * Ex: When query received for the subtables,
         *      RegisteredName  : Device.Tables1.T1.{i}.T2
         *      Query           : Device.Tables1.T1.2.T2.
         *      Final should be : Device.Tables1.T1.2.T2
         * If we enable below code, it will change the final buffer as below which is wrong.
         *                        Device.Tables1.T1.2.T2.
         *---------------------------------------------------------------------------------------------------
         * In short, the input query is used only to find & replace {i} in the registered name.
         * So removed the below code
         *---------------------------------------------------------------------------------------------------
         */
#if 0
        else if(*preg == 0) /*end of query path but continue to write out the full name of child*/
        {
            while(*pinst)
            {
                buffer[idx++] = *pinst++;
            }
        }
#endif
    }
    buffer[idx++] = 0;

    RBUSLOG_DEBUG(" _convert_reg_name_to_instance_name");
    RBUSLOG_DEBUG("   reg: %s", registrationName);
    RBUSLOG_DEBUG(" query: %s", query);
    RBUSLOG_DEBUG(" final: %s", buffer);

    return buffer;
}

/*
    node can be either an instance node or a registration node (if an instance node doesn't exist).
    query will be set if node is a registration node, so that registration names can be converted to instance names
 */
static void _get_recursive_wildcard_handler(elementNode* node, char const* query, rbusHandle_t handle, const char* pRequestingComp, rbusProperty_t properties, int *pCount, int level)
{
    rbusGetHandlerOptions_t options;
    memset(&options, 0, sizeof(options));

    /* Update the Get Handler input options */
    options.requestingComponent = pRequestingComp;

    RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler node=%s type=%d query=%s", level*4, " ", node ? node->fullName : "NULL", node ? node->type : 0, query ? query : "NULL");

    if (node != NULL)
    {
        /*if table getHandler, then pass the query to it and stop recursion*/
        if((node->type == RBUS_ELEMENT_TYPE_TABLE) && (node->cbTable.getHandler))
        {
            rbusError_t result;
            rbusProperty_t tmpProperties;
            char instanceName[RBUS_MAX_NAME_LENGTH];
            char partialPath[RBUS_MAX_NAME_LENGTH];

            snprintf(partialPath, RBUS_MAX_NAME_LENGTH-1, "%s.", 
                     query ? _convert_reg_name_to_instance_name(node->fullName, query, instanceName) : node->fullName);

            RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler calling table getHandler partialPath=%s", level*4, " ", partialPath);

            rbusProperty_Init(&tmpProperties, partialPath, NULL);

            result = node->cbTable.getHandler(handle, tmpProperties, &options);

            if (result == RBUS_ERROR_SUCCESS )
            {
                int count = rbusProperty_Count(tmpProperties);

                RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler table getHandler returned %d properties", level*4, " ", count-1);

                /*the first property is just the partialPath we passed in */
                if(count > 1)
                {
                    /*take the second property, which is a list*/
                    rbusProperty_PushBack(properties, rbusProperty_GetNext(tmpProperties));
                    *pCount += count - 1;
                }
            }
            else
            {
                RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler table getHandler failed rc=%d", level*4, " ", result);
            }
            rbusProperty_Release(tmpProperties);
            return;
        }

        elementNode* child = node->child;

        while(child)
        {
            if((child->type == RBUS_ELEMENT_TYPE_PROPERTY) && (child->cbTable.getHandler))
            {
                rbusError_t result;
                char instanceName[RBUS_MAX_NAME_LENGTH];
                rbusProperty_t tmpProperties;

                RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler calling property getHandler node=%s", level*4, " ", child->fullName);

                rbusProperty_Init(&tmpProperties, query ? _convert_reg_name_to_instance_name(child->fullName, query, instanceName) : child->fullName, NULL);
                result = child->cbTable.getHandler(handle, tmpProperties, &options);
                if (result == RBUS_ERROR_SUCCESS)
                {
                    rbusProperty_PushBack(properties, tmpProperties);
                    *pCount += 1;
                }
                rbusProperty_Release(tmpProperties);
            }
            /*recurse into children that are not row templates without table getHandler*/
            else if( child->child && !(child->parent->type == RBUS_ELEMENT_TYPE_TABLE && strcmp(child->name, "{i}") == 0 && child->cbTable.getHandler == NULL) )
            {
                RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler recurse into %s", level*4, " ", child->fullName);
                _get_recursive_wildcard_handler(child, query, handle, pRequestingComp, properties, pCount, level+1);
            }
            else
            {
                RBUSLOG_DEBUG("%*s_get_recursive_wildcard_handler skipping %s", level*4, " ", child->fullName);
            }

            child = child->nextSibling;
        }
    }
}

static void _get_callback_handler (rbusHandle_t handle, rbusMessage request, rbusMessage *response)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    int paramSize = 1, i = 0;
    rbusError_t result = RBUS_ERROR_SUCCESS;
    char const *parameterName = NULL;
    char const *pCompName = NULL;
    rbusProperty_t* properties = NULL;
    rbusGetHandlerOptions_t options;

    memset(&options, 0, sizeof(options));
    rbusMessage_GetString(request, &pCompName);
    rbusMessage_GetInt32(request, &paramSize);

    RBUSLOG_DEBUG("Param Size [%d]", paramSize);

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
            rbusMessage_GetString(request, &parameterName);

            RBUSLOG_DEBUG("Param Name [%d]:[%s]", i, parameterName);

            rbusProperty_SetName(properties[i], parameterName);

            /* Check for wildcard query */
            int length = strlen(parameterName) - 1;
            if (parameterName[length] == '.')
            {
                int hasInstance = 1;
                RBUSLOG_DEBUG("handle the wildcard request..");
                rbusMessage_Init(response);

                el = retrieveInstanceElement(handleInfo->elementRoot, parameterName);
                if (el != NULL)
                {
                    rbusProperty_t xproperties, first;
                    rbusValue_t xtmp;
                    int count = 0;

                    if(strstr(el->fullName, "{i}"))
                        hasInstance = 0;
                        
                    rbusValue_Init(&xtmp);
                    rbusValue_SetString(xtmp, "tmpValue");
                    rbusProperty_Init(&xproperties, "tmpProp", xtmp);
                    rbusValue_Release(xtmp);
                    _get_recursive_wildcard_handler(el, hasInstance ? NULL : parameterName, handle, pCompName, xproperties, &count, 0);
                    RBUSLOG_DEBUG("We have identified %d entries that are matching the request and got the value. Lets return it.", count);

                    if (count > 0)
                    {
                        first = rbusProperty_GetNext(xproperties);
                        rbusMessage_SetInt32(*response, (int) RBUS_ERROR_SUCCESS);
                        rbusMessage_SetInt32(*response, count);
                        for(i = 0; i < count; i++)
                        {
                            rbusValue_appendToMessage(rbusProperty_GetName(first), rbusProperty_GetValue(first), *response);
                            first = rbusProperty_GetNext(first);
                        }
                    }
                    else
                    {
                        rbusMessage_SetInt32(*response, (int) RBUS_ERROR_ELEMENT_DOES_NOT_EXIST);
                    }
                    /* Release the memory */
                    rbusProperty_Release(xproperties);
                }
                else
                {
                    rbusMessage_SetInt32(*response, (int) RBUS_ERROR_ELEMENT_DOES_NOT_EXIST);
                }

                /* Free the memory, regardless of success or not.. */
                for (i = 0; i < paramSize; i++)
                {
                    rbusProperty_Release(properties[i]);
                }
                free (properties);

                return;
            }

            //Do a look up and call the corresponding method
            el = retrieveInstanceElement(handleInfo->elementRoot, parameterName);
            if(el != NULL)
            {
                RBUSLOG_DEBUG("Retrieved [%s]", parameterName);

                if(el->cbTable.getHandler)
                {
                    RBUSLOG_DEBUG("Table and CB exists for [%s], call the CB!", parameterName);

                    result = el->cbTable.getHandler(handle, properties[i], &options);

                    if (result != RBUS_ERROR_SUCCESS)
                    {
                        RBUSLOG_WARN("called CB with result [%d]", result);
                        break;
                    }
                }
                else
                {
                    RBUSLOG_WARN("Element retrieved, but no cb installed for [%s]!", parameterName);
                    result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
                    break;
                }
            }
            else
            {
                RBUSLOG_WARN("Not able to retrieve element [%s]", parameterName);
                result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
                break;
            }
        }

        rbusMessage_Init(response);
        rbusMessage_SetInt32(*response, (int) result);
        if (result == RBUS_ERROR_SUCCESS)
        {
            rbusMessage_SetInt32(*response, paramSize);
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
        RBUSLOG_WARN("Get Failed as %s did not send any input", pCompName);
        result = RBUS_ERROR_INVALID_INPUT;
        rbusMessage_Init(response);
        rbusMessage_SetInt32(*response, (int) result);
    }

    return;
}

static void _table_add_row_callback_handler (rbusHandle_t handle, rbusMessage request, rbusMessage* response)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    rbusError_t result = RBUS_ERROR_BUS_ERROR;
    int sessionId;
    char const* tableName;
    char const* aliasName = NULL;
    int err;
    uint32_t instNum = 0;

    rbusMessage_GetInt32(request, &sessionId);
    rbusMessage_GetString(request, &tableName);
    err = rbusMessage_GetString(request, &aliasName); /*this presumes rbus_updateTable sent the alias.  
                                                 if CCSP/dmcli is calling us, then this will be NULL*/
    if(err != RT_OK || (aliasName && strlen(aliasName)==0))
        aliasName = NULL;

    RBUSLOG_DEBUG("%s table [%s] alias [%s] err [%d]", __FUNCTION__, tableName, aliasName, err);

    elementNode* tableRegElem = retrieveElement(handleInfo->elementRoot, tableName);
    elementNode* tableInstElem = retrieveInstanceElement(handleInfo->elementRoot, tableName);

    if(tableRegElem && tableInstElem)
    {
        if(tableRegElem->cbTable.tableAddRowHandler)
        {
            RBUSLOG_INFO("%s calling tableAddRowHandler table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);

            result = tableRegElem->cbTable.tableAddRowHandler(handle, tableName, aliasName, &instNum);

            if (result == RBUS_ERROR_SUCCESS)
            {
                registerTableRow(handle, tableInstElem, tableName, aliasName, instNum);
            }
            else
            {
                RBUSLOG_WARN("%s tableAddRowHandler failed table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);
            }
        }
        else
        {
            RBUSLOG_WARN("%s tableAddRowHandler not registered table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);
            result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
        }
    }
    else
    {
        RBUSLOG_WARN("%s no element found table [%s] alias [%s]", __FUNCTION__, tableName, aliasName);
        result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    rbusMessage_Init(response);
    rbusMessage_SetInt32(*response, result);
    rbusMessage_SetInt32(*response, (int32_t)instNum);
}

static void _table_remove_row_callback_handler (rbusHandle_t handle, rbusMessage request, rbusMessage* response)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    rbusError_t result = RBUS_ERROR_BUS_ERROR;
    int sessionId;
    char const* rowName;

    rbusMessage_GetInt32(request, &sessionId);
    rbusMessage_GetString(request, &rowName);

    RBUSLOG_DEBUG("%s row [%s]", __FUNCTION__, rowName);

    /*get the element for the row */
    elementNode* rowRegElem = retrieveElement(handleInfo->elementRoot, rowName);
    elementNode* rowInstElem = retrieveInstanceElement(handleInfo->elementRoot, rowName);

    if(rowRegElem && rowInstElem)
    {
        /*switch to the row's table */
        elementNode* tableRegElem = rowRegElem->parent;
        elementNode* tableInstElem = rowInstElem->parent;

        if(tableRegElem && tableInstElem)
        {
            if(tableRegElem->cbTable.tableRemoveRowHandler)
            {
                RBUSLOG_INFO("%s calling tableRemoveRowHandler row [%s]", __FUNCTION__, rowName);

                result = tableRegElem->cbTable.tableRemoveRowHandler(handle, rowName);

                if (result == RBUS_ERROR_SUCCESS)
                {
                    unregisterTableRow(handle, rowInstElem);
                }
                else
                {
                    RBUSLOG_WARN("%s tableRemoveRowHandler failed row [%s]", __FUNCTION__, rowName);
                }
            }
            else
            {
                RBUSLOG_INFO("%s tableRemoveRowHandler not registered row [%s]", __FUNCTION__, rowName);
                result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
            }
        }
        else
        {
            RBUSLOG_WARN("%s no parent element found row [%s]", __FUNCTION__, rowName);
            result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
    }
    else
    {
        RBUSLOG_WARN("%s no element found row [%s]", __FUNCTION__, rowName);
        result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    rbusMessage_Init(response);
    rbusMessage_SetInt32(*response, result);
}

static int _method_callback_handler(rbusHandle_t handle, rbusMessage request, rbusMessage* response, const rtMessageHeader* hdr)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    rbusError_t result = RBUS_ERROR_BUS_ERROR;
    int sessionId;
    char const* methodName;
    rbusObject_t inParams, outParams;

    rbusMessage_GetInt32(request, &sessionId);
    rbusMessage_GetString(request, &methodName);
    rbusObject_initFromMessage(&inParams, request);

    RBUSLOG_INFO("%s method [%s]", __FUNCTION__, methodName);

    /*get the element for the row */
    elementNode* methRegElem = retrieveElement(handleInfo->elementRoot, methodName);
    elementNode* methInstElem = retrieveInstanceElement(handleInfo->elementRoot, methodName);

    if(methRegElem && methInstElem)
    {
        if(methRegElem->cbTable.methodHandler)
        {
            RBUSLOG_INFO("%s calling methodHandler method [%s]", __FUNCTION__, methodName);

            rbusObject_Init(&outParams, NULL);

            rbusMethodAsyncHandle_t asyncHandle = malloc(sizeof(struct _rbusMethodAsyncHandle));
            asyncHandle->hdr = *hdr;

            result = methRegElem->cbTable.methodHandler(handle, methodName, inParams, outParams, asyncHandle);
            
            if (result == RBUS_ERROR_ASYNC_RESPONSE)
            {
                /*outParams will be sent async*/
                RBUSLOG_INFO("%s async method in progress [%s]", __FUNCTION__, methodName);
            }
            else
            {
                free(asyncHandle);
            }

            if (result != RBUS_ERROR_SUCCESS)
            {
                rbusObject_Release(outParams);
            }
        }
        else
        {
            RBUSLOG_INFO("%s methodHandler not registered method [%s]", __FUNCTION__, methodName);
            result = RBUS_ERROR_ACCESS_NOT_ALLOWED;
        }
    }
    else
    {
        RBUSLOG_WARN("%s no element found method [%s]", __FUNCTION__, methodName);
        result = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    rbusObject_Release(inParams);

    if(result == RBUS_ERROR_ASYNC_RESPONSE)
    {
        return RTMESSAGE_BUS_SUCCESS_ASYNC;
    }
    else
    {
        rbusMessage_Init(response);
        rbusMessage_SetInt32(*response, result);
        if (result == RBUS_ERROR_SUCCESS)
        {
            rbusObject_appendToMessage(outParams, *response);
            rbusObject_Release(outParams);
        }
        return RTMESSAGE_BUS_SUCCESS; 
    }
}

static int _callback_handler(char const* destination, char const* method, rbusMessage request, void* userData, rbusMessage* response, const rtMessageHeader* hdr)
{
    rbusHandle_t handle = (rbusHandle_t)userData;

    RBUSLOG_DEBUG("Received callback for [%s]", destination);

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
    else if(!strcmp(method, METHOD_RPC))
    {
        return _method_callback_handler (handle, request, response, hdr);
    }
    else
    {
        RBUSLOG_WARN("unhandled callback for [%s] method!", method);
    }

    return 0;
}

//******************************* Bus Initialization *****************************//
rbusError_t rbus_open(rbusHandle_t* handle, char const* componentName)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    int foundIndex = -1;
    int  i = 0;
    rbusHandle_t tmpHandle;

    if((handle == NULL) || (componentName == NULL))
    {
        return RBUS_ERROR_INVALID_INPUT;
    }

    *handle = NULL;

    rbusConfig_CreateOnce();

    /*
        Per spec: If a component calls this API more than once, any previous busHandle 
        and all previous data element registrations will be canceled.
    */
    for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
    {
        if(handle_array[i].inUse && strcmp(componentName, handle_array[i].componentName) == 0)
        {
            rbus_close(&handle_array[i]);
        }
    }

    /*  Find open item in array: 
        TODO why can't this just be a rtVector we push/remove from? */
    for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
    {
        if(!handle_array[i].inUse)
        {
            foundIndex = i;
            break;
        }
    }

    if(foundIndex == -1)
    {
        RBUSLOG_ERROR("<%s>: Exceeded the allowed number of components per process!", __FUNCTION__);
        return RBUS_ERROR_OUT_OF_RESOURCES;
    }

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
        RBUSLOG_ERROR("<%s>: rbus_openBrokerConnection() failed with %d", __FUNCTION__, err);
        return RBUS_ERROR_BUS_ERROR;
    }

    /*call after opening broker connection -- currently safe to call for multiple rbus_open calls*/
    err = rbus_registerClientDisconnectHandler(_client_disconnect_callback_handler);
    RBUSLOG_DEBUG("registering client disconnect handler %s", err == RTMESSAGE_BUS_SUCCESS ? "succeeded" : "failed");

    tmpHandle = &handle_array[foundIndex];

    RBUSLOG_INFO("Bus registration successfull!");
    RBUSLOG_DEBUG("<%s>: Try rbus_registerObj() for component base object [%s]!", __FUNCTION__, componentName);

    if((err = rbus_registerObj(componentName, _callback_handler, tmpHandle)) != RTMESSAGE_BUS_SUCCESS)
    {
        /*Note that this will fail if a previous rbus_open was made with the same componentName
          because rbus_registerObj doesn't allow the same name to be registered twice.  This would
          also fail if ccsp using rbus-core has registered the same object name */
        RBUSLOG_ERROR("<%s>: rbus_registerObj() failed with %d", __FUNCTION__, err);
        return RBUS_ERROR_BUS_ERROR;
    }

    RBUSLOG_DEBUG("<%s>: rbus_registerObj() Success!", __FUNCTION__);

    if((err = rbus_registerSubscribeHandler(componentName, _event_subscribe_callback_handler, tmpHandle)) != RTMESSAGE_BUS_SUCCESS)
    {
        RBUSLOG_ERROR("<%s>: rbus_registerSubscribeHandler() failed with %d", __FUNCTION__, err);
        return RBUS_ERROR_BUS_ERROR;
    }

    RBUSLOG_DEBUG("<%s>: rbus_registerSubscribeHandler() Success!", __FUNCTION__);

    handle_array[foundIndex].inUse = 1;
    handle_array[foundIndex].componentName = strdup(componentName);
    *handle = tmpHandle;
    rtVector_Create(&handle_array[foundIndex].eventSubs);
    rtVector_Create(&handle_array[foundIndex].messageCallbacks);
    handle_array[foundIndex].connection = rbus_getConnection();

    return errorcode;
}

rbusError_t rbus_close(rbusHandle_t handle)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;

    if(handle == NULL)
    {
        return RBUS_ERROR_INVALID_INPUT;
    }

    if(handleInfo->eventSubs)
    {
        while(rtVector_Size(handleInfo->eventSubs))
        {
            rbusEventSubscription_t* sub = (rbusEventSubscription_t*)rtVector_At(handleInfo->eventSubs, 0);
            if(sub)
            {
                if(sub->filter)
                    rbusEvent_UnsubscribeEx(handle, sub, 1);
                else
                    rbusEvent_Unsubscribe(handle, sub->eventName);
            }
        }
        rtVector_Destroy(handleInfo->eventSubs, NULL);
        handleInfo->eventSubs = NULL;
    }

    if (handleInfo->messageCallbacks)
    {
        rbusMessage_RemoveAllListeners(handle);
        rtVector_Destroy(handleInfo->messageCallbacks, NULL);
        handleInfo->messageCallbacks = NULL;
    }

    if(handleInfo->subscriptions != NULL)
    {
        rbusSubscriptions_destroy(handleInfo->subscriptions);
        handleInfo->subscriptions = NULL;
    }

    rbusValueChange_CloseHandle(handle);//called before freeElementNode below

    rbusAsyncSubscribe_CloseHandle(handle);

    if(handleInfo->elementRoot)
    {
        freeElementNode(handleInfo->elementRoot);
        handleInfo->elementRoot = NULL;
    }

    if((err = rbus_unregisterObj(handleInfo->componentName)) != RTMESSAGE_BUS_SUCCESS) //FIXME: shouldn't rbus_closeBrokerConnection be called even if this fails ?
    {
        RBUSLOG_WARN("<%s>: rbus_unregisterObj() for [%s] fails with %d", __FUNCTION__, handleInfo->componentName, err);
        errorcode = RBUS_ERROR_INVALID_HANDLE;
    }
    else
    {
        int canClose = 1;
        int i;

        RBUSLOG_DEBUG("<%s>: rbus_unregisterObj() for [%s] Success!!", __FUNCTION__, handleInfo->componentName);
        free(handleInfo->componentName);
        handleInfo->componentName = NULL;
        handleInfo->inUse = 0;

        for(i = 0; i < MAX_COMPS_PER_PROCESS; i++)
        {
            if(handle_array[i].inUse)
            {
                canClose = 0;
                break;
            }
        }

        if(canClose)
        {
            //calling before closing connection
            rbus_unregisterClientDisconnectHandler();

            if((err = rbus_closeBrokerConnection()) != RTMESSAGE_BUS_SUCCESS)
            {
                RBUSLOG_WARN("<%s>: rbus_closeBrokerConnection() fails with %d", __FUNCTION__, err);
                errorcode = RBUS_ERROR_BUS_ERROR;
            }
            else
            {
                RBUSLOG_INFO("Bus unregistration Successfull!");
            }

            rbusConfig_Destroy();
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
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    for(i=0; i<numDataElements; ++i)
    {
        char* name = elements[i].name;
        rbus_error_t err = RTMESSAGE_BUS_SUCCESS;

        if((!name) || (0 == strlen(name))) {
            rc = RBUS_ERROR_INVALID_INPUT;
            break ;
        }

        RBUSLOG_DEBUG("rbus_getDataElements: %s", name);

        if(handleInfo->elementRoot == NULL)
        {
            RBUSLOG_DEBUG("First Time, create the root node for [%s]!", handleInfo->componentName);
            handleInfo->elementRoot = getEmptyElementNode();
            handleInfo->elementRoot->name = strdup(handleInfo->componentName);
            RBUSLOG_DEBUG("Root node created for [%s]", handleInfo->elementRoot->name);
        }

        if(handleInfo->subscriptions == NULL)
        {
            rbusSubscriptions_create(&handleInfo->subscriptions, handle, handleInfo->componentName, handleInfo->elementRoot, rbusConfig_Get()->tmpDir);
        }

        elementNode* node;
        if((node = insertElement(handleInfo->elementRoot, &elements[i])) == NULL)
        {
            RBUSLOG_ERROR("<%s>: failed to insert element [%s]!!", __FUNCTION__, name);
            rc = RBUS_ERROR_OUT_OF_RESOURCES;
            break;
        }
        else
        {
            if((err = rbus_addElement(handleInfo->componentName, name)) != RTMESSAGE_BUS_SUCCESS)
            {
                RBUSLOG_ERROR("<%s>: failed to add element with core [%s] err=%d!!", __FUNCTION__, name, err);
                removeElement(node);
                rc = RBUS_ERROR_ELEMENT_NAME_DUPLICATE;
                break;
            }
            else
            {
                rbusSubscriptions_resubscribeCache(handle, handleInfo->subscriptions, name, node);
                RBUSLOG_INFO("%s inserted successfully!", name);
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
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    int i;
    for(i=0; i<numDataElements; ++i)
    {
        char const* name = elements[i].name;
/*
        if(rbus_unregisterEvent(handleInfo->componentName, name) != RTMESSAGE_BUS_SUCCESS)
            RBUSLOG_INFO("<%s>: failed to remove event [%s]!!", __FUNCTION__, name);
*/
        if(rbus_removeElement(handleInfo->componentName, name) != RTMESSAGE_BUS_SUCCESS)
            RBUSLOG_WARN("<%s>: failed to remove element from core [%s]!!", __FUNCTION__, name);

/*      TODO: we need to remove all instance elements that this registration element instantiated
        rbusValueChange_RemoveParameter(handle, NULL, name);
        removeElement(&(handleInfo->elementRoot), name);
*/
    }
    return RBUS_ERROR_SUCCESS;
}

//************************* Discovery related Operations *******************//
rbusError_t rbus_discoverComponentName (rbusHandle_t handle,
                            int numElements, char const** elementNames,
                            int *numComponents, char ***componentName)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    if(handle == NULL)
    {
        return RBUS_ERROR_INVALID_INPUT;
    }
    else if ((numElements < 1) || (NULL == elementNames[0]))
    {
        RBUSLOG_WARN("Invalid input passed to rbus_discoverElementsObjects");
        return RBUS_ERROR_INVALID_INPUT;
    }
    *numComponents = 0;
    *componentName = 0;
    char **output = NULL;
    int out_count = 0;
    if(RTMESSAGE_BUS_SUCCESS == rbus_discoverElementsObjects(numElements, elementNames, &out_count, &output))
    {
        *componentName = output;
        *numComponents = out_count;
    }
    else
    {
         RBUSLOG_WARN("return from rbus_discoverElementsObjects is not success");
    }
  
    return errorcode;
}

rbusError_t rbus_discoverComponentDataElements (rbusHandle_t handle,
                            char const* name, bool nextLevel,
                            int *numElements, char*** elementNames)
{
    rbus_error_t ret;

    *numElements = 0;
    *elementNames = 0;
    UNUSED1(nextLevel);
    if((handle == NULL) || (name == NULL))
    {
        return RBUS_ERROR_INVALID_INPUT;
    }

    ret = rbus_discoverObjectElements(name, numElements, elementNames);

    /* mrollins FIXME what was this doing before and do i need it after I changed the rbuscore discover api ?
        if(*numElements > 0)
            *numElements = *numElements-1;  //Fix this. We need a better way to ignore the component name as element name.
    */

    return ret == RTMESSAGE_BUS_SUCCESS ? RBUS_ERROR_SUCCESS : RBUS_ERROR_BUS_ERROR;
}

//************************* Parameters related Operations *******************//
rbusError_t rbus_get(rbusHandle_t handle, char const* name, rbusValue_t* value)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rbusMessage request, response;
    int ret = -1;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*) handle;

    if (_is_wildcard_query(name))
    {
        RBUSLOG_WARN("%s This method does not support wildcard query", __FUNCTION__);
        return RBUS_ERROR_ACCESS_NOT_ALLOWED;
    }

    /* Is it a valid Query */
    if (!_is_valid_get_query(name))
    {
        RBUSLOG_WARN("%s This method is only to get Parameters", __FUNCTION__);
        return RBUS_ERROR_INVALID_INPUT;
    }

    rbusMessage_Init(&request);
    /* Set the Component name that invokes the set */
    rbusMessage_SetString(request, handleInfo->componentName);
    /* Param Size */
    rbusMessage_SetInt32(request, (int32_t)1);
    rbusMessage_SetString(request, name);

    RBUSLOG_DEBUG("Calling rbus_invokeRemoteMethod for [%s]", name);

    if((err = rbus_invokeRemoteMethod(name, METHOD_GETPARAMETERVALUES, request, 6000, &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        RBUSLOG_ERROR("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        errorcode = rbuscoreError_to_rbusError(err);
    }
    else
    {
        int valSize;
        rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;

        RBUSLOG_DEBUG("Received response for remote method invocation!");

        rbusMessage_GetInt32(response, &ret);

        RBUSLOG_DEBUG("Response from the remote method is [%d]!",ret);
        errorcode = (rbusError_t) ret;
        legacyRetCode = (rbusLegacyReturn_t) ret;

        if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
        {
            errorcode = RBUS_ERROR_SUCCESS;
            RBUSLOG_DEBUG("Received valid response!");
            rbusMessage_GetInt32(response, &valSize);
            if(1/*valSize*/)
            {
                char const *buff = NULL;

                //Param Name
                rbusMessage_GetString(response, &buff);
                if(buff && (strcmp(name, buff) == 0))
                {
                    rbusValue_initFromMessage(value, response);
                }
                else
                {
                    RBUSLOG_WARN("Param mismatch!");
                    RBUSLOG_WARN("Requested param: [%s], Received Param: [%s]", name, buff);
                }
            }
        }
        else
        {
            RBUSLOG_ERROR("Response from remote method indicates the call failed!!");
            errorcode = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
            if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
            {
                errorcode = CCSPError_to_rbusError(legacyRetCode);
            }
        }

        rbusMessage_Release(response);
    }
    return errorcode;
}

rbusError_t _getExt_response_parser(rbusMessage response, int *numValues, rbusProperty_t* retProperties)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
    int numOfVals = 0;
    int ret = -1;
    int i = 0;
    RBUSLOG_DEBUG("Received response for remote method invocation!");

    rbusMessage_GetInt32(response, &ret);
    RBUSLOG_DEBUG("Response from the remote method is [%d]!",ret);

    errorcode = (rbusError_t) ret;
    legacyRetCode = (rbusLegacyReturn_t) ret;

    *numValues = 0;
    if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
    {
        errorcode = RBUS_ERROR_SUCCESS;
        RBUSLOG_DEBUG("Received valid response!");
        rbusMessage_GetInt32(response, &numOfVals);
        *numValues = numOfVals;
        RBUSLOG_DEBUG("Number of return params = %d", numOfVals);

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
        RBUSLOG_ERROR("Response from remote method indicates the call failed!!");
        errorcode = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
        {
            errorcode = CCSPError_to_rbusError(legacyRetCode);
        }
    }
    rbusMessage_Release(response);

    return errorcode;
}

rbusError_t rbus_getExt(rbusHandle_t handle, int paramCount, char const** pParamNames, int *numValues, rbusProperty_t* retProperties)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    int i;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*) handle;

    if ((1 == paramCount) && (_is_wildcard_query(pParamNames[0])))
    {
        int numDestinations = 0;
        char** destinations;
        //int length = strlen(pParamNames[0]);

        err = rbus_discoverWildcardDestinations(pParamNames[0], &numDestinations, &destinations);
        if (RTMESSAGE_BUS_SUCCESS == err)
        {
            RBUSLOG_DEBUG("Query for expression %s was successful. See result below:", pParamNames[0]);
            rbusProperty_t last = NULL;
            *numValues = 0;
            if (0 == numDestinations)
            {
                RBUSLOG_DEBUG("It is possibly a table entry from single component.");
            }
            else
            {
                for(i = 0; i < numDestinations; i++)
                {
                    int tmpNumOfValues = 0;
                    rbusMessage request, response;
                    RBUSLOG_DEBUG("Destination %d is %s", i, destinations[i]);

                    /* Get the query sent to each component identified */
                    rbusMessage_Init(&request);
                    /* Set the Component name that invokes the set */
                    rbusMessage_SetString(request, handleInfo->componentName);
                    rbusMessage_SetInt32(request, 1);
                    rbusMessage_SetString(request, pParamNames[0]);
                    /* Invoke the method */
                    if((err = rbus_invokeRemoteMethod(destinations[i], METHOD_GETPARAMETERVALUES, request, 60000, &response)) != RTMESSAGE_BUS_SUCCESS)
                    {
                        RBUSLOG_ERROR("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
                        errorcode = rbuscoreError_to_rbusError(err);
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
                        RBUSLOG_WARN("Failed to get the data from %s Component", destinations[i]);
                        break;
                    }
                    else
                    {
                        *numValues += tmpNumOfValues;
                    }
                }

                for(i = 0; i < numDestinations; i++)
                    free(destinations[i]);
                free(destinations);

                return errorcode;
            }
        }
        else
        {
            RBUSLOG_DEBUG("Query for expression %s was not successful.", pParamNames[0]);
            return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
    }

    {
        rbusMessage request, response;
        int numComponents;
        char** componentNames = NULL;

        /*discover which components have some ownership of the params in the list*/
        errorcode = rbus_discoverComponentName(handle, paramCount, pParamNames, &numComponents, &componentNames);
        if(errorcode == RBUS_ERROR_SUCCESS && paramCount == numComponents)
        {
#if 0
            RBUSLOG_DEBUG("rbus_discoverComponentName return %d component for %d params", numComponents, paramCount);
            for(i = 0; i < numComponents; ++i)
            {
                RBUSLOG_DEBUG("%d: %s %s",i, pParamNames[i], componentNames[i]);
            }
#endif
            for(i = 0; i < paramCount; ++i)
            {
                if(!componentNames[i] || !componentNames[i][0])
                {
                    RBUSLOG_ERROR("Cannot find component for %s", pParamNames[i]);
                    errorcode = RBUS_ERROR_INVALID_INPUT;
                }
            }

            if(errorcode == RBUS_ERROR_INVALID_INPUT)
            {
                free(componentNames);
                return RBUS_ERROR_INVALID_INPUT;
            }

            *retProperties = NULL;/*NULL to mark first batch*/
            *numValues = 0;

            /*batch by component*/
            for(;;)
            {
                char* componentName = NULL;
                char const* firstParamName = NULL;
                int batchCount = 0;

                for(i = 0; i < paramCount; ++i)
                {
                    if(componentNames[i])
                    {
                        if(!componentName)
                        {
                            RBUSLOG_DEBUG("%s starting batch for component %s", __FUNCTION__, componentNames[i]);
                            componentName = strdup(componentNames[i]);
                            firstParamName = pParamNames[i];
                            batchCount = 1;
                        }
                        else if(strcmp(componentName, componentNames[i]) == 0)
                        {
                            batchCount++;
                        }
                    }
                }

                if(componentName)
                {
                    rbusMessage_Init(&request);
                    rbusMessage_SetString(request, componentName);
                    rbusMessage_SetInt32(request, batchCount);

                    RBUSLOG_DEBUG("batchCount %d", batchCount);

                    for(i = 0; i < paramCount; ++i)
                    {
                        if(componentNames[i] && strcmp(componentName, componentNames[i]) == 0)
                        {
                            RBUSLOG_DEBUG("%s adding %s to batch", __FUNCTION__, pParamNames[i]);                            
                            rbusMessage_SetString(request, pParamNames[i]);

                            /*free here so its removed from batch scan*/
                            free(componentNames[i]);
                            componentNames[i] = NULL;
                        }
                    }                  

                    RBUSLOG_DEBUG("%s sending batch request with %d params to component %s", __FUNCTION__, batchCount, componentName);
                    free(componentName);

                    if((err = rbus_invokeRemoteMethod(firstParamName, METHOD_GETPARAMETERVALUES, request, 6000, &response)) != RTMESSAGE_BUS_SUCCESS)
                    {
                        RBUSLOG_ERROR("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
                        errorcode = rbuscoreError_to_rbusError(err);
                        break;
                    }
                    else
                    {
                        rbusProperty_t batchResult;
                        int batchNumVals;
                        if((errorcode = _getExt_response_parser(response, &batchNumVals, &batchResult)) != RBUS_ERROR_SUCCESS)
                        {
                            RBUSLOG_ERROR("%s error parsing response %d", __FUNCTION__, errorcode);
                        }
                        else
                        {
                            RBUSLOG_DEBUG("%s got valid response", __FUNCTION__);
                            if(*retProperties == NULL) /*first batch*/
                            {
                                *retProperties = batchResult;
                            }
                            else /*append subsequent batches*/
                            {
                                rbusProperty_PushBack(*retProperties, batchResult);
                                rbusProperty_Release(batchResult);
                            }
                            *numValues += batchNumVals;
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            errorcode = RBUS_ERROR_DESTINATION_NOT_REACHABLE;
            RBUSLOG_ERROR("Discover component names failed with error %d and counts %d/%d", errorcode, paramCount, numComponents);
        }
        if(componentNames)
            free(componentNames);
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
                        RBUSLOG_WARN("%s unexpected type param %d", __FUNCTION__, type);
                        break;
                }

                rbusValue_Release(value);
            }
            else
            {
                RBUSLOG_ERROR("%s rbus_get type missmatch. expected %d. got %d", __FUNCTION__, type, rbusValue_GetType(value));
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
    rbusMessage setRequest, setResponse;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*) handle;
    rbusValueType_t type = rbusValue_GetType(value);

    if ((NULL == value) || (RBUS_NONE == type))
    {
        return errorcode;
    }
    rbusMessage_Init(&setRequest);
    /* Set the Session ID first */
    if ((opts) && (opts->sessionId != 0))
        rbusMessage_SetInt32(setRequest, opts->sessionId);
    else
        rbusMessage_SetInt32(setRequest, 0);

    /* Set the Component name that invokes the set */
    rbusMessage_SetString(setRequest, handleInfo->componentName);
    /* Set the Size of params */
    rbusMessage_SetInt32(setRequest, 1);

    /* Set the params in details */
    rbusValue_appendToMessage(name, value, setRequest);

    /* Set the Commit value; FIXME: Should we use string? */
    rbusMessage_SetString(setRequest, (!opts || opts->commit) ? "TRUE" : "FALSE");

    if((err = rbus_invokeRemoteMethod(name, METHOD_SETPARAMETERVALUES, setRequest, 6000, &setResponse)) != RTMESSAGE_BUS_SUCCESS)
    {
        RBUSLOG_ERROR("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        errorcode = rbuscoreError_to_rbusError(err);
    }
    else
    {
        rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
        int ret = -1;
        char const* pErrorReason = NULL;
        rbusMessage_GetInt32(setResponse, &ret);

        RBUSLOG_DEBUG("Response from the remote method is [%d]!", ret);
        errorcode = (rbusError_t) ret;
        legacyRetCode = (rbusLegacyReturn_t) ret;

        if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
        {
            errorcode = RBUS_ERROR_SUCCESS;
            RBUSLOG_DEBUG("Successfully Set the Value");
        }
        else
        {
            rbusMessage_GetString(setResponse, &pErrorReason);
            RBUSLOG_WARN("Failed to Set the Value for %s", pErrorReason);
            if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
            {
                errorcode = CCSPError_to_rbusError(legacyRetCode);
            }
        }

        /* Release the reponse message */
        rbusMessage_Release(setResponse);
    }
    return errorcode;
}

rbusError_t rbus_setMulti(rbusHandle_t handle, int numProps, rbusProperty_t properties, rbusSetOptions_t* opts)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rbusMessage setRequest, setResponse;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*) handle;
    rbusValueType_t type = RBUS_NONE;
    rbusProperty_t current;

    if (numProps > 0 && properties != NULL)
    {
        char const** pParamNames;
        int numComponents;
        char** componentNames = NULL;
        int i;

        /*create list of paramNames to pass to rbus_discoverComponentName*/
        pParamNames = malloc(sizeof(char*) * numProps);
        current = properties;
        i = 0;
        while(current && i < numProps)
        {
            pParamNames[i++] = rbusProperty_GetName(current);
            type = rbusValue_GetType(rbusProperty_GetValue(current));
            if (RBUS_NONE == type)
            {
                printf("Invalid data type passed in one of the data type\n");
                free(pParamNames);
                return errorcode;
            }
            current = rbusProperty_GetNext(current);
        }
        if(i != numProps)
        {
            RBUSLOG_WARN ("Invalid input: numProps more then actual number of properties.");
            free(pParamNames);
            return RBUS_ERROR_INVALID_INPUT;
        }

        /*discover which components have some ownership of the params*/
        errorcode = rbus_discoverComponentName(handle, numProps, pParamNames, &numComponents, &componentNames);
        if(errorcode == RBUS_ERROR_SUCCESS && numProps == numComponents)
        {
#if 0
            RBUSLOG_DEBUG("rbus_discoverComponentName return %d component for %d params", numComponents, numProps);
            for(i = 0; i < numComponents; ++i)
            {
                RBUSLOG_DEBUG("%d: %s %s",i, pParamNames[i], componentNames[i]);
            }
#endif
            current = properties;
            for(i = 0; i < numProps; ++i, current = rbusProperty_GetNext(current))
            {
                if(!componentNames[i] || !componentNames[i][0])
                {
                    RBUSLOG_ERROR("Cannot find component for %s", rbusProperty_GetName(current));
                    errorcode = RBUS_ERROR_INVALID_INPUT;
                }
            }

            if(errorcode == RBUS_ERROR_INVALID_INPUT)
            {
                free(componentNames);
                return RBUS_ERROR_INVALID_INPUT;
            }

            for(;;)
            {
                char* componentName = NULL;
                char const* firstParamName = NULL;
                int batchCount = 0;

                for(i = 0; i < numProps; ++i)
                {
                    if(componentNames[i])
                    {
                        if(!componentName)
                        {
                            RBUSLOG_DEBUG("%s starting batch for component %s", __FUNCTION__, componentNames[i]);
                            componentName = strdup(componentNames[i]);
                            firstParamName = pParamNames[i];
                            batchCount = 1;
                        }
                        else if(strcmp(componentName, componentNames[i]) == 0)
                        {
                            batchCount++;
                        }
                    }
                }

                if(componentName)
                {

                    rbusMessage_Init(&setRequest);

                    /* Set the Session ID first */
                    if ((opts) && (opts->sessionId != 0))
                        rbusMessage_SetInt32(setRequest, opts->sessionId);
                    else
                        rbusMessage_SetInt32(setRequest, 0);

                    /* Set the Component name that invokes the set */
                    rbusMessage_SetString(setRequest, handleInfo->componentName);
                    /* Set the Size of params */
                    rbusMessage_SetInt32(setRequest, batchCount);

                    current = properties;
                    for(i = 0; i < numProps; ++i, current = rbusProperty_GetNext(current))
                    {
                        if(componentNames[i] && strcmp(componentName, componentNames[i]) == 0)
                        {
                            if(strcmp(pParamNames[i], rbusProperty_GetName(current)))
                                RBUSLOG_ERROR("paramName doesn't match current property");

                            RBUSLOG_DEBUG("%s adding %s to batch", __FUNCTION__, rbusProperty_GetName(current));                            
                            rbusValue_appendToMessage(rbusProperty_GetName(current), rbusProperty_GetValue(current), setRequest);

                            /*free here so its removed from batch scan*/
                            free(componentNames[i]);
                            componentNames[i] = NULL;
                        }
                    }  

                    /* Set the Commit value; FIXME: Should we use string? */
                    rbusMessage_SetString(setRequest, (!opts || opts->commit) ? "TRUE" : "FALSE");

                    if((err = rbus_invokeRemoteMethod(firstParamName, METHOD_SETPARAMETERVALUES, setRequest, 6000, &setResponse)) != RTMESSAGE_BUS_SUCCESS)
                    {
                        RBUSLOG_ERROR("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
                        errorcode = rbuscoreError_to_rbusError(err);
                    }
                    else
                    {
                        char const* pErrorReason = NULL;
                        rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
                        int ret = -1;
                        rbusMessage_GetInt32(setResponse, &ret);

                        RBUSLOG_DEBUG("Response from the remote method is [%d]!", ret);
                        errorcode = (rbusError_t) ret;
                        legacyRetCode = (rbusLegacyReturn_t) ret;

                        if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
                        {
                            errorcode = RBUS_ERROR_SUCCESS;
                            RBUSLOG_DEBUG("Successfully Set the Value");
                        }
                        else
                        {
                            rbusMessage_GetString(setResponse, &pErrorReason);
                            RBUSLOG_WARN("Failed to Set the Value for %s", pErrorReason);
                            if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
                            {
                                errorcode = CCSPError_to_rbusError(legacyRetCode);
                            }
                        }

                        /* Release the reponse message */
                        rbusMessage_Release(setResponse);
                    }
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            errorcode = RBUS_ERROR_DESTINATION_NOT_REACHABLE;
            RBUSLOG_ERROR("Discover component names failed with error %d and counts %d/%d", errorcode, numProps, numComponents);
        }
        if(componentNames)
            free(componentNames);
    }
    return errorcode;
}

#if 0
rbusError_t rbus_setMulti(rbusHandle_t handle, int numValues,
        char const** valueNames, rbusValue_t* values, rbusSetOptions_t* opts)
{
    rbusError_t errorcode = RBUS_ERROR_INVALID_INPUT;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rbusMessage setRequest, setResponse;
    int loopCnt = 0;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*) handle;

    if (values != NULL)
    {
        rbusMessage_Init(&setRequest);
        /* Set the Session ID first */
        rbusMessage_SetInt32(setRequest, 0);
        /* Set the Component name that invokes the set */
        rbusMessage_SetString(setRequest, handleInfo->componentName);
        /* Set the Size of params */
        rbusMessage_SetInt32(setRequest, numValues);

        /* Set the params in details */
        for (loopCnt = 0; loopCnt < numValues; loopCnt++)
        {
            rbusValue_appendToMessage(valueNames[loopCnt], values[loopCnt], setRequest);
        }

        /* Set the Commit value; FIXME: Should we use string? */
        rbusMessage_SetString(setRequest, (!opts || opts->commit) ? "TRUE" : "FALSE");

        /* TODO: At this point in time, only given Table/Component can be updated with SET/GET..
         * So, passing the elementname as first arg is not a issue for now..
         * We must enhance the rbus in such a way that we shd be able to set across components. Lets revist this area at that time.
         */
#if 0
        /* TODO: First step towards the above comment. When we enhace to support acorss components, this following has to be looped or appropriate count will be passed */
        char const* pElementNames[] = {values[0].name, NULL};
        char** pComponentName = NULL;
        err = rbus_discoverElementObjects(pElementNames, 1, &pComponentName);
        if (err != RTMESSAGE_BUS_SUCCESS)
        {
            RBUSLOG_INFO ("Element not found");
            errorcode = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
        }
        else
        {
            RBUSLOG_INFO ("Component name is, %s", pComponentName[0]);
            free (pComponentName[0]);
        }
#endif
        if((err = rbus_invokeRemoteMethod(valueNames[0], METHOD_SETPARAMETERVALUES, setRequest, 6000, &setResponse)) != RTMESSAGE_BUS_SUCCESS)
        {
            RBUSLOG_INFO("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
            errorcode = RBUS_ERROR_BUS_ERROR;
        }
        else
        {
            char const* pErrorReason = NULL;
            rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;
            int ret = -1;
            rbusMessage_GetInt32(setResponse, &ret);

            RBUSLOG_INFO("Response from the remote method is [%d]!", ret);
            errorcode = (rbusError_t) ret;
            legacyRetCode = (rbusLegacyReturn_t) ret;

            if((errorcode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
            {
                errorcode = RBUS_ERROR_SUCCESS;
                RBUSLOG_INFO("Successfully Set the Value");
            }
            else
            {
                rbusMessage_GetString(setResponse, &pErrorReason);
                RBUSLOG_INFO("Failed to Set the Value for %s", pErrorReason);
            }

            /* Release the reponse message */
            rbusMessage_Release(setResponse);
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
                RBUSLOG_WARN("%s unexpected type param %d", __FUNCTION__, type);
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

rbusError_t rbus_setUInt(rbusHandle_t handle, char const* paramName, unsigned int paramVal)
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
    rbus_error_t err;
    int returnCode = 0;
    int32_t instanceId = 0;
    const char dot = '.';
    rbusMessage request, response;
    (void)handle;
    rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;

    RBUSLOG_INFO("%s: %s %s", __FUNCTION__, tableName, aliasName);

    if(tableName == NULL || tableName[strlen(tableName)-1] != dot)
    {
        RBUSLOG_WARN("%s invalid table name %s", __FUNCTION__, tableName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    rbusMessage_Init(&request);
    rbusMessage_SetInt32(request, 0);/*TODO: this should be the session ID*/
    rbusMessage_SetString(request, tableName);/*TODO: do we need to append the name as well as pass the name as the 1st arg to rbus_invokeRemoteMethod ?*/
    if(aliasName)
        rbusMessage_SetString(request, aliasName);
    else
        rbusMessage_SetString(request, "");

    if((err = rbus_invokeRemoteMethod(
        tableName, /*as taken from ccsp_base_api.c, this was the destination component ID, but to locate the route, the table name can be used
                     because the broker simlpy looks at the top level nodes that are owned by a component route.  maybe this breaks if the broker changes*/
        METHOD_ADDTBLROW, 
        request, 
        6000, 
        &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        RBUSLOG_INFO("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        return rbuscoreError_to_rbusError(err);
    }
    else
    {
        rbusMessage_GetInt32(response, &returnCode);
        rbusMessage_GetInt32(response, &instanceId);
        legacyRetCode = (rbusLegacyReturn_t)returnCode;

        if(instNum)
            *instNum = (uint32_t)instanceId;/*FIXME we need an rbus_PopUInt32 to avoid loosing a bit */

        RBUSLOG_INFO("%s rbus_invokeRemoteMethod success response returnCode:%d instanceId:%d", __FUNCTION__, returnCode, instanceId);
        if((returnCode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
        {
            returnCode = RBUS_ERROR_SUCCESS;
            RBUSLOG_DEBUG("Successfully Set the Value");
        }
        else
        {
            RBUSLOG_WARN("Response from remote method indicates the call failed!!");
            if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
            {
                returnCode = CCSPError_to_rbusError(legacyRetCode);
            }
        }
        rbusMessage_Release(response);
    }

    return returnCode;
}

rbusError_t rbusTable_removeRow(
    rbusHandle_t handle,
    char const* rowName)
{
    rbus_error_t err;
    int returnCode = 0;
    rbusMessage request, response;
    (void)handle;
    rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;

    RBUSLOG_INFO("%s: %s", __FUNCTION__, rowName);

    rbusMessage_Init(&request);
    rbusMessage_SetInt32(request, 0);/*TODO: this should be the session ID*/
    rbusMessage_SetString(request, rowName);/*TODO: do we need to append the name as well as pass the name as the 1st arg to rbus_invokeRemoteMethod ?*/

    if((err = rbus_invokeRemoteMethod(
        rowName,
        METHOD_DELETETBLROW, 
        request, 
        6000, 
        &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        RBUSLOG_INFO("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        return rbuscoreError_to_rbusError(err);
    }
    else
    {
        rbusMessage_GetInt32(response, &returnCode);
        legacyRetCode = (rbusLegacyReturn_t)returnCode;

        RBUSLOG_INFO("%s rbus_invokeRemoteMethod success response returnCode:%d", __FUNCTION__, returnCode);
        if((returnCode == RBUS_ERROR_SUCCESS) || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
        {
            returnCode = RBUS_ERROR_SUCCESS;
            RBUSLOG_DEBUG("Successfully Set the Value");
        }
        else
        {
            RBUSLOG_WARN("Response from remote method indicates the call failed!!");
            if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
            {
                returnCode = CCSPError_to_rbusError(legacyRetCode);
            }
        }
        rbusMessage_Release(response);
    }

    return returnCode;
}

rbusError_t rbusTable_registerRow(
    rbusHandle_t handle,
    char const* tableName,
    char const* aliasName,
    uint32_t instNum)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    char rowName[RBUS_MAX_NAME_LENGTH] = {0};
    int rc;

    rc = snprintf(rowName, RBUS_MAX_NAME_LENGTH, "%s%d", tableName, instNum);
    if(rc < 0 || rc >= RBUS_MAX_NAME_LENGTH)
    {
        RBUSLOG_WARN("%s: invalid table name %s", __FUNCTION__, tableName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    elementNode* rowInstElem = retrieveInstanceElement(handleInfo->elementRoot, rowName);
    elementNode* tableInstElem = retrieveInstanceElement(handleInfo->elementRoot, tableName);

    if(rowInstElem)
    {
        RBUSLOG_WARN("%s: row already exists %s", __FUNCTION__, rowName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    if(!tableInstElem)
    {
        RBUSLOG_WARN("%s: table does not exist %s", __FUNCTION__, tableName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    RBUSLOG_DEBUG("%s: register table row %s", __FUNCTION__, rowName);
    registerTableRow(handle, tableInstElem, tableName, aliasName, instNum);
    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbusTable_unregisterRow(
    rbusHandle_t handle,
    char const* rowName)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;

    elementNode* rowInstElem = retrieveInstanceElement(handleInfo->elementRoot, rowName);

    if(!rowInstElem)
    {
        RBUSLOG_DEBUG("%s: row does not exists %s", __FUNCTION__, rowName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    unregisterTableRow(handle, rowInstElem);
    return RBUS_ERROR_SUCCESS;
}

//************************** Events ****************************//

static rbusMessage rbusEvent_CreatePayloadEx(rbusEventSubscription_t* sub)
{
    rbusMessage payload = NULL;

    if(sub->filter || sub->interval || sub->duration)
    {
        rbusMessage_Init(&payload);

        rbusMessage_SetInt32(payload, sub->interval);
        rbusMessage_SetInt32(payload, sub->duration);

        if(sub->filter)
        {
            rbusMessage_SetInt32(payload, 1);
            rbusFilter_AppendToMessage(sub->filter, payload);
        }
        else
        {
            rbusMessage_SetInt32(payload, 0);
        }
    }

    return payload;
}

static rbusError_t rbusEvent_SubscribeWithRetries(
    rbusHandle_t                    handle,
    char const*                     eventName,
    rbusEventHandler_t              handler,
    void*                           userData,
    rbusFilter_t                    filter,
    int32_t                         interval,
    uint32_t                        duration,    
    int                             timeout,
    rbusSubscribeAsyncRespHandler_t async)
{
    rbus_error_t coreerr;
    int providerError = RBUS_ERROR_SUCCESS;
    rbusEventSubscription_t* sub;
    rbusMessage payload = NULL;
    int destNotFoundSleep = 1000; /*miliseconds*/
    int destNotFoundTimeout;
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;

    if(timeout == -1)
    {
        destNotFoundTimeout = rbusConfig_Get()->subscribeTimeout;
    }
    else
    {
        destNotFoundTimeout = timeout * 1000; /*concert seconds to miliseconds*/
    }

    sub = malloc(sizeof(rbusEventSubscription_t));

    sub->handle = handle;
    sub->eventName = strdup(eventName);
    sub->handler = handler;

    sub->userData = userData;
    sub->filter = filter;
    sub->duration = duration;
    sub->interval = interval;
    sub->asyncHandler = async;

    if(sub->filter)
        rbusFilter_Retain(sub->filter);

    payload = rbusEvent_CreatePayloadEx(sub);

    if(sub->asyncHandler)
    {
        rbusAsyncSubscribe_AddSubscription(sub);

        return RBUS_ERROR_SUCCESS;
    }

    for(;;)
    {
        RBUSLOG_INFO("%s: %s subscribing", __FUNCTION__, eventName);

        coreerr = rbus_subscribeToEventTimeout(NULL, sub->eventName, _event_callback_handler, payload, sub, &providerError, destNotFoundTimeout);
        
        if(coreerr == RTMESSAGE_BUS_ERROR_DESTINATION_UNREACHABLE && destNotFoundTimeout > 0)
        {
            int sleepTime = destNotFoundSleep;

            if(sleepTime > destNotFoundTimeout)
                sleepTime = destNotFoundTimeout;

            RBUSLOG_INFO("%s: %s no provider. retry in %d ms with %d left", 
                __FUNCTION__, eventName, sleepTime, destNotFoundTimeout );

            //TODO: do we need pthread_cond_timedwait ?  e.g. maybe another thread calls rbus_close and we need to shutdown
            sleep(sleepTime/1000);

            destNotFoundTimeout -= destNotFoundSleep;

            //double the wait time
            destNotFoundSleep *= 2;

            //cap it so the wait time still allows frequent retries
            if(destNotFoundSleep > rbusConfig_Get()->subscribeMaxWait)
                destNotFoundSleep = rbusConfig_Get()->subscribeMaxWait;
        }
        else
        {
            break;
        }
    }

    if(payload)
    {
        rbusMessage_Release(payload);
    }

    if(coreerr == RTMESSAGE_BUS_SUCCESS)
    {
        rtVector_PushBack(handleInfo->eventSubs, sub);

        RBUSLOG_INFO("%s: %s subscribe retries succeeded", __FUNCTION__, eventName);
        
        return RBUS_ERROR_SUCCESS;
    }
    else
    {
        rbusEventSubscription_free(sub);

        if(coreerr == RTMESSAGE_BUS_ERROR_DESTINATION_UNREACHABLE)
        {
            RBUSLOG_INFO("%s: %s all subscribe retries failed because no provider could be found", __FUNCTION__, eventName);
            RBUSLOG_WARN("EVENT_SUBSCRIPTION_FAIL_NO_PROVIDER_COMPONENT  %s", eventName);/*RDKB-33658-AC7*/
            return RBUS_ERROR_TIMEOUT;
        }
        else if(providerError != RBUS_ERROR_SUCCESS)
        {   
            RBUSLOG_INFO("%s: %s subscribe retries failed due provider error %d", __FUNCTION__, eventName, providerError);
            RBUSLOG_WARN("EVENT_SUBSCRIPTION_FAIL_INVALID_INPUT  %s", eventName);/*RDKB-33658-AC9*/
            return providerError;
        }
        else
        {
            RBUSLOG_INFO("%s: %s subscribe retries failed due to core error %d", __FUNCTION__, eventName, coreerr);
            return RBUS_ERROR_BUS_ERROR;
        }
    }
}

rbusError_t  rbusEvent_Subscribe(
    rbusHandle_t        handle,
    char const*         eventName,
    rbusEventHandler_t  handler,
    void*               userData,
    int                 timeout)
{
    rbusError_t errorcode;

    RBUSLOG_INFO("%s: %s", __FUNCTION__, eventName);

    errorcode = rbusEvent_SubscribeWithRetries(handle, eventName, handler, userData, NULL, 0, 0 , timeout, NULL);

    if(errorcode != RBUS_ERROR_SUCCESS)
    {
        RBUSLOG_WARN("%s: %s failed err=%d", __FUNCTION__, eventName, errorcode);
    }

    return errorcode;
}

rbusError_t  rbusEvent_SubscribeAsync(
    rbusHandle_t                    handle,
    char const*                     eventName,
    rbusEventHandler_t              handler,
    rbusSubscribeAsyncRespHandler_t subscribeHandler,
    void*                           userData,
    int                             timeout)
{
    rbusError_t errorcode;

    RBUSLOG_INFO("%s: %s", __FUNCTION__, eventName);

    errorcode = rbusEvent_SubscribeWithRetries(handle, eventName, handler, userData, NULL, 0, 0, timeout, subscribeHandler);

    if(errorcode != RBUS_ERROR_SUCCESS)
    {
        RBUSLOG_WARN("%s: %s failed err=%d", __FUNCTION__, eventName, errorcode);
    }

    return errorcode;
}

rbusError_t rbusEvent_Unsubscribe(
    rbusHandle_t        handle,
    char const*         eventName)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    rbusEventSubscription_t* sub;

    RBUSLOG_INFO("%s: %s", __FUNCTION__, eventName);

    /*the use of rtVector is inefficient here.  I have to loop through the vector to find the sub by name, 
        then call RemoveItem, which loops through again to find the item by address to destroy */
    sub = rbusEventSubscription_find(handleInfo->eventSubs, eventName, NULL);

    if(sub)
    {
        rbus_error_t coreerr = rbus_unsubscribeFromEvent(NULL, eventName, NULL);

        rtVector_RemoveItem(handleInfo->eventSubs, sub, rbusEventSubscription_free);

        if(coreerr == RTMESSAGE_BUS_SUCCESS)
        {
            return RBUS_ERROR_SUCCESS;
        }
        else
        {
            RBUSLOG_INFO("%s: %s failed with core err=%d", __FUNCTION__, eventName, coreerr);
            
            if(coreerr == RTMESSAGE_BUS_ERROR_DESTINATION_UNREACHABLE)
            {
                return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
            }
            else
            {
                return RBUS_ERROR_BUS_ERROR;
            }
        }
    }
    else
    {
        RBUSLOG_INFO("%s: %s no existing subscription found", __FUNCTION__, eventName);
        return RBUS_ERROR_INVALID_OPERATION; //TODO - is the the right error to return
    }
}

rbusError_t rbusEvent_SubscribeEx(
    rbusHandle_t                handle,
    rbusEventSubscription_t*    subscription,
    int                         numSubscriptions,
    int                         timeout)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    int i, j;

    for(i = 0; i < numSubscriptions; ++i)
    {
        RBUSLOG_INFO("%s: %s", __FUNCTION__, subscription[i].eventName);

        //FIXME/TODO -- since this is not using async path, this could block and thus block the rest of the subs to come
        //For rbusEvent_Subscribe, since it a single subscribe, blocking is fine but for rbusEvent_SubscribeEx,
        //where we can have multiple, we need to actually run all these in parallel.  So we might need to leverage
        //the asyncsubscribe api to handle this.
        errorcode = rbusEvent_SubscribeWithRetries(
            handle, subscription[i].eventName, subscription[i].handler, subscription[i].userData, 
            subscription[i].filter, subscription[i].interval, subscription[i].duration, timeout, NULL);

        if(errorcode != RBUS_ERROR_SUCCESS)
        {
            RBUSLOG_WARN("%s: %s failed err=%d", __FUNCTION__, subscription[i].eventName, errorcode);

            /*  Treat SubscribeEx like a transaction because
                if any subs fails, how will the user know which ones succeeded and which failed ?
                So, as a transaction, we just undo everything, which are all those from 0 to i-1.
            */
            for(j = 0; j < i; ++j)
            {
                rbusEvent_Unsubscribe(handle, subscription[i].eventName);
            }
            break;
        }
    }

    return errorcode;
}

rbusError_t rbusEvent_SubscribeExAsync(
    rbusHandle_t                    handle,
    rbusEventSubscription_t*        subscription,
    int                             numSubscriptions,
    rbusSubscribeAsyncRespHandler_t subscribeHandler,
    int                             timeout)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
    int i, j;

    for(i = 0; i < numSubscriptions; ++i)
    {
        RBUSLOG_INFO("%s: %s", __FUNCTION__, subscription[i].eventName);

        errorcode = rbusEvent_SubscribeWithRetries(
            handle, subscription[i].eventName, subscription[i].handler, subscription[i].userData, 
            subscription[i].filter, subscription[i].interval, subscription[i].duration, timeout, subscribeHandler);

        if(errorcode != RBUS_ERROR_SUCCESS)
        {
            RBUSLOG_WARN("%s: %s failed err=%d", __FUNCTION__, subscription[i].eventName, errorcode);

            /*  Treat SubscribeEx like a transaction because
                if any subs fails, how will the user know which ones succeeded and which failed ?
                So, as a transaction, we just undo everything, which are all those from 0 to i-1.
            */
            for(j = 0; j < i; ++j)
            {
                rbusEvent_Unsubscribe(handle, subscription[i].eventName);
            }
            break;
        }
    }

    return errorcode;    
}

rbusError_t rbusEvent_UnsubscribeEx(
    rbusHandle_t                handle,
    rbusEventSubscription_t*    subscription,
    int                         numSubscriptions)
{
    rbusError_t errorcode = RBUS_ERROR_SUCCESS;
   
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    int i;

    //TODO we will call unsubscribe for every sub in list
    //if any unsubscribe fails below we use RBUS_ERROR_BUS_ERROR for return error
    //The caller will have no idea which ones failed to unsub and which succeeded (if any)
    //and unlike SubscribeEx, I don't think we can treat this like a transactions because
    //its assumed that caller has successfully subscribed before so we need to attempt all 
    //to get as many as possible unsubscribed and off the bus

    for(i = 0; i < numSubscriptions; ++i)
    {
        rbusEventSubscription_t* sub;

        RBUSLOG_INFO("%s: %s", __FUNCTION__, subscription[i].eventName);

        /*the use of rtVector is inefficient here.  I have to loop through the vector to find the sub by name, 
            then call RemoveItem, which loops through again to find the item by address to destroy */
        sub = rbusEventSubscription_find(handleInfo->eventSubs, subscription[i].eventName, subscription[i].filter);

        if(sub)
        {
            rbus_error_t coreerr;
            rbusMessage payload;

            payload = rbusEvent_CreatePayloadEx(&subscription[i]);

            coreerr = rbus_unsubscribeFromEvent(NULL, subscription[i].eventName, payload);

            if(payload)
            {
                rbusMessage_Release(payload);
            }

            rtVector_RemoveItem(handleInfo->eventSubs, sub, rbusEventSubscription_free);

            if(coreerr != RTMESSAGE_BUS_SUCCESS)
            {
                RBUSLOG_INFO("%s: %s failed with core err=%d", __FUNCTION__, subscription[i].eventName, coreerr);
                
                //FIXME -- we just overwrite any existing error that might have happened in a previous loop
                if(coreerr == RTMESSAGE_BUS_ERROR_DESTINATION_UNREACHABLE)
                {
                    errorcode = RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
                }
                else
                {
                    errorcode = RBUS_ERROR_BUS_ERROR;
                }
            }
        }
        else
        {
            RBUSLOG_INFO("%s: %s no existing subscription found", __FUNCTION__, subscription[i].eventName);
            errorcode = RBUS_ERROR_INVALID_OPERATION; //TODO - is the the right error to return
        }
    }

    return errorcode;
}

rbusError_t  rbusEvent_Publish(
  rbusHandle_t          handle,
  rbusEvent_t*          eventData)
{
    struct _rbusHandle* handleInfo = (struct _rbusHandle*)handle;
    rbus_error_t err, errOut = RTMESSAGE_BUS_SUCCESS;
    rtListItem listItem;
    rbusSubscription_t* subscription;

    RBUSLOG_INFO("%s: %s", __FUNCTION__, eventData->name);

    /*get the node and walk its subscriber list, 
      publishing event to each subscriber*/
    elementNode* el = retrieveInstanceElement(handleInfo->elementRoot, eventData->name);

    if(!el)
    {
        RBUSLOG_WARN("rbusEvent_Publish failed: retrieveElement return NULL for %s", eventData->name);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    if(!el->subscriptions)/*nobody subscribed yet*/
    {
        return RBUS_ERROR_SUCCESS;
    }

    rtList_GetFront(el->subscriptions, &listItem);
    while(listItem)
    {
        bool publish = true;

        rtListItem_GetData(listItem, (void**)&subscription);
        if(!subscription || !subscription->eventName || !subscription->listener)
        {
            RBUSLOG_INFO("rbusEvent_Publish failed: null subscriber data");
            if(errOut == RTMESSAGE_BUS_SUCCESS)
                errOut = RTMESSAGE_BUS_ERROR_GENERAL;
            rtListItem_GetNext(listItem, &listItem);
        }

        /* Commented out the following experiment.  Leaving it the comment for now.
           The idea here was to only publish to the subscriber who either didn't have a filter,
           or had a filter that was triggered.  So subscribers who had a filter that wasn't
           triggered, would not get an event.  This would allow multiple consumers to 
           subscribe to the same property but with different filters, and those consumers
           would only get events when their specific filter was triggered.
           So currently, without this code, if one consumer's filter is triggered, all
           consumer's subscribed to this same property will get the event.
        */
#if 0
        /* apply filter for value change events */
        if(eventData->type == RBUS_EVENT_VALUE_CHANGED)
        {
            /*it is a code bug to call value change for non-properties*/
            assert(el->type == RBUS_ELEMENT_TYPE_PROPERTY);

            /* if autoPublish then rbus_valuechange should be the only one calling us*/
            if(subscription->autoPublish)
            {
                /* if the subscriber has a filter we check the filter to determine if we publish.
                   if the subscriber does not have a filter, we publish always*/
                if(subscription->filter)
                {
                    /*We publish an event only when the value crosses the filter threshold boundary.
                      When the value crosses into the threshold we publish a single event signally the filter started matching.
                      When the value crosses out of the threshold we publish a single event signally the filter stopped matching.
                      We do not publish continuous events while the filter continues to match. The consumer can read the 'filter'
                      property from the event data to determine if the filter has started or stopped matching. */

                    rbusValue_t valNew, valOld, valFilter;
                    int cNew, cOld;
                    int result = -1;/*-1 means don't publish*/
    
                    valNew = rbusObject_GetValue(eventData->data, "value");
                    valOld = rbusObject_GetValue(eventData->data, "oldValue");
                    valFilter = subscription->filter->filter.threshold.value;

                    cNew = rbusValue_Compare(valNew, valFilter);
                    cOld = rbusValue_Compare(valOld, valFilter);

                    switch(subscription->filter->filter.threshold.type)
                    {
                    case RBUS_THRESHOLD_ON_CHANGE_GREATER_THAN:
                        if(cNew > 0 && cOld <= 0)
                            result = 1;
                        else if(cNew <= 0 && cOld > 0)
                            result = 0;
                        break;
                    case RBUS_THRESHOLD_ON_CHANGE_GREATER_THAN_OR_EQUAL:
                        if(cNew >= 0 && cOld < 0)
                            result = 1;
                        else if(cNew < 0 && cOld >= 0)
                            result = 0;
                        break;
                    case RBUS_THRESHOLD_ON_CHANGE_LESS_THAN:
                        if(cNew < 0 && cOld >= 0)
                            result = 1;
                        else if(cNew >= 0 && cOld < 0)
                            result = 0;
                        break;
                    case RBUS_THRESHOLD_ON_CHANGE_LESS_THAN_OR_EQUAL:
                        if(cNew <= 0 && cOld > 0)
                            result = 1;
                        else if(cNew > 0 && cOld <= 0)
                            result = 0;
                        break;
                    case RBUS_THRESHOLD_ON_CHANGE_EQUAL:
                        if(cNew == 0 && cOld != 0)
                            result = 1;
                        else if(cNew != 0 && cOld == 0)
                            result = 0;
                        break;
                    case RBUS_THRESHOLD_ON_CHANGE_NOT_EQUAL:
                        if(cNew != 0 && cOld == 0)
                            result = 1;
                        else if(cNew == 0 && cOld != 0)
                            result = 0;
                        break;
                    default: 
                        break;
                    }

                    if(result != -1)
                    {
                        /*set 'filter' to true/false implying that either the filter has started or stopped matching*/
                        rbusValue_t val;
                        rbusValue_Init(&val);
                        rbusValue_SetBoolean(val, result);
                        rbusObject_SetValue(eventData->data, "filter", val);
                        rbusValue_Release(val);
                    }
                    else
                    {
                        publish = false;
                    }
                }
            }
            else
            {
                /* If autoPublish is false then a provider should be the only one calling us.
                   Its expected that the provider will apply any filter so if the provider has
                   set the filter then we publish to the subscriber owning that filter.
                   If the provider set the filter NULL then we publish to all subscribers. */
                if(eventData->filter && eventData->filter != subscription->filter)
                {
                    publish = false;
                }
            }
        }
#endif
        if(publish)
        {
            rbusMessage msg;

            rbusMessage_Init(&msg);
            rbusEvent_appendToMessage(eventData, msg);

            RBUSLOG_INFO("rbusEvent_Publish: publising event %s to listener %s", subscription->eventName, subscription->listener);
            err = rbus_publishSubscriberEvent(
                handleInfo->componentName,  
                subscription->eventName/*use the same eventName the consumer subscribed with; not event instance name eventData->name*/, 
                subscription->listener, 
                msg);

            rbusMessage_Release(msg);

            if(err != RTMESSAGE_BUS_SUCCESS)
            {
                if(errOut == RTMESSAGE_BUS_SUCCESS)
                    errOut = err;
                RBUSLOG_INFO("rbusEvent_Publish faild: rbus_publishSubscriberEvent return error %d", err);
            }
        }

        rtListItem_GetNext(listItem, &listItem);
    }

    return errOut == RTMESSAGE_BUS_SUCCESS ? RBUS_ERROR_SUCCESS: RBUS_ERROR_BUS_ERROR;
}

rbusError_t rbusMethod_InvokeInternal(
    rbusHandle_t handle, 
    char const* methodName, 
    rbusObject_t inParams, 
    rbusObject_t* outParams,
    int timeout)
{
    (void)handle;
    rbus_error_t err;
    int returnCode = 0;
    rbusMessage request, response;
    rbusLegacyReturn_t legacyRetCode = RBUS_LEGACY_ERR_FAILURE;

    RBUSLOG_INFO("%s: %s", __FUNCTION__, methodName);

    rbusMessage_Init(&request);
    rbusMessage_SetInt32(request, 0);/*TODO: this should be the session ID*/
    rbusMessage_SetString(request, methodName); /*TODO: do we need to append the name as well as pass the name as the 1st arg to rbus_invokeRemoteMethod ?*/

    if(inParams)
        rbusObject_appendToMessage(inParams, request);

    if((err = rbus_invokeRemoteMethod(
        methodName,
        METHOD_RPC, 
        request, 
        timeout, 
        &response)) != RTMESSAGE_BUS_SUCCESS)
    {
        RBUSLOG_INFO("%s rbus_invokeRemoteMethod failed with err %d", __FUNCTION__, err);
        return rbuscoreError_to_rbusError(err);
    }

    rbusMessage_GetInt32(response, &returnCode);
    legacyRetCode = (rbusLegacyReturn_t)returnCode;

    if(returnCode == RBUS_ERROR_SUCCESS || (legacyRetCode == RBUS_LEGACY_ERR_SUCCESS))
    {
        rbusObject_initFromMessage(outParams, response);
    }
    else
    {
        if(legacyRetCode > RBUS_LEGACY_ERR_SUCCESS)
        {
            returnCode = CCSPError_to_rbusError(legacyRetCode);
        }
    }

    rbusMessage_Release(response);

    RBUSLOG_INFO("%s rbus_invokeRemoteMethod success response returnCode:%d", __FUNCTION__, returnCode);

    return returnCode;
}

rbusError_t rbusMethod_Invoke(
    rbusHandle_t handle, 
    char const* methodName, 
    rbusObject_t inParams, 
    rbusObject_t* outParams)
{
    return rbusMethod_InvokeInternal(handle, methodName, inParams, outParams, 6000);
}

typedef struct _rbusMethodInvokeAsyncData_t
{
    rbusHandle_t handle;
    char* methodName; 
    rbusObject_t inParams; 
    rbusMethodAsyncRespHandler_t callback;
    int timeout;
} rbusMethodInvokeAsyncData_t;

static void* rbusMethod_InvokeAsyncThreadFunc(void *p)
{
    rbusError_t err;
    rbusMethodInvokeAsyncData_t* data = p;
    rbusObject_t outParams = NULL;

    err = rbusMethod_InvokeInternal(
        data->handle,
        data->methodName, 
        data->inParams, 
        &outParams,
        data->timeout);

    data->callback(data->handle, data->methodName, err, outParams);

    rbusObject_Release(data->inParams);
    if(outParams)
        rbusObject_Release(outParams);
    free(data->methodName);
    free(data);

    return NULL;
}

rbusError_t rbusMethod_InvokeAsync(
    rbusHandle_t handle, 
    char const* methodName, 
    rbusObject_t inParams, 
    rbusMethodAsyncRespHandler_t callback, 
    int timeout)
{
    pthread_t pid;
    rbusMethodInvokeAsyncData_t* data;
    int err = 0;

    rbusObject_Retain(inParams);

    data = malloc(sizeof(rbusMethodInvokeAsyncData_t));
    data->handle = handle;
    data->methodName = strdup(methodName);
    data->inParams = inParams;
    data->callback = callback;
    data->timeout = timeout > 0 ? timeout : 6000;

    if((err = pthread_create(&pid, NULL, rbusMethod_InvokeAsyncThreadFunc, data)) != 0)
    {
        RBUSLOG_ERROR("%s pthread_create failed: err=%d", __FUNCTION__, err);
        return RBUS_ERROR_BUS_ERROR;
    }

	if((err = pthread_detach(pid)) != 0)
    {
        RBUSLOG_ERROR("%s pthread_detach failed: err=%d", __FUNCTION__, err);
    }

    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbusMethod_SendAsyncResponse(
    rbusMethodAsyncHandle_t asyncHandle,
    rbusError_t error,
    rbusObject_t outParams)
{
    rbusMessage response;

    rbusMessage_Init(&response);
    rbusMessage_SetInt32(response, error);
    if(outParams)
        rbusObject_appendToMessage(outParams, response);
    rbus_sendResponse(&asyncHandle->hdr, response);
    free(asyncHandle);
    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbus_createSession(rbusHandle_t handle, uint32_t *pSessionId)
{
    (void)handle;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rbusMessage response;
    if (pSessionId)
    {
        *pSessionId = 0;
        if((err = rbus_invokeRemoteMethod(RBUS_SMGR_DESTINATION_NAME, RBUS_SMGR_METHOD_REQUEST_SESSION_ID, NULL, 6000, &response)) == RTMESSAGE_BUS_SUCCESS)
        {
            rbusMessage_GetInt32(response, /*MESSAGE_FIELD_RESULT,*/ (int*) &err);
            if(RTMESSAGE_BUS_SUCCESS != err)
            {
                RBUSLOG_ERROR("Session manager reports internal error %d.", err);
                rc = RBUS_ERROR_SESSION_ALREADY_EXIST;
            }
            else
            {
                rbusMessage_GetInt32(response, /*MESSAGE_FIELD_PAYLOAD,*/ (int*) pSessionId);
                RBUSLOG_INFO("Received new session id %u", *pSessionId);
            }
        }
        else
        {
            RBUSLOG_ERROR("Failed to communicated with session manager.");
            rc = rbuscoreError_to_rbusError(err);
        }
    }
    else
    {
        RBUSLOG_WARN("Invalid Input passed..");
        rc = RBUS_ERROR_INVALID_INPUT;
    }
    return rc;
}

rbusError_t rbus_getCurrentSession(rbusHandle_t handle, uint32_t *pSessionId)
{
    (void)handle;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    rbus_error_t err = RTMESSAGE_BUS_SUCCESS;
    rbusMessage response;

    if (pSessionId)
    {
        *pSessionId = 0;
        if((err = rbus_invokeRemoteMethod(RBUS_SMGR_DESTINATION_NAME, RBUS_SMGR_METHOD_GET_CURRENT_SESSION_ID, NULL, 6000, &response)) == RTMESSAGE_BUS_SUCCESS)
        {
            rbusMessage_GetInt32(response, /*MESSAGE_FIELD_RESULT,*/ (int*) &err);
            if(RTMESSAGE_BUS_SUCCESS != err)
            {
                RBUSLOG_ERROR("Session manager reports internal error %d.", err);
                rc = RBUS_ERROR_SESSION_ALREADY_EXIST;
            }
            else
            {
                rbusMessage_GetInt32(response, /*MESSAGE_FIELD_PAYLOAD,*/ (int*) pSessionId);
                RBUSLOG_INFO("Received new session id %u", *pSessionId);
            }
        }
        else
        {
            RBUSLOG_ERROR("Failed to communicated with session manager.");
            rc = rbuscoreError_to_rbusError(err);
        }
    }
    else
    {
        RBUSLOG_WARN("Invalid Input passed..");
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
        rbusMessage inputSession;
        rbusMessage response;

        rbusMessage_Init(&inputSession);
        rbusMessage_SetInt32(inputSession, /*MESSAGE_FIELD_PAYLOAD,*/ sessionId);
        if((err = rbus_invokeRemoteMethod(RBUS_SMGR_DESTINATION_NAME, RBUS_SMGR_METHOD_END_SESSION, inputSession, 6000, &response)) == RTMESSAGE_BUS_SUCCESS)
        {
            rbusMessage_GetInt32(response, /*MESSAGE_FIELD_RESULT,*/ (int*) &err);
            if(RTMESSAGE_BUS_SUCCESS != err)
            {
                RBUSLOG_ERROR("Session manager reports internal error %d.", err);
                rc = RBUS_ERROR_SESSION_ALREADY_EXIST;
            }
            else
                RBUSLOG_INFO("Successfully ended session %u.", sessionId);
        }
        else
        {
            RBUSLOG_ERROR("Failed to communicated with session manager.");
            rc = rbuscoreError_to_rbusError(err);
        }
    }
    else
    {
        RBUSLOG_WARN("Invalid Input passed..");
        rc = RBUS_ERROR_INVALID_INPUT;
    }

    return rc;
}

rbusStatus_t rbus_checkStatus(void)
{
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
        RBUSLOG_WARN("Invalid Input passed..");
        return RBUS_ERROR_INVALID_INPUT;
    }
}

rbusError_t rbus_setLogLevel(rbusLogLevel_t level)
{
    if (level <= RBUS_LOG_FATAL)
    {
        rtLog_SetLevel((rtLogLevel) level);
        return RBUS_ERROR_SUCCESS;
    }
    else
        return RBUS_ERROR_INVALID_INPUT;
}

char const*
rbusError_ToString(rbusError_t e)
{

#define rbusError_String(E, S) case E: s = S; break;

  char const * s = NULL;
  switch (e)
  {
    rbusError_String(RBUS_ERROR_SUCCESS, "ok");
    rbusError_String(RBUS_ERROR_BUS_ERROR, "generic error");
    rbusError_String(RBUS_ERROR_INVALID_INPUT, "invalid input");
    rbusError_String(RBUS_ERROR_NOT_INITIALIZED, "not initialized");
    rbusError_String(RBUS_ERROR_OUT_OF_RESOURCES, "out of resources");
    rbusError_String(RBUS_ERROR_DESTINATION_NOT_FOUND, "destination not found");
    rbusError_String(RBUS_ERROR_DESTINATION_NOT_REACHABLE, "destination not reachable");
    rbusError_String(RBUS_ERROR_DESTINATION_RESPONSE_FAILURE, "destination response failure");
    rbusError_String(RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION, "invalid response from destination");
    rbusError_String(RBUS_ERROR_INVALID_OPERATION, "invalid operation");
    rbusError_String(RBUS_ERROR_INVALID_EVENT, "invalid event");
    rbusError_String(RBUS_ERROR_INVALID_HANDLE, "invalid handle");
    rbusError_String(RBUS_ERROR_SESSION_ALREADY_EXIST, "session already exists");
    rbusError_String(RBUS_ERROR_COMPONENT_NAME_DUPLICATE, "duplicate component name");
    rbusError_String(RBUS_ERROR_ELEMENT_NAME_DUPLICATE, "duplicate element name");
    rbusError_String(RBUS_ERROR_ELEMENT_NAME_MISSING, "name missing");
    rbusError_String(RBUS_ERROR_COMPONENT_DOES_NOT_EXIST, "component does not exist");
    rbusError_String(RBUS_ERROR_ELEMENT_DOES_NOT_EXIST, "element name does not exist");
    rbusError_String(RBUS_ERROR_ACCESS_NOT_ALLOWED, "access denied");
    rbusError_String(RBUS_ERROR_INVALID_CONTEXT, "invalid context");
    rbusError_String(RBUS_ERROR_TIMEOUT, "timeout");
    rbusError_String(RBUS_ERROR_ASYNC_RESPONSE, "async operation in progress");
    default:
      s = "unknown error";
  }
  return s;
}

/* End of File */
