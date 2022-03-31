#ifndef RBUS_HANDLE_H
#define RBUS_HANDLE_H

#include "rbus_element.h"
#include "rbus_subscriptions.h"
#include <rtConnection.h>
#include <rtVector.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
  RBUS_MAX_HANDLES 16

  rtConnection has 64 listener limit.
  16 allows:
    1 inbox (registered on first rbus_open)
    1 advisory (also registered on the first rbus_open)
    16 component names (registered on each rbus_open call)
    46 additional listeners which can be used by the rbus_message api, other rtConnection clients or for rbus future requirements
*/
#define RBUS_MAX_HANDLES 16

struct _rbusHandle
{
  char*                 componentName;
  int32_t               componentId;
  elementNode*          elementRoot;

  /* consumer side subscriptions FIXME - 
    this needs to be an associative map instead of list/vector*/
  rtVector              eventSubs; 

  /* provider side subscriptions */
  rbusSubscriptions_t   subscriptions; 

  rtVector              messageCallbacks;
  rtConnection          connection;
};

void rbusHandleList_Add(struct _rbusHandle* handle);
void rbusHandleList_Remove(struct _rbusHandle* handle);
bool rbusHandleList_IsEmpty();
bool rbusHandleList_IsFull();
void rbusHandleList_ClientDisconnect(char const* clientListener);
struct _rbusHandle* rbusHandleList_GetByComponentID(int32_t componentId);
struct _rbusHandle* rbusHandleList_GetByName(char const* componentName);

#ifdef __cplusplus
}
#endif
#endif