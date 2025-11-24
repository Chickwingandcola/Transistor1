/*******************************************************************************
* File Name: AMP2_EN.c  
* Version 2.20
*
* Description:
*  This file contains APIs to set up the Pins component for low power modes.
*
* Note:
*
********************************************************************************
* Copyright 2015, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions, 
* disclaimers, and limitations in the end user license agreement accompanying 
* the software package with which this file was provided.
*******************************************************************************/

#include "cytypes.h"
#include "AMP2_EN.h"

static AMP2_EN_BACKUP_STRUCT  AMP2_EN_backup = {0u, 0u, 0u};


/*******************************************************************************
* Function Name: AMP2_EN_Sleep
****************************************************************************//**
*
* \brief Stores the pin configuration and prepares the pin for entering chip 
*  deep-sleep/hibernate modes. This function applies only to SIO and USBIO pins.
*  It should not be called for GPIO or GPIO_OVT pins.
*
* <b>Note</b> This function is available in PSoC 4 only.
*
* \return 
*  None 
*  
* \sideeffect
*  For SIO pins, this function configures the pin input threshold to CMOS and
*  drive level to Vddio. This is needed for SIO pins when in device 
*  deep-sleep/hibernate modes.
*
* \funcusage
*  \snippet AMP2_EN_SUT.c usage_AMP2_EN_Sleep_Wakeup
*******************************************************************************/
void AMP2_EN_Sleep(void)
{
    #if defined(AMP2_EN__PC)
        AMP2_EN_backup.pcState = AMP2_EN_PC;
    #else
        #if (CY_PSOC4_4200L)
            /* Save the regulator state and put the PHY into suspend mode */
            AMP2_EN_backup.usbState = AMP2_EN_CR1_REG;
            AMP2_EN_USB_POWER_REG |= AMP2_EN_USBIO_ENTER_SLEEP;
            AMP2_EN_CR1_REG &= AMP2_EN_USBIO_CR1_OFF;
        #endif
    #endif
    #if defined(CYIPBLOCK_m0s8ioss_VERSION) && defined(AMP2_EN__SIO)
        AMP2_EN_backup.sioState = AMP2_EN_SIO_REG;
        /* SIO requires unregulated output buffer and single ended input buffer */
        AMP2_EN_SIO_REG &= (uint32)(~AMP2_EN_SIO_LPM_MASK);
    #endif  
}


/*******************************************************************************
* Function Name: AMP2_EN_Wakeup
****************************************************************************//**
*
* \brief Restores the pin configuration that was saved during Pin_Sleep(). This 
* function applies only to SIO and USBIO pins. It should not be called for
* GPIO or GPIO_OVT pins.
*
* For USBIO pins, the wakeup is only triggered for falling edge interrupts.
*
* <b>Note</b> This function is available in PSoC 4 only.
*
* \return 
*  None
*  
* \funcusage
*  Refer to AMP2_EN_Sleep() for an example usage.
*******************************************************************************/
void AMP2_EN_Wakeup(void)
{
    #if defined(AMP2_EN__PC)
        AMP2_EN_PC = AMP2_EN_backup.pcState;
    #else
        #if (CY_PSOC4_4200L)
            /* Restore the regulator state and come out of suspend mode */
            AMP2_EN_USB_POWER_REG &= AMP2_EN_USBIO_EXIT_SLEEP_PH1;
            AMP2_EN_CR1_REG = AMP2_EN_backup.usbState;
            AMP2_EN_USB_POWER_REG &= AMP2_EN_USBIO_EXIT_SLEEP_PH2;
        #endif
    #endif
    #if defined(CYIPBLOCK_m0s8ioss_VERSION) && defined(AMP2_EN__SIO)
        AMP2_EN_SIO_REG = AMP2_EN_backup.sioState;
    #endif
}


/* [] END OF FILE */
