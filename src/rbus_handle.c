#include "rbus_handle.h"
#include <string.h>

static rtVector gHandleList = NULL;

#define VERIFY_NULL(T,R) if(NULL == T){ RBUSLOG_ERROR(#T" is NULL"); R; }

void rbusHandleList_Add(struct _rbusHandle* handle)
{
    VERIFY_NULL(handle,return);
    if(!gHandleList)
        rtVector_Create(&gHandleList);
    RBUSLOG_DEBUG("%s adding %p", __FUNCTION__, handle);
    rtVector_PushBack(gHandleList, handle);
}

void rbusHandleList_Remove(struct _rbusHandle* handle)
{
    VERIFY_NULL(handle,return);
    RBUSLOG_DEBUG("%s removing %p", __FUNCTION__, handle);
    rtVector_RemoveItem(gHandleList, handle, rtVector_Cleanup_Free);
    if(rtVector_Size(gHandleList) == 0)
    {
        rtVector_Destroy(gHandleList, NULL);
        gHandleList = NULL;
        return;
    }
}

bool rbusHandleList_IsEmpty()
{
    return gHandleList == NULL;
}

bool rbusHandleList_IsFull()
{
    RBUSLOG_DEBUG("%s size=%zu", __FUNCTION__, rtVector_Size(gHandleList));
    return (gHandleList && rtVector_Size(gHandleList) >= RBUS_MAX_HANDLES);
}

void rbusHandleList_ClientDisconnect(char const* clientListener)
{
    size_t i;
    size_t len;
    VERIFY_NULL(clientListener,return);
    if(gHandleList)/*this could theoretically be null if advisory event comes in between the time rbus_close calls 
                  rbusHandleList_Remove and rbus_unregisterClientDisconnectHandler*/
    {
        len = rtVector_Size(gHandleList);
        for(i = 0; i < len; i++)
        {
            struct _rbusHandle* handle = (struct _rbusHandle*)rtVector_At(gHandleList, i);
            if(handle->subscriptions)
            {
                /*assuming this doesn't reenter this api which could possibly deadlock*/
                rbusSubscriptions_handleClientDisconnect(handle, handle->subscriptions, clientListener);
            }
        }
    }
}

struct _rbusHandle* rbusHandleList_GetByComponentID(int32_t componentId)
{
    size_t i;
    size_t len;
    struct _rbusHandle* handle = NULL;
    if(gHandleList)
    {
        len = rtVector_Size(gHandleList);
        for(i = 0; i < len; i++)
        {
            handle = (struct _rbusHandle*)rtVector_At(gHandleList, i);
            if(handle->componentId == componentId)
            {
                break;
            }
        }
    }
    return handle;
}

struct _rbusHandle* rbusHandleList_GetByName(char const* componentName)
{
    size_t i;
    size_t len;
    struct _rbusHandle* handle = NULL;
    VERIFY_NULL(componentName,return NULL);
    if(gHandleList)
    {
        len = rtVector_Size(gHandleList);
        for(i = 0; i < len; i++)
        {
            struct _rbusHandle* tmp = (struct _rbusHandle*)rtVector_At(gHandleList, i);
            if(strcmp(tmp->componentName, componentName) == 0)
            {
                handle = tmp;
                break;
            }
        }
    }
    return handle;
}
