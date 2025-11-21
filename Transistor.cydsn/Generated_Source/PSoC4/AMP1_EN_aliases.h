/*******************************************************************************
* File Name: AMP1_EN.h  
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

#if !defined(CY_PINS_AMP1_EN_ALIASES_H) /* Pins AMP1_EN_ALIASES_H */
#define CY_PINS_AMP1_EN_ALIASES_H

#include "cytypes.h"
#include "cyfitter.h"
#include "cypins.h"


/***************************************
*              Constants        
***************************************/
#define AMP1_EN_0			(AMP1_EN__0__PC)
#define AMP1_EN_0_PS		(AMP1_EN__0__PS)
#define AMP1_EN_0_PC		(AMP1_EN__0__PC)
#define AMP1_EN_0_DR		(AMP1_EN__0__DR)
#define AMP1_EN_0_SHIFT	(AMP1_EN__0__SHIFT)
#define AMP1_EN_0_INTR	((uint16)((uint16)0x0003u << (AMP1_EN__0__SHIFT*2u)))

#define AMP1_EN_INTR_ALL	 ((uint16)(AMP1_EN_0_INTR))


#endif /* End Pins AMP1_EN_ALIASES_H */


/* [] END OF FILE */
