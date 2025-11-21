/*******************************************************************************
* File Name: AD5940_EXTI.h  
* Version 2.20
*
* Description:
*  This file contains the Alias definitions for Per-Pin APIs in cypins.h. 
*  Information on using these APIs can be found in the System Reference Guide.
*
* Note:
*
********************************************************************************
* Copyright 2008-2015, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions, 
* disclaimers, and limitations in the end user license agreement accompanying 
* the software package with which this file was provided.
*******************************************************************************/

#if !defined(CY_PINS_AD5940_EXTI_ALIASES_H) /* Pins AD5940_EXTI_ALIASES_H */
#define CY_PINS_AD5940_EXTI_ALIASES_H

#include "cytypes.h"
#include "cyfitter.h"
#include "cypins.h"


/***************************************
*              Constants        
***************************************/
#define AD5940_EXTI_0			(AD5940_EXTI__0__PC)
#define AD5940_EXTI_0_PS		(AD5940_EXTI__0__PS)
#define AD5940_EXTI_0_PC		(AD5940_EXTI__0__PC)
#define AD5940_EXTI_0_DR		(AD5940_EXTI__0__DR)
#define AD5940_EXTI_0_SHIFT	(AD5940_EXTI__0__SHIFT)
#define AD5940_EXTI_0_INTR	((uint16)((uint16)0x0003u << (AD5940_EXTI__0__SHIFT*2u)))

#define AD5940_EXTI_INTR_ALL	 ((uint16)(AD5940_EXTI_0_INTR))


#endif /* End Pins AD5940_EXTI_ALIASES_H */


/* [] END OF FILE */
