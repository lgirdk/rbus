#include "rbus_config.h"
#include "rbus_log.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* These RBUS_* defines are the list of config settings with their default values
 * Each can be overridden using an environment variable of the same name
 */
#define RBUS_TMP_DIRECTORY      "/tmp"      /*temp directory where persistent data can be stored*/
#define RBUS_SUBSCRIBE_TIMEOUT   600000     /*subscribe retry timeout in miliseconds*/
#define RBUS_SUBSCRIBE_MAXWAIT   60000      /*subscribe retry max wait between retries in miliseconds*/
#define RBUS_VALUECHANGE_PERIOD  2000       /*polling period for valuechange detector*/
#define RBUS_GET_DEFAULT_TIMEOUT 60000     /* default timeout in miliseconds for GET API */
#define RBUS_SET_DEFAULT_TIMEOUT 60000      /* default timeout in miliseconds for SET API */
#define RBUS_GET_TIMEOUT_OVERRIDE "/tmp/rbus_timeout_get"
#define RBUS_SET_TIMEOUT_OVERRIDE "/tmp/rbus_timeout_set"

#define initStr(P,N) \
{ \
    char* V = getenv(#N); \
    P=strdup((V && strlen(V)) ? V : N); \
    RBUSLOG_DEBUG(#N"=%s",P); \
}

#define initInt(P,N) \
{ \
    char* V = getenv(#N); \
    P=((V && strlen(V)) ? atoi(V) : N); \
    RBUSLOG_DEBUG(#N"=%d",P); \
}

static rbusConfig_t* gConfig = NULL;

void rbusConfig_CreateOnce()
{
    if(gConfig)
        return;

    RBUSLOG_DEBUG("%s", __FUNCTION__);

    gConfig = malloc(sizeof(struct _rbusConfig_t));

    initStr(gConfig->tmpDir,                RBUS_TMP_DIRECTORY);
    initInt(gConfig->subscribeTimeout,      RBUS_SUBSCRIBE_TIMEOUT);
    initInt(gConfig->subscribeMaxWait,      RBUS_SUBSCRIBE_MAXWAIT);
    initInt(gConfig->valueChangePeriod,     RBUS_VALUECHANGE_PERIOD);
    initInt(gConfig->getTimeout,            RBUS_GET_DEFAULT_TIMEOUT);
    initInt(gConfig->setTimeout,            RBUS_SET_DEFAULT_TIMEOUT);
}

void rbusConfig_Destroy()
{
    if(!gConfig)
        return;

    RBUSLOG_DEBUG("%s", __FUNCTION__);

    free(gConfig->tmpDir);
    free(gConfig);
    gConfig = NULL;
}

rbusConfig_t* rbusConfig_Get()
{
    return gConfig;
}

int rbusConfig_ReadGetTimeout()
{
    int timeout = 0;
    FILE *fp = NULL;
    char buf[25] = {0};

    if (access(RBUS_GET_TIMEOUT_OVERRIDE, F_OK) == 0)
    {
        fp = fopen(RBUS_GET_TIMEOUT_OVERRIDE, "r");
        if(fp != NULL) {
            fread(buf, 1, sizeof(buf), fp);
            timeout = atoi(buf);
            fclose(fp);
        }
        if (timeout > 0)
            return timeout * 1000;
    }

    return gConfig->getTimeout;
}

int rbusConfig_ReadSetTimeout()
{
    int timeout = 0;
    FILE *fp = NULL;
    char buf[25] = {0};

    if (access(RBUS_SET_TIMEOUT_OVERRIDE, F_OK) == 0)
    {
        fp = fopen(RBUS_SET_TIMEOUT_OVERRIDE, "r");
        if(fp != NULL) {
            fread(buf, 1, sizeof(buf), fp);
            timeout = atoi(buf);
            fclose(fp);
        }
        if (timeout > 0)
            return timeout * 1000;
    }

    return gConfig->setTimeout;
}
