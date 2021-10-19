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

#define _GNU_SOURCE 1
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include <float.h>
#include <rtmessage/rtRetainable.h>
#include <limits.h>
#include "rbus_buffer.h"
#include "rbus_log.h"

#define RBUS_TIMEZONE_LEN   6

struct _rbusValue
{
    rtRetainable retainable;
    union
    {
        bool                    b;
        char                    c;
        unsigned char           u;
        int8_t                  i8;
        uint8_t                 u8;
        int16_t                 i16;
        uint16_t                u16;
        int32_t                 i32;
        uint32_t                u32;
        int64_t                 i64;
        uint64_t                u64;
        float                   f32;
        double                  f64;
        rbusDateTime_t          tv;
        rbusBuffer_t            bytes;
        struct  _rbusProperty*  property;
        struct  _rbusObject*    object;
    } d;
    rbusValueType_t type;
};

char const* rbusValueType_ToDebugString(rbusValueType_t type)
{
    char const* s = NULL;
    #define __RBUSVALUE_STRINGIFY(E) case E: s = #E; break

    switch(type)
    {
      __RBUSVALUE_STRINGIFY(RBUS_STRING);
      __RBUSVALUE_STRINGIFY(RBUS_BYTES);
      __RBUSVALUE_STRINGIFY(RBUS_BOOLEAN);
      __RBUSVALUE_STRINGIFY(RBUS_CHAR);
      __RBUSVALUE_STRINGIFY(RBUS_BYTE);
      __RBUSVALUE_STRINGIFY(RBUS_INT16);
      __RBUSVALUE_STRINGIFY(RBUS_UINT16);
      __RBUSVALUE_STRINGIFY(RBUS_INT32);
      __RBUSVALUE_STRINGIFY(RBUS_UINT32);
      __RBUSVALUE_STRINGIFY(RBUS_INT64);
      __RBUSVALUE_STRINGIFY(RBUS_UINT64);
      __RBUSVALUE_STRINGIFY(RBUS_SINGLE);
      __RBUSVALUE_STRINGIFY(RBUS_DOUBLE);
      __RBUSVALUE_STRINGIFY(RBUS_DATETIME);
      __RBUSVALUE_STRINGIFY(RBUS_PROPERTY);
      __RBUSVALUE_STRINGIFY(RBUS_OBJECT);
      __RBUSVALUE_STRINGIFY(RBUS_NONE);
      default:
        s = "unknown type";
        break;
    }
    return s;
}

static void rbusValue_FreeInternal(rbusValue_t v)
{
    if( (v->type == RBUS_STRING || v->type == RBUS_BYTES) && v->d.bytes )
    {
        rbusBuffer_Destroy(v->d.bytes);
    }
    else if(v->type == RBUS_PROPERTY && v->d.property)
    {
        rbusProperty_Release(v->d.property);
    }
    else if(v->type == RBUS_OBJECT && v->d.object)
    {
        rbusObject_Release(v->d.object);
    }
    v->d.bytes = NULL; /*set NULL in all cases as GetString/GetBytes rely on this*/


}

void rbusValue_Init(rbusValue_t* v)
{
    (*v) = malloc(sizeof(struct _rbusValue));
    (*v)->d.bytes = NULL;
    (*v)->type = RBUS_NONE;
    (*v)->retainable.refCount = 1;
}

void rbusValue_Destroy(rtRetainable* r)
{
    rbusValue_t v = (rbusValue_t)r;
    rbusValue_FreeInternal(v);
    v->type = RBUS_NONE;
    free(v);
}

void rbusValue_Retain(rbusValue_t v)
{
    rtRetainable_retain(v);
}

void rbusValue_Release(rbusValue_t v)
{
    rtRetainable_release(v, rbusValue_Destroy);
}

char* rbusValue_ToString(rbusValue_t v, char* buf, size_t buflen)
{
    char* p = NULL;
    int n;

    if(v->type == RBUS_NONE)
        return NULL;

    if (buf)
    {
      p = buf;
      n = (int)buflen;

        switch(v->type)
        {
        case RBUS_STRING:
            if(n > v->d.bytes->posWrite)
                n = v->d.bytes->posWrite;
            break;
        case RBUS_BYTES:
            if(n > v->d.bytes->posWrite + 1)
                n = v->d.bytes->posWrite + 1;
            break;
        default:
            break;
        }
    }
    else
    {
        switch(v->type)
        {
        case RBUS_STRING:
            n = v->d.bytes->posWrite;
            break;
        case RBUS_BYTES:
            n = (2 * v->d.bytes->posWrite) + 1;
            break;
        case RBUS_BOOLEAN:
            n = snprintf(p, 0, "%d", (int)v->d.b)+1;
            break;
        case RBUS_INT32:
            n = snprintf(p, 0, "%d", v->d.i32)+1;
            break;
        case RBUS_UINT32:
            n = snprintf(p, 0, "%u", v->d.u32)+1;
            break;
        case RBUS_CHAR:
            n = snprintf(p, 0, "%c", v->d.c)+1;
            break;
        case RBUS_BYTE:
            n = snprintf(p, 0, "%x", v->d.u)+1;
            break;
        case RBUS_INT8:
            n = snprintf(p, 0, "%d", v->d.i8)+1;
            break;
        case RBUS_UINT8:
            n = snprintf(p, 0, "%u", v->d.u8)+1;
            break;
        case RBUS_INT16:
            n = snprintf(p, 0, "%hd", v->d.i16)+1;
            break;
        case RBUS_UINT16:
            n = snprintf(p, 0, "%hu", v->d.u16)+1;
            break;
        case RBUS_INT64:
            n = snprintf(p, 0, "%" PRIi64, v->d.i64)+1;
            break;
        case RBUS_UINT64:
            n = snprintf(p, 0, "%" PRIu64, v->d.u64)+1;
            break;
        case RBUS_SINGLE:
            n = snprintf(p, 0, "%*f", FLT_DIG, v->d.f32)+1;
            break;
        case RBUS_DOUBLE:
            n = snprintf(p, 0, "%.*f", DBL_DIG, v->d.f64)+1;
            break;
        case RBUS_DATETIME:
            n = snprintf(p, 0, "0000-00-00T00:00:00+00:00") + 1;
            break;
        default:
            n = snprintf(p, 0, "FIXME TYPE %d", v->type)+1;
            break;
        }
        p = calloc(n, 1);
    }
    
    switch(v->type)
    {
    case RBUS_STRING:
        strncpy(p, (char const* ) v->d.bytes->data, n);
        break;
    case RBUS_BYTES:
    {
        int i = 0;
        for (i = 0; i < v->d.bytes->posWrite; i++)
            sprintf (&p[i * 2], "%02X", v->d.bytes->data[i]);
        p[2 * v->d.bytes->posWrite] = 0;
        break;
    }
    case RBUS_BOOLEAN:
        snprintf(p, n, "%d", (int)v->d.b);
        break;
    case RBUS_INT32:
        snprintf(p, n, "%d", v->d.i32);
        break;
    case RBUS_UINT32:
        snprintf(p, n, "%u", v->d.u32);
        break;
    case RBUS_CHAR:
        snprintf(p, n, "%c", v->d.c);
        break;
    case RBUS_BYTE:
        snprintf(p, n, "%x", v->d.u);
        break;
    case RBUS_INT8:
        snprintf(p, n, "%d", v->d.i8);
        break;
    case RBUS_UINT8:
        snprintf(p, n, "%u", v->d.u8);
        break;
    case RBUS_INT16:
        snprintf(p, n, "%hd", v->d.i16);
        break;
    case RBUS_UINT16:
        snprintf(p, n, "%hu", v->d.u16);
        break;
    case RBUS_INT64:
        snprintf(p, n, "%" PRIi64, v->d.i64);
        break;
    case RBUS_UINT64:
        snprintf(p, n, "%" PRIu64, v->d.u64);
        break;
    case RBUS_SINGLE:
        snprintf(p, n, "%*f", FLT_DIG, v->d.f32);
        break;
    case RBUS_DOUBLE:
        snprintf(p, n, "%.*f", DBL_DIG, v->d.f64);
        break;
    case RBUS_DATETIME:
        {
            char tmpBuff[40] = {0}; /* 27 bytes is good enough; */
            if(v->d.tv.m_tz.m_tzhour || v->d.tv.m_tz.m_tzmin) {
                if(v->d.tv.m_tz.m_isWest)
                    snprintf(tmpBuff, 40, "-%02d:%02d",v->d.tv.m_tz.m_tzhour, v->d.tv.m_tz.m_tzmin);
                else
                    snprintf(tmpBuff, 40, "+%02d:%02d",v->d.tv.m_tz.m_tzhour, v->d.tv.m_tz.m_tzmin);
            } else {
                snprintf(tmpBuff, 40, "Z");
            }
            if(0 == v->d.tv.m_time.tm_year) {
                snprintf(p, n, "%04d-%02d-%02dT%02d:%02d:%02d%s", v->d.tv.m_time.tm_year,
                                                                    v->d.tv.m_time.tm_mon,
                                                                    v->d.tv.m_time.tm_mday,
                                                                    v->d.tv.m_time.tm_hour,
                                                                    v->d.tv.m_time.tm_min,
                                                                    v->d.tv.m_time.tm_sec,
                                                                    tmpBuff);
            } else {
                /* tm_mon represents month from 0 to 11. So increment tm_mon by 1.
                   tm_year represents years since 1900. So add 1900 to tm_year.
                 */
                snprintf(p, n, "%04d-%02d-%02dT%02d:%02d:%02d%s", v->d.tv.m_time.tm_year+1900,
                                                                    v->d.tv.m_time.tm_mon+1,
                                                                    v->d.tv.m_time.tm_mday,
                                                                    v->d.tv.m_time.tm_hour,
                                                                    v->d.tv.m_time.tm_min,
                                                                    v->d.tv.m_time.tm_sec,
                                                                    tmpBuff);
            }
            break;
        }
    case RBUS_NONE:
        
    default:
        snprintf(p, n, "FIXME TYPE %d", v->type);
        break;
    }
    return p;
}

char* rbusValue_ToDebugString(rbusValue_t v, char* buf, size_t buflen)
{
    int len = buflen;
    char* p = buf;
    char* s = rbusValue_ToString(v, NULL, 0);
    char const* t = rbusValueType_ToDebugString(v->type);
    char fmt[] = "rbusValue type:%s value:%s";
    if(!p)
    {
        len = snprintf(NULL, 0, fmt, t, s) + 1;
        p = malloc(len);
    }
    snprintf(p, len, fmt, t, s);
    free(s);
    return p;
}

rbusValueType_t rbusValue_GetType(rbusValue_t v)
{
    return v->type;
}

/*If type is RBUS_STRING and data is not NULL, len will be set to the strlen of the internal string and a c-string null terminated string is returned.
  If type is RBUS_BYTES and data is not NULL, len will set to the length of the byte array and a pointer to the raw bytes is returned
  If type is RBUS_BYTES users should take care, as we do not add a null terminator, if one is missing
  For anything else len is set to 0 and the function returns NULL
*/
char const* rbusValue_GetString(rbusValue_t v, int* len)
{
    char const* bytes = (char const*)rbusValue_GetBytes(v, len);
    /* For strings, subtract 1 so the user gets back strlen instead of byte length */
    if(bytes && len && v->type == RBUS_STRING)
        (*len)--; 
    return bytes;
}

/*If type is RBUS_BYTES or RBUS_STRING and data is not NULL, len will be set to the length of the byte array
    which will captures the null terminator if the type is RBUS_STRING.
  For anything else len is set to 0 and the function returns NULL
*/
uint8_t const* rbusValue_GetBytes(rbusValue_t v, int* len)
{
    /*v->d.bytes is NULL in the case SetBytes was called with NULL*/
    if(!v->d.bytes)
    {
        if(len)
            *len = 0;
        return NULL;
    }
    assert(v->d.bytes->data);
    assert(v->type == RBUS_STRING || v->type == RBUS_BYTES);
    assert(v->type != RBUS_STRING || strlen((char const*)v->d.bytes->data) == (size_t)v->d.bytes->posWrite-1);
    if(len)
        *len = v->d.bytes->posWrite;
    return v->d.bytes->data;
}

bool rbusValue_GetBoolean(rbusValue_t v)
{
    assert(v->type == RBUS_BOOLEAN);
    return v->d.b;
}

char rbusValue_GetChar(rbusValue_t v)
{
    assert(v->type == RBUS_CHAR);
    return v->d.c;
}

unsigned char rbusValue_GetByte(rbusValue_t v)
{
    assert(v->type == RBUS_BYTE);
    return v->d.u;
}

int8_t rbusValue_GetInt8(rbusValue_t v)
{
    assert(v->type == RBUS_INT8);
    return v->d.i8;
}

uint8_t rbusValue_GetUInt8(rbusValue_t v)
{
    assert(v->type == RBUS_UINT8);
    return v->d.u8;
}

int16_t rbusValue_GetInt16(rbusValue_t v)
{
    assert(v->type == RBUS_INT16);
    return v->d.i16;
}

uint16_t rbusValue_GetUInt16(rbusValue_t v)
{
    assert(v->type == RBUS_UINT16);
    return v->d.u16;
}

int32_t rbusValue_GetInt32(rbusValue_t v)
{
    assert(v->type == RBUS_INT32);
    return v->d.i32;
}

uint32_t rbusValue_GetUInt32(rbusValue_t v)
{
    assert(v->type == RBUS_UINT32);
    return v->d.u32;
}

int64_t rbusValue_GetInt64(rbusValue_t v)
{
    assert(v->type == RBUS_INT64);
    return v->d.i64;
}

uint64_t rbusValue_GetUInt64(rbusValue_t v)
{
    assert(v->type == RBUS_UINT64);
    return v->d.u64;
}

float rbusValue_GetSingle(rbusValue_t v)
{
    assert(v->type == RBUS_SINGLE);
    return v->d.f32;
}

double rbusValue_GetDouble(rbusValue_t v)
{
    assert(v->type == RBUS_DOUBLE);
    return v->d.f64;
}

rbusDateTime_t const* rbusValue_GetTime(rbusValue_t v)
{
    assert(v->type == RBUS_DATETIME);
    return &v->d.tv;
}

struct _rbusProperty* rbusValue_GetProperty(rbusValue_t v)
{
    assert(v->type == RBUS_PROPERTY);
    return v->d.property;
}

struct _rbusObject* rbusValue_GetObject(rbusValue_t v)
{
    assert(v->type == RBUS_OBJECT);
    return v->d.object;
}

static void rbusValue_SetBufferData(rbusValue_t v, const void* data, int len, rbusValueType_t type)
{
    if((v->type == RBUS_STRING || v->type == RBUS_BYTES) && v->d.bytes)
    {
        assert(v->d.bytes->data);
        assert(v->d.bytes->lenAlloc > 0);
        v->d.bytes->posWrite = v->d.bytes->posRead = 0;
    }
    else
    {
        rbusBuffer_Create(&v->d.bytes);
    }
    rbusBuffer_Write(v->d.bytes, data, len);
    v->type = type;
}

void rbusValue_SetString(rbusValue_t v, char const* s)
{
    /*if NULL is passed, we need to set ourselves to be a NULL RBUS_STRING
      We free the buffer (which sets it NULL) and set our type to RBUS_STRING
      Functions rbusValue_GetString & rbusValue_GetBytes can determine if
      they need to return NULL or not by checking if the buffer is NULL or not.
    */
    if(s == NULL)
    {
        rbusValue_FreeInternal(v);
        v->type = RBUS_STRING;
        return;
    }
    rbusValue_SetBufferData(v, s, strlen(s)+1, RBUS_STRING);/* +1 to write null terminator */
    assert(strlen((char const*)v->d.bytes->data)+1==(size_t)v->d.bytes->posWrite);
    assert(strlen(s)+1==(size_t)v->d.bytes->posWrite);
}

void rbusValue_SetBytes(rbusValue_t v, uint8_t const* p, int len)
{
    /*see comment in rbusValue_SetString*/
    if(p == NULL)
    {
        rbusValue_FreeInternal(v);
        v->type = RBUS_BYTES;
        return;
    }
    rbusValue_SetBufferData(v, p, len, RBUS_BYTES);
}

void rbusValue_SetBoolean(rbusValue_t v, bool b)
{
    rbusValue_FreeInternal(v);
    v->d.b = b;
    v->type = RBUS_BOOLEAN;
}

void rbusValue_SetChar(rbusValue_t v, char c)
{
    rbusValue_FreeInternal(v);
    v->d.c = c;
    v->type = RBUS_CHAR;
}

void rbusValue_SetByte(rbusValue_t v, unsigned char u)
{
    rbusValue_FreeInternal(v);
    v->d.u = u;
    v->type = RBUS_BYTE;
}

void rbusValue_SetInt8(rbusValue_t v, int8_t i8)
{
    rbusValue_FreeInternal(v);
    v->d.i8 = i8;
    v->type = RBUS_INT8;
}

void rbusValue_SetUInt8(rbusValue_t v, uint8_t u8)
{
    rbusValue_FreeInternal(v);
    v->d.u8 = u8;
    v->type = RBUS_UINT8;
}

void rbusValue_SetInt16(rbusValue_t v, int16_t i16)
{
    rbusValue_FreeInternal(v);
    v->d.i16 = i16;
    v->type = RBUS_INT16;
}

void rbusValue_SetUInt16(rbusValue_t v, uint16_t u16)
{
    rbusValue_FreeInternal(v);
    v->d.u16 = u16;
    v->type = RBUS_UINT16;
}

void rbusValue_SetInt32(rbusValue_t v, int32_t i32)
{
    rbusValue_FreeInternal(v);
    v->d.i32 = i32;
    v->type = RBUS_INT32;
}

void rbusValue_SetUInt32(rbusValue_t v, uint32_t u32)
{
    rbusValue_FreeInternal(v);
    v->d.u32 = u32;
    v->type = RBUS_UINT32;
}

void rbusValue_SetInt64(rbusValue_t v, int64_t i64)
{
    rbusValue_FreeInternal(v);
    v->d.i64 = i64;
    v->type = RBUS_INT64;
}

void rbusValue_SetUInt64(rbusValue_t v, uint64_t u64)
{
    rbusValue_FreeInternal(v);
    v->d.u64 = u64;
    v->type = RBUS_UINT64;
}

void rbusValue_SetSingle(rbusValue_t v, float f32)
{
    rbusValue_FreeInternal(v);
    v->d.f32 = f32;
    v->type = RBUS_SINGLE;
}

void rbusValue_SetDouble(rbusValue_t v, double f64)
{
    rbusValue_FreeInternal(v);
    v->d.f64 = f64;
    v->type = RBUS_DOUBLE;
}

void rbusValue_SetTime(rbusValue_t v, rbusDateTime_t* tv)
{
    rbusValue_FreeInternal(v);
    v->d.tv = *tv;
    v->type = RBUS_DATETIME;
}

void rbusValue_SetProperty(rbusValue_t v, struct _rbusProperty* property)
{
    rbusValue_FreeInternal(v);
    v->type = RBUS_PROPERTY;
    v->d.property = property;
    if(v->d.property)
        rbusProperty_Retain(v->d.property);
}

void rbusValue_SetObject(rbusValue_t v, struct _rbusObject* object)
{
    rbusValue_FreeInternal(v);
    v->type = RBUS_OBJECT;
    v->d.object = object;
    if(v->d.object)
        rbusObject_Retain(v->d.object);
}

uint8_t const* rbusValue_GetV(rbusValue_t v)
{
    switch(v->type)
    {
    case RBUS_STRING:
    case RBUS_BYTES:
        return v->d.bytes->data;
    default:
        return (uint8_t const*)&v->d.b;
    }
}

uint32_t rbusValue_GetL(rbusValue_t v)
{
    switch(v->type)
    {
    case RBUS_STRING:       return v->d.bytes->posWrite; 
    case RBUS_BOOLEAN:      return sizeof(bool);
    case RBUS_INT32:        return sizeof(int32_t);
    case RBUS_UINT32:       return sizeof(uint32_t);
    case RBUS_CHAR:         return sizeof(char);
    case RBUS_BYTE:        return sizeof(unsigned char);
    case RBUS_INT8:         return sizeof(int8_t);
    case RBUS_UINT8:        return sizeof(uint8_t);
    case RBUS_INT16:        return sizeof(int16_t);
    case RBUS_UINT16:       return sizeof(uint16_t);
    case RBUS_INT64:        return sizeof(int64_t);
    case RBUS_UINT64:       return sizeof(uint64_t);
    case RBUS_SINGLE:       return sizeof(float);
    case RBUS_DOUBLE:       return sizeof(double);
    case RBUS_DATETIME:     return sizeof(rbusDateTime_t);
    case RBUS_BYTES:        return v->d.bytes->posWrite;
    default:                return 0;
    }
}

void rbusValue_SetTLV(rbusValue_t v, rbusValueType_t type, uint32_t length, void const* value)
{
    switch(type)
    {
    /*Calling rbusValue_SetString/rbusValue_SetBuffer so the value's internal buffer is created.*/
    case RBUS_STRING:
        rbusValue_SetString(v, (char const*)value);
        break;
    case RBUS_BYTES:
        rbusValue_SetBytes(v, value, length);
        break;
    default:
        rbusValue_FreeInternal(v);
        v->type = type;
        memcpy(&v->d.b, value, length);
        break;
    }
    assert(rbusValue_GetL(v) == length);
}

int rbusValue_Decode(rbusValue_t* value, rbusBuffer_t const buff)
{
    uint16_t    type;
    uint16_t    length;
    rbusValue_t current;

    rbusValue_Init(value);

    current = *value;

    // read value
    rbusBuffer_ReadUInt16(buff, &type);
    rbusBuffer_ReadUInt16(buff, &length);
    current->type = type;
    switch(type)
    {
    /*Calling rbusValue_SetString/rbusValue_SetBytes so the value's internal buffer is created.*/
    case RBUS_STRING:
        if(!(buff->posRead + length <= buff->lenAlloc))
        {
            RBUSLOG_WARN("rbusValue_Decode failed");
            return -1;
        }
        assert(strlen((char const*)buff->data + buff->posRead) + 1 == (size_t)length);/*length should captures null term*/
        rbusValue_SetString(current, (char const*)buff->data + buff->posRead);
        buff->posRead += length;
        return length;
    case RBUS_BYTES:
        if(!(buff->posRead + length <= buff->lenAlloc))
        {
            RBUSLOG_WARN("rbusValue_Decode failed");
            return -1;
        }
        rbusValue_SetBytes(current, buff->data + buff->posRead, length);
        buff->posRead += length;
        return length;
    /* For the other types, its ok to read directly into them and set the type below */
    case RBUS_BOOLEAN:
        return rbusBuffer_ReadBoolean(buff, &current->d.b);
    case RBUS_INT32:
        return rbusBuffer_ReadInt32(buff, &current->d.i32);
    case RBUS_UINT32:
        return rbusBuffer_ReadUInt32(buff, &current->d.u32);
    case RBUS_CHAR:
        return rbusBuffer_ReadChar(buff, &current->d.c);
    case RBUS_BYTE:
        return rbusBuffer_ReadByte(buff, &current->d.u);
    case RBUS_INT8:
        return rbusBuffer_ReadInt8(buff, &current->d.i8);
    case RBUS_UINT8:
        return rbusBuffer_ReadUInt8(buff, &current->d.u8);
    case RBUS_INT16:
        return rbusBuffer_ReadInt16(buff, &current->d.i16);
    case RBUS_UINT16:
        return rbusBuffer_ReadUInt16(buff, &current->d.u16);
    case RBUS_INT64:
        return rbusBuffer_ReadInt64(buff, &current->d.i64);
    case RBUS_UINT64:
        return rbusBuffer_ReadUInt64(buff, &current->d.u64);
    case RBUS_SINGLE:
        return rbusBuffer_ReadSingle(buff, &current->d.f32);
    case RBUS_DOUBLE:
        return rbusBuffer_ReadDouble(buff, &current->d.f64);
    case RBUS_DATETIME:
        return rbusBuffer_ReadDateTime(buff, &current->d.tv);
    default:
        assert(false);
        return -1;
    }
}

void rbusValue_Encode(rbusValue_t value, rbusBuffer_t buff)
{
    // encode value
    switch(value->type)
    {
    case RBUS_STRING:/*length should include null term*/
        assert(value->d.bytes->data);
        assert(value->d.bytes->posWrite <= value->d.bytes->lenAlloc);
        assert(value->d.bytes->posRead == 0);
        assert(strlen((char const*)value->d.bytes->data)+1 == (size_t)value->d.bytes->posWrite);
        rbusBuffer_WriteStringTLV(buff, (char const*)value->d.bytes->data, value->d.bytes->posWrite);
        break;
    case RBUS_BYTES:
        assert(value->d.bytes->data);
        assert(value->d.bytes->posWrite <= value->d.bytes->lenAlloc);
        assert(value->d.bytes->posRead == 0);
        rbusBuffer_WriteBytesTLV(buff, value->d.bytes->data, value->d.bytes->posWrite);
        break;
    case RBUS_BOOLEAN:
        rbusBuffer_WriteBooleanTLV(buff, value->d.b);
        break;
    case RBUS_INT32:
        rbusBuffer_WriteInt32TLV(buff, value->d.i32);
        break;
    case RBUS_UINT32:
        rbusBuffer_WriteUInt32TLV(buff, value->d.u32);
        break;
    case RBUS_CHAR:
        rbusBuffer_WriteCharTLV(buff, value->d.c);
        break;
    case RBUS_BYTE:
        rbusBuffer_WriteByteTLV(buff, value->d.u);
        break;
    case RBUS_INT8:
        rbusBuffer_WriteInt8TLV(buff, value->d.i8);
        break;
    case RBUS_UINT8:
        rbusBuffer_WriteUInt8TLV(buff, value->d.u8);
        break;
    case RBUS_INT16:
        rbusBuffer_WriteInt16TLV(buff, value->d.i16);
        break;
    case RBUS_UINT16:
        rbusBuffer_WriteUInt16TLV(buff, value->d.u16);
        break;
    case RBUS_INT64:
        rbusBuffer_WriteInt64TLV(buff, value->d.i64);
        break;
    case RBUS_UINT64:
        rbusBuffer_WriteUInt64TLV(buff, value->d.u64);
        break;
    case RBUS_SINGLE:
        rbusBuffer_WriteSingleTLV(buff, value->d.f32);
        break;
    case RBUS_DOUBLE:
        rbusBuffer_WriteDoubleTLV(buff, value->d.f64);
        break;
    case RBUS_DATETIME:
        rbusBuffer_WriteDateTimeTLV(buff, &value->d.tv);
        break;
    default:
        assert(false);
        break;
    }
}

static double rbusValue_CoerceNumericToDouble(rbusValue_t v)
{
    switch(v->type)
    {
    case RBUS_INT32:
        return (double)rbusValue_GetInt32(v);
    case RBUS_UINT32:
        return (double)rbusValue_GetUInt32(v);
    case RBUS_BOOLEAN:
        return (double)rbusValue_GetBoolean(v);
    case RBUS_CHAR:
        return (double)rbusValue_GetChar(v);
    case RBUS_BYTE:
        return (double)rbusValue_GetByte(v);
    case RBUS_INT8:
        return (double)rbusValue_GetInt8(v);
    case RBUS_UINT8:
        return (double)rbusValue_GetUInt8(v);
    case RBUS_INT16:
        return (double)rbusValue_GetInt16(v);
    case RBUS_UINT16:
        return (double)rbusValue_GetUInt16(v);
    case RBUS_INT64:
        return (double)rbusValue_GetInt64(v);
    case RBUS_UINT64:
        return (double)rbusValue_GetUInt64(v);
    case RBUS_SINGLE:
        return (double)rbusValue_GetSingle(v);
    default:
        return rbusValue_GetDouble(v);
    }
}

int rbusValue_Compare(rbusValue_t v1, rbusValue_t v2)
{
    if(v1 == v2)
        return 0;

    /*compare integral values being type insensitive*/
    if(v1->type <= RBUS_DOUBLE && v2->type <= RBUS_DOUBLE)
    {
        double d1 = rbusValue_CoerceNumericToDouble(v1);
        double d2 = rbusValue_CoerceNumericToDouble(v2);

        if(d1 == d2)
            return 0;
        else if(d1 < d2)
            return -1;
        else
            return 1;
    }

    if(v1->type != v2->type)
    {
        if(v1->type < v2->type)
            return -1;
        else
            return 1;
    }

    switch(v1->type)
    {
    case RBUS_STRING:
    {
        return strcmp((char const*)v1->d.bytes->data, (char const*)v2->d.bytes->data);
    }
    case RBUS_BYTES:
    {
        int c = memcmp(v1->d.bytes->data, v2->d.bytes->data, v1->d.bytes->posWrite);
        if(v1->d.bytes->posWrite < v2->d.bytes->posWrite && c == 0)
            c = -1;
        else if(v1->d.bytes->posWrite > v2->d.bytes->posWrite && c == 0)
            c = 1;
        return c;
    }
    case RBUS_DATETIME:
    {
        if(memcmp(rbusValue_GetTime(v1), rbusValue_GetTime(v2), sizeof(rbusDateTime_t)))
            return 0;

        /*apply timezone and diff the times*/
        rbusDateTime_t dt1 = *rbusValue_GetTime(v1);
        rbusDateTime_t dt2 = *rbusValue_GetTime(v2);

        dt1.m_time.tm_hour += (dt1.m_tz.m_isWest ? dt1.m_tz.m_tzhour * -1 : dt1.m_tz.m_tzhour);
        dt1.m_time.tm_min += (dt1.m_tz.m_isWest ? dt1.m_tz.m_tzmin * -1 : dt1.m_tz.m_tzmin);
        if(dt1.m_time.tm_min < 0)
            dt1.m_time.tm_min += 60;
        if(dt1.m_time.tm_min >= 60)
            dt1.m_time.tm_min -= 60;

        dt2.m_time.tm_hour += (dt2.m_tz.m_isWest ? dt2.m_tz.m_tzhour * -1 : dt2.m_tz.m_tzhour);
        dt2.m_time.tm_min += (dt2.m_tz.m_isWest ? dt2.m_tz.m_tzmin * -1 : dt2.m_tz.m_tzmin);
        if(dt2.m_time.tm_min < 0)
            dt2.m_time.tm_min += 60;
        if(dt2.m_time.tm_min >= 60)
            dt2.m_time.tm_min -= 60;

        time_t t1 = mktime(&dt1.m_time);
        time_t t2 = mktime(&dt2.m_time);
        double diffSecs = difftime(t1, t2);

        if(diffSecs == 0)
            return 0;
        else if(diffSecs < 0)
            return -1;
        else
            return 1;
    }
    case RBUS_PROPERTY:
    {
        if(rbusValue_GetProperty(v1) && rbusValue_GetProperty(v2))
            return rbusProperty_Compare(rbusValue_GetProperty(v1), rbusValue_GetProperty(v2));
        else if(rbusValue_GetProperty(v1))
            return 1;
        else
            return -1;
    }
    case RBUS_OBJECT:
    {
        if(rbusValue_GetObject(v1) && rbusValue_GetObject(v2))
            return rbusObject_Compare(rbusValue_GetObject(v1), rbusValue_GetObject(v2), true);
        else if(rbusValue_GetObject(v1))
            return 1;
        else
            return -1;
    }
    case RBUS_NONE:
    default:
        return 0;
    }
}

void rbusValue_SetPointer(rbusValue_t* value1, rbusValue_t value2)
{
    rbusValue_Release(*value1);
    *value1 = value2;
    rbusValue_Retain(*value1);
}

void rbusValue_Swap(rbusValue_t* v1, rbusValue_t* v2)
{
    rbusValue_t tmp;
    tmp = *v1;
    *v1 = *v2;
    *v2 = tmp;
}

void rbusValue_Copy(rbusValue_t dest, rbusValue_t source)
{
    if(dest == source)
    {
        RBUSLOG_INFO("%s: dest and source are the same", __FUNCTION__);
        return;
    }

    rbusValue_FreeInternal(dest);

    switch(source->type)
    {
    case RBUS_STRING:
        rbusValue_SetString(dest, rbusValue_GetString(source, NULL));
        break;
    case RBUS_BYTES:
        {
        int len;
        uint8_t const* bytes = rbusValue_GetBytes(source, &len);
        rbusValue_SetBytes(dest, bytes, len);
        }
        break;
    case RBUS_PROPERTY:
    case RBUS_OBJECT:
        RBUSLOG_INFO("%s PROPERTY/OBJECT NOT SUPPORTED YET", __FUNCTION__); /* TODO */
        break;
    default:
        dest->type = source->type;
        memcpy(&dest->d, &source->d, sizeof(dest->d));
        break;
    }

    assert(rbusValue_Compare(dest, source)==0);
}

bool rbusValue_SetFromString(rbusValue_t value, rbusValueType_t type, const char* pStringInput)
{
    bool tmpB = false;
    char tmpC = 0;
    unsigned char tmpUC = 0;
    float tmpF = 0.0f;
    double tmpD = 0.0f;
    long tmpL = 0;
    unsigned long tmpUL = 0;
    long long tmpLL = 0;
    unsigned long long tmpULL = 0;
    errno = 0;
    char *endptr = NULL;
    char sign = *pStringInput;
    unsigned int tmp_strlen = 0;

    if (pStringInput == NULL)
        return false;

    switch(type)
    {
    case RBUS_STRING:
        rbusValue_SetString(value, pStringInput);
        break;
    case RBUS_BYTES:
        tmp_strlen= strlen(pStringInput);
        rbusValue_SetBytes(value,(uint8_t const*)pStringInput,tmp_strlen);
        break;
    case RBUS_BOOLEAN:
        tmp_strlen = strlen(pStringInput);
        if (((0 == strncasecmp("true", pStringInput, 4)) && (tmp_strlen == 4)) || ((0 == strncasecmp("1", pStringInput, 1)) && (tmp_strlen == 1)))
            tmpB = true;
        else if (((0 == strncasecmp("false", pStringInput, 5)) && (tmp_strlen == 5)) || ((0 == strncasecmp("0", pStringInput, 1)) && (tmp_strlen == 1)))
            tmpB = false;
        else
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        rbusValue_SetBoolean(value, tmpB);
        break;
    case RBUS_CHAR:
        sscanf(pStringInput, "%c",&tmpC);
        rbusValue_SetChar(value, tmpC);
        break;
    case RBUS_BYTE:
        sscanf(pStringInput, "%c",&tmpUC);
        rbusValue_SetByte(value, tmpUC);
        break;
    case RBUS_INT8:
    {
        tmpL = strtol (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (errno == ERANGE) || (tmpL < INT8_MIN  || tmpL > INT8_MAX))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        rbusValue_SetInt8(value, (int8_t)tmpL);
        break;
    }
    case RBUS_UINT8:
    {
        tmpUL = strtoul (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (sign == '-' && tmpUL != 0) || (errno == ERANGE) || (tmpUL > UINT8_MAX))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        rbusValue_SetUInt8(value, (uint8_t)tmpUL);
        break;
    }
    case RBUS_INT16:
        tmpL = strtol (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (errno == ERANGE && (tmpL == LONG_MAX || tmpL == LONG_MIN)) || (tmpL > INT16_MAX) || (tmpL < INT16_MIN))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else if ((tmpL <= INT16_MAX) && (tmpL >= INT16_MIN))
        {
            int16_t tmpI16 = (int16_t) tmpL;
            rbusValue_SetInt16(value, tmpI16);
        }
        break;
    case RBUS_INT32:
        tmpL = strtol (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (errno == ERANGE && (tmpL == LONG_MAX || tmpL == LONG_MIN)) || (tmpL > INT32_MAX) || (tmpL < INT32_MIN))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else if ((tmpL <= INT32_MAX) && (tmpL >= INT32_MIN))
        {
            int32_t tmpI32 = (int32_t) tmpL;
            rbusValue_SetInt32(value, tmpI32);
        }
        break;
    case RBUS_INT64:
        tmpLL = strtoll (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (errno == ERANGE && (tmpLL == LLONG_MAX || tmpLL == LLONG_MIN)) || (tmpLL > INT64_MAX) || (tmpLL < INT64_MIN))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else if ((tmpLL <= INT64_MAX) && (tmpLL >= INT64_MIN))
        {
            int64_t tmpI64 = (int64_t) tmpLL;
            rbusValue_SetInt64(value, tmpI64);
        }
        break;
    case RBUS_UINT16:
        tmpUL = strtoul (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (sign == '-' && tmpUL != 0) || (errno == ERANGE && (tmpUL == ULONG_MAX || tmpUL == 0)) || (tmpUL > UINT16_MAX))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else
        {
            uint16_t tmpU16 = (uint16_t) tmpUL;
            rbusValue_SetUInt16(value, tmpU16);
            break;
        }
    case RBUS_UINT32:
        tmpUL = strtoul (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (sign == '-' && tmpUL != 0) || (errno == ERANGE && (tmpUL == ULONG_MAX || tmpUL == 0)) || (tmpUL > UINT32_MAX))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else
        {
            uint32_t tmpU32 = (uint32_t) tmpUL;
            rbusValue_SetUInt32(value, tmpU32);
            break;
        }
    case RBUS_UINT64:
        tmpULL = strtoull (pStringInput, &endptr, 0);
        if ((pStringInput == endptr) || (*endptr != 0) || (sign == '-' && tmpULL != 0) || (errno == ERANGE && (tmpULL == ULLONG_MAX || tmpULL == 0)) || (tmpULL > UINT64_MAX))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else
        {
            uint64_t tmpU64 = (uint64_t) tmpULL;
            rbusValue_SetUInt64(value, tmpU64);
            break;
        }
    case RBUS_SINGLE:
        tmpF = strtof(pStringInput, &endptr);
        if ((pStringInput == endptr) || (*endptr != 0) || (errno == ERANGE))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else
        {
            rbusValue_SetSingle(value, tmpF);
            break;
        }
    case RBUS_DOUBLE:
        tmpD = strtod(pStringInput, &endptr);
        if ((pStringInput == endptr) || (*endptr != 0) || (errno == ERANGE))
        {
            RBUSLOG_INFO ("Invalid input string");
            return false;
        }
        else
        {
            rbusValue_SetDouble(value, tmpD);
            break;
        }
    case RBUS_DATETIME:
        {
            rbusDateTime_t tv = {{0},{0}};
            if(0 != strncmp(pStringInput,"0000-",5)) {
                char *pRet = NULL;
                if(strstr(pStringInput,"T"))
                    pRet=(char *)strptime(pStringInput, "%Y-%m-%dT%H:%M:%S", &(tv.m_time));
                else
                    pRet=(char *)strptime(pStringInput, "%Y-%m-%d %H:%M:%S", &(tv.m_time));
                if(!pRet) {
                    RBUSLOG_INFO ("Invalid input string ");
                    return false;
                }
                if((RBUS_TIMEZONE_LEN == strlen(pRet)) &&
                        (isdigit((int)pRet[1]) &&
                         isdigit((int)pRet[2]) &&
                         isdigit((int)pRet[4]) &&
                         isdigit((int)pRet[5]))
                  ) {
                    tv.m_tz.m_isWest = ('-' == pRet[0]);
                    sscanf(pRet+1,"%02d:%02d",&(tv.m_tz.m_tzhour), &(tv.m_tz.m_tzmin));
                }
            }
            rbusValue_SetTime(value, &tv);
        }
        break;
    case RBUS_PROPERTY:
    case RBUS_OBJECT:
    default:
        return false;
    }
    return true;
}

void rbusValue_fwrite(rbusValue_t value, int depth, FILE* fout)
{
    rbusValueType_t type;

    if(!value)
        return;

    type = rbusValue_GetType(value);

    if(type == RBUS_OBJECT)
    {
        rbusObject_fwrite(rbusValue_GetObject(value), depth, fout);
    }
    else if(type == RBUS_PROPERTY)
    {
        rbusProperty_fwrite(rbusValue_GetProperty(value), depth, fout);
    }
    else
    {
        int i;
        char* s = rbusValue_ToDebugString(value, NULL, 0);
        for(i=0; i<depth; ++i)
            fprintf(fout, " ");
        fprintf(fout, "%s", s);
        free(s);
    }
}
