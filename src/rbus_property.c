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
#include <string.h>
#include <stdlib.h>
#include <rtmessage/rtRetainable.h>

struct _rbusProperty
{
    rtRetainable retainable;
    char* name;
    rbusValue_t value;
    struct _rbusProperty* next;
};

void rbusProperty_Init(rbusProperty_t* p, char const* name, rbusValue_t value)
{
    (*p) = malloc(sizeof(struct _rbusProperty));

    if(name)
        (*p)->name = strdup(name);
    else
        (*p)->name = NULL;

    (*p)->next = NULL;

    (*p)->value = NULL;
    if(value)
        rbusProperty_SetValue((*p), value);

    (*p)->retainable.refCount = 1;
}

void rbusProperty_Destroy(rtRetainable* r)
{
    rbusProperty_t property = (rbusProperty_t)r;

    if(property->name)
    {
        free(property->name);
        property->name = NULL;
    }

    rbusValue_Release(property->value);
    if(property->next)
    {
        rbusProperty_Release(property->next);
        property->next = NULL;
    }

    free(property);
}

void rbusProperty_Retain(rbusProperty_t property)
{
    rtRetainable_retain(property);
}

void rbusProperty_Release(rbusProperty_t property)
{
    rtRetainable_release(property, rbusProperty_Destroy);
}

void rbusProperty_fwrite(rbusProperty_t prop, int depth, FILE* fout)
{
    int i;
    for(i=0; i<depth; ++i)
        fprintf(fout, " ");
    fprintf(fout, "rbusProperty name=%s\n", rbusProperty_GetName(prop));
    rbusValue_fwrite(rbusProperty_GetValue(prop), depth+1, fout);
    fprintf(fout,"\n");
    prop = rbusProperty_GetNext(prop);

    if(prop)
        rbusProperty_fwrite(prop, depth, fout);
}

#if 0
void rbusProperty_Copy(rbusProperty_t destination, rbusProperty_t source)
{
    if(destination->name)
    {
        free(destination->name);
        destination->name = NULL;
    }

    if(source->name)
        destination->name = strdup(source->name);

    rbusValue_Copy(destination->value, source->value);

    if(source->next)
    {
        rbusProperty_SetNext(destination, source->next);
    }
}
#endif

int rbusProperty_Compare(rbusProperty_t property1, rbusProperty_t property2)
{
    int rc;

    if(property1 == property2)
        return 0;

    rc = strcmp(property1->name, property2->name);
    if(rc)
        return rc;/*return strcmp result so we can use rbusProperty_Compare to sort properties by name*/

    return rbusValue_Compare(property1->value, property2->value);
}

char const* rbusProperty_GetName(rbusProperty_t property)
{
    return property->name;
}

void rbusProperty_SetName(rbusProperty_t property, char const* name)
{
    if(property->name)
        free(property->name);
    if(name)
        property->name = strdup(name);
    else
        property->name = NULL;
}

rbusValue_t rbusProperty_GetValue(rbusProperty_t property)
{
    return property->value;
}

void rbusProperty_SetValue(rbusProperty_t property, rbusValue_t value)
{
    if(property->value)
        rbusValue_Release(property->value);
    property->value = value;
    if(property->value)
        rbusValue_Retain(property->value);
}

rbusProperty_t rbusProperty_GetNext(rbusProperty_t property)
{
    return property->next;
}

void rbusProperty_SetNext(rbusProperty_t property, rbusProperty_t next)
{
    if(property->next)
        rbusProperty_Release(property->next);
    property->next = next;
    if(property->next)
        rbusProperty_Retain(property->next);
}

void rbusProperty_PushBack(rbusProperty_t property, rbusProperty_t back)
{
    rbusProperty_t last = property;
    while(last->next)
        last = last->next;
    rbusProperty_SetNext(last, back);
}

uint32_t rbusProperty_Count(rbusProperty_t property)
{
    uint32_t count = 1;
    rbusProperty_t prop = property;
    while(prop->next)
    {
        count++;
        prop = prop->next;
    }
    return count;
}

