/*******************************************************************************
* File Name: debug.c
*
* Version 1.0
*
* Description:
*  Common BLE application code for printout debug messages.
*
* Hardware Dependency:
*  CY8CKIT-042 BLE
*
********************************************************************************
* Copyright 2016, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#include "main.h"






/*******************************************************************************
* Function Name: PrintStackVersion
********************************************************************************
*
* Summary:
*   Prints the BLE Stack version if the PRINT_STACK_VERSION is defined.
*
* Parameters:
*   None.
*
* Return:
*   None.
*
*******************************************************************************/
void PrintStackVersion(void)
{
    CYBLE_STACK_LIB_VERSION_T stackVersion;
    
    apiResult = CyBle_GetStackLibraryVersion(&stackVersion);
    if(apiResult != CYBLE_ERROR_OK)
    {
        DBG_PRINTF("CyBle_GetStackLibraryVersion API Error: ");
        PrintApiResult();
    }
    else
    {
        DBG_PRINTF("BLE Stack Version: %d.%d.%d.%d\r\n", stackVersion.majorVersion, 
                                                         stackVersion.minorVersion,
                                                         stackVersion.patch,
                                                         stackVersion.buildNumber);
    }
}

/*******************************************************************************
* Function Name: PrintState
********************************************************************************
*
* Summary:
*   Decodes and prints the cyBle_state global variable value.
*
* Parameters:
*   None.
*
* Return:
*   None.
*
*******************************************************************************/
void PrintState(void)
{
    DBG_PRINTF("state: ");
    switch(CyBle_GetState())
    {
        case CYBLE_STATE_STOPPED:
            DBG_PRINTF("stopped\r\n");
            break;
            
        case CYBLE_STATE_INITIALIZING:
            DBG_PRINTF("initializing\r\n");
            break;
            
        case CYBLE_STATE_CONNECTED:
            DBG_PRINTF("connected\r\n");
            break;
            
        case CYBLE_STATE_DISCONNECTED:
            DBG_PRINTF("disconnected\r\n");
            break;
            
        case CYBLE_STATE_ADVERTISING:
            DBG_PRINTF("advertising\r\n");
            break;

        default:
            DBG_PRINTF("unknown\r\n");
            break;
    }
}


/*******************************************************************************
* Function Name: PrintApiResult
********************************************************************************
*
* Summary:
*   Decodes and prints the apiResult global variable value.
*
* Parameters:
*   None.
*
* Return:
*   None.
*
*******************************************************************************/
void PrintApiResult(void)
{
    DBG_PRINTF("0x%2.2x ", apiResult);
    
    switch(apiResult)
    {
        case CYBLE_ERROR_OK:
            DBG_PRINTF("ok\r\n");
            break;
            
        case CYBLE_ERROR_INVALID_PARAMETER:
            DBG_PRINTF("invalid parameter\r\n");
            break;
            
        case CYBLE_ERROR_INVALID_OPERATION:
            DBG_PRINTF("invalid operation\r\n");
            break;
            
        case CYBLE_ERROR_NO_DEVICE_ENTITY:
            DBG_PRINTF("no device entity\r\n");
            break;
            
        case CYBLE_ERROR_NTF_DISABLED:
            DBG_PRINTF("notification is disabled\r\n");
            break;
            
        case CYBLE_ERROR_IND_DISABLED:
            DBG_PRINTF("indication is disabled\r\n");
            break;
            
        case CYBLE_ERROR_CHAR_IS_NOT_DISCOVERED:
            DBG_PRINTF("characteristic is not discovered\r\n");
            break;
            
        case CYBLE_ERROR_INVALID_STATE:
            DBG_PRINTF("invalid state: ");
            PrintState();
            break;
            
        case CYBLE_ERROR_GATT_DB_INVALID_ATTR_HANDLE:
            DBG_PRINTF("invalid attribute handle\r\n");
            break;
            
        case CYBLE_ERROR_FLASH_WRITE_NOT_PERMITED:
            DBG_PRINTF("flash write not permitted\r\n");
            break;
            
        default:
            DBG_PRINTF("other api result\r\n");
            break;
    }
}


/* [] END OF FILE */
