#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rbus.h>
#include <rtMemory.h>
#include "mta_hal_tr104.h"

rbusError_t mta_tr104_rbusGetHandler(rbusHandle_t handle, rbusProperty_t inProperty, rbusGetHandlerOptions_t* opt)
{
    (void)handle;
    (void)opt;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    int retParamSize = 1;
    const char *pParamNameList = NULL;
    char **pParamValueList = NULL;

    rbusValue_t value;
    rbusValue_Init(&value);

    pParamNameList = rbusProperty_GetName(inProperty);

    retParamSize = 1;

    /* Get Data From HAL and return in as rbusProperty_t */
    if(mta_hal_getTR104parameterValues((char**)&pParamNameList, &retParamSize, &pParamValueList) == 0 )
    {
        /* Tokenize the returns value list and set the tlv and pass it to inProperty */
        char *pTmp = NULL;
        char* saveptr = NULL;
        char *pStr = strdup (pParamValueList[0]);
        pTmp = strtok_r(pStr, ",", &saveptr);
        if (pTmp)
        {
            char* pType = strtok_r(NULL, ",", &saveptr);
            char* pValue = strtok_r(NULL, ",", &saveptr);
            if ((pType == NULL) || (pValue == NULL))
            {
                printf ("ccspMtaAgentTR104Hal: The pre-defined formatted string is not present\n");
                rc = RBUS_ERROR_INVALID_INPUT; 
            }

            if (rc == RBUS_ERROR_SUCCESS)
            {
                /* Parse the Value  */
                /* Get the Type */
                if(strcasecmp (pType, "boolean") == 0)
                {
                    rbusValue_SetFromString(value, RBUS_BOOLEAN, pValue);
                }
                else if(strcasecmp (pType, "int") == 0)
                {
                    rbusValue_SetFromString(value, RBUS_INT32, pValue);
                }
                else if(strcasecmp (pType, "unsignedint") == 0)
                {
                    rbusValue_SetFromString(value, RBUS_UINT32, pValue);
                }
                else if(strcasecmp (pType, "long") == 0)
                {
                    rbusValue_SetFromString(value, RBUS_INT64, pValue);
                }
                else if(strcasecmp (pType, "unsignedLong") == 0)
                {
                    rbusValue_SetFromString(value, RBUS_UINT64, pValue);
                }
                else if(strcasecmp (pType, "string") == 0)
                {
                    rbusValue_SetFromString(value, RBUS_STRING, pValue);
                }
                else
                {
                    printf ("ccspMtaAgentTR104Hal: Invalid format to send across..\n");
                    rc = RBUS_ERROR_INVALID_INPUT;
                }
                rbusProperty_SetValue(inProperty, value);
                rbusValue_Release(value);
            }
        }
        /* Free the memory */
        free (pStr);
        mta_hal_freeTR104parameterValues(pParamValueList, retParamSize);
    }
    else
    {
        printf ("ccspMtaAgentTR104Hal: Failed to communicate to hal.. \n");
        rc = RBUS_ERROR_DESTINATION_RESPONSE_FAILURE;
    }

    return rc;
}

rbusError_t mta_tr104_rbusSetHandler(rbusHandle_t handle, rbusProperty_t inProperty, rbusSetHandlerOptions_t* opts)
{
    (void)handle;
    (void)opts;
    int count = 1;
    const char * name = rbusProperty_GetName(inProperty);
    rbusValue_t value = rbusProperty_GetValue(inProperty);
    rbusValueType_t type = rbusValue_GetType(value);

    char *aParamDetail = NULL;

    /* Arrive at length and do malloc; for now 512; */
    aParamDetail = rt_calloc (1, 512);

    char* pStrValue = rbusValue_ToString(value, NULL, 0);
    /* Make a single string */
    if (type == RBUS_BOOLEAN)
    {
        if (rbusValue_GetBoolean(value) == true)
            sprintf (aParamDetail, "%s,%s,%s", name, "boolean", "true");
        else
            sprintf (aParamDetail, "%s,%s,%s", name, "boolean", "false");
    }
    else if (type == RBUS_INT32)
    {
        sprintf (aParamDetail, "%s,%s,%s", name, "int", pStrValue);
    }
    else if (type == RBUS_UINT32)
    {
        sprintf (aParamDetail, "%s,%s,%s", name, "unsignedInt", pStrValue);
    }
    else if (type == RBUS_INT64)
    {
        sprintf (aParamDetail, "%s,%s,%s", name, "long", pStrValue);
    }
    else if (type == RBUS_UINT64)
    {
        sprintf (aParamDetail, "%s,%s,%s", name, "unsignedLong", pStrValue);
    }
    else if (type == RBUS_STRING)
    {
        sprintf (aParamDetail, "%s,%s,%s", name, "string", pStrValue);
    }
   
    /* Free the pStrValue */
    if (pStrValue)
        free (pStrValue);

    if (0 != mta_hal_setTR104parameterValues(&aParamDetail, &count))
    {
        printf ("ccspMtaAgentTR104Hal: Set Failed\n");
        return RBUS_ERROR_INVALID_INPUT;
    }

    printf ("ccspMtaAgentTR104Hal: Set Succsess\n");
    return RBUS_ERROR_SUCCESS;
}


int main()
{
    int i = 0;
    int halRet = 0;
    char **pParamList = NULL;
    int paramCount = 0;
    rbusError_t rc = RBUS_ERROR_SUCCESS;
    rbusHandle_t rbusHandle = NULL;
    rbusDataElement_t *pDataElements = NULL;

   /* Get the list of properties that are supported by MTA Hal */
    halRet = mta_hal_getTR104parameterNames(&pParamList, &paramCount);
    if (halRet != 0)
    {
        printf ("ccspMtaAgentTR104Hal: MTA Hal Returned Failure\n");
        return -1;
    }

    rc = rbus_open(&rbusHandle, "ccspMtaAgentTR104Hal");
    if(rc != RBUS_ERROR_SUCCESS)
    {
        printf("ccspMtaAgentTR104Hal: rbus_open failed: %d\n", rc);
        return -1;
    }
 
    pDataElements = (rbusDataElement_t*)calloc(paramCount, sizeof(rbusDataElement_t));

    while (i < paramCount)
    {
        pDataElements[i].name = pParamList[i];
        pDataElements[i].type = RBUS_ELEMENT_TYPE_PROPERTY;
        pDataElements[i].cbTable.getHandler = mta_tr104_rbusGetHandler;
        pDataElements[i].cbTable.setHandler = mta_tr104_rbusSetHandler;
        i++;
    }
    
    rc = rbus_regDataElements(rbusHandle, paramCount, pDataElements);
    if(rc != RBUS_ERROR_SUCCESS)
    {
        printf("ccspMtaAgentTR104Hal: rbus_regDataElements failed: %d\n", rc);
        rbus_close(rbusHandle);
        return -1;
    }

    while(1)
        sleep(1);

    rbus_unregDataElements(rbusHandle, paramCount, pDataElements);
    rbus_close(rbusHandle);

    mta_hal_getTR104parameterNames(&pParamList, &paramCount);
    return 0;
}
