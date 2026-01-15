/*******************************************************************************
* File Name: Lowpower_LED.h  
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

#if !defined(CY_PINS_Lowpower_LED_ALIASES_H) /* Pins Lowpower_LED_ALIASES_H */
#define CY_PINS_Lowpower_LED_ALIASES_H

#include "cytypes.h"
#include "cyfitter.h"
#include "cypins.h"


/***************************************
*              Constants        
***************************************/
#define Lowpower_LED_0			(Lowpower_LED__0__PC)
#define Lowpower_LED_0_PS		(Lowpower_LED__0__PS)
#define Lowpower_LED_0_PC		(Lowpower_LED__0__PC)
#define Lowpower_LED_0_DR		(Lowpower_LED__0__DR)
#define Lowpower_LED_0_SHIFT	(Lowpower_LED__0__SHIFT)
#define Lowpower_LED_0_INTR	((uint16)((uint16)0x0003u << (Lowpower_LED__0__SHIFT*2u)))

#define Lowpower_LED_INTR_ALL	 ((uint16)(Lowpower_LED_0_INTR))


#endif /* End Pins Lowpower_LED_ALIASES_H */


/* [] END OF FILE */
