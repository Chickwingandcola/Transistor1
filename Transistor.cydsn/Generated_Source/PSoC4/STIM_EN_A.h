/*******************************************************************************
* File Name: STIM_EN_A.h  
* Version 2.20
*
* Description:
*  This file contains Pin function prototypes and register defines
*
********************************************************************************
* Copyright 2008-2015, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions, 
* disclaimers, and limitations in the end user license agreement accompanying 
* the software package with which this file was provided.
*******************************************************************************/

#if !defined(CY_PINS_STIM_EN_A_H) /* Pins STIM_EN_A_H */
#define CY_PINS_STIM_EN_A_H

#include "cytypes.h"
#include "cyfitter.h"
#include "STIM_EN_A_aliases.h"


/***************************************
*     Data Struct Definitions
***************************************/

/**
* \addtogroup group_structures
* @{
*/
    
/* Structure for sleep mode support */
typedef struct
{
    uint32 pcState; /**< State of the port control register */
    uint32 sioState; /**< State of the SIO configuration */
    uint32 usbState; /**< State of the USBIO regulator */
} STIM_EN_A_BACKUP_STRUCT;

/** @} structures */


/***************************************
*        Function Prototypes             
***************************************/
/**
* \addtogroup group_general
* @{
*/
uint8   STIM_EN_A_Read(void);
void    STIM_EN_A_Write(uint8 value);
uint8   STIM_EN_A_ReadDataReg(void);
#if defined(STIM_EN_A__PC) || (CY_PSOC4_4200L) 
    void    STIM_EN_A_SetDriveMode(uint8 mode);
#endif
void    STIM_EN_A_SetInterruptMode(uint16 position, uint16 mode);
uint8   STIM_EN_A_ClearInterrupt(void);
/** @} general */

/**
* \addtogroup group_power
* @{
*/
void STIM_EN_A_Sleep(void); 
void STIM_EN_A_Wakeup(void);
/** @} power */


/***************************************
*           API Constants        
***************************************/
#if defined(STIM_EN_A__PC) || (CY_PSOC4_4200L) 
    /* Drive Modes */
    #define STIM_EN_A_DRIVE_MODE_BITS        (3)
    #define STIM_EN_A_DRIVE_MODE_IND_MASK    (0xFFFFFFFFu >> (32 - STIM_EN_A_DRIVE_MODE_BITS))

    /**
    * \addtogroup group_constants
    * @{
    */
        /** \addtogroup driveMode Drive mode constants
         * \brief Constants to be passed as "mode" parameter in the STIM_EN_A_SetDriveMode() function.
         *  @{
         */
        #define STIM_EN_A_DM_ALG_HIZ         (0x00u) /**< \brief High Impedance Analog   */
        #define STIM_EN_A_DM_DIG_HIZ         (0x01u) /**< \brief High Impedance Digital  */
        #define STIM_EN_A_DM_RES_UP          (0x02u) /**< \brief Resistive Pull Up       */
        #define STIM_EN_A_DM_RES_DWN         (0x03u) /**< \brief Resistive Pull Down     */
        #define STIM_EN_A_DM_OD_LO           (0x04u) /**< \brief Open Drain, Drives Low  */
        #define STIM_EN_A_DM_OD_HI           (0x05u) /**< \brief Open Drain, Drives High */
        #define STIM_EN_A_DM_STRONG          (0x06u) /**< \brief Strong Drive            */
        #define STIM_EN_A_DM_RES_UPDWN       (0x07u) /**< \brief Resistive Pull Up/Down  */
        /** @} driveMode */
    /** @} group_constants */
#endif

/* Digital Port Constants */
#define STIM_EN_A_MASK               STIM_EN_A__MASK
#define STIM_EN_A_SHIFT              STIM_EN_A__SHIFT
#define STIM_EN_A_WIDTH              1u

/**
* \addtogroup group_constants
* @{
*/
    /** \addtogroup intrMode Interrupt constants
     * \brief Constants to be passed as "mode" parameter in STIM_EN_A_SetInterruptMode() function.
     *  @{
     */
        #define STIM_EN_A_INTR_NONE      ((uint16)(0x0000u)) /**< \brief Disabled             */
        #define STIM_EN_A_INTR_RISING    ((uint16)(0x5555u)) /**< \brief Rising edge trigger  */
        #define STIM_EN_A_INTR_FALLING   ((uint16)(0xaaaau)) /**< \brief Falling edge trigger */
        #define STIM_EN_A_INTR_BOTH      ((uint16)(0xffffu)) /**< \brief Both edge trigger    */
    /** @} intrMode */
/** @} group_constants */

/* SIO LPM definition */
#if defined(STIM_EN_A__SIO)
    #define STIM_EN_A_SIO_LPM_MASK       (0x03u)
#endif

/* USBIO definitions */
#if !defined(STIM_EN_A__PC) && (CY_PSOC4_4200L)
    #define STIM_EN_A_USBIO_ENABLE               ((uint32)0x80000000u)
    #define STIM_EN_A_USBIO_DISABLE              ((uint32)(~STIM_EN_A_USBIO_ENABLE))
    #define STIM_EN_A_USBIO_SUSPEND_SHIFT        CYFLD_USBDEVv2_USB_SUSPEND__OFFSET
    #define STIM_EN_A_USBIO_SUSPEND_DEL_SHIFT    CYFLD_USBDEVv2_USB_SUSPEND_DEL__OFFSET
    #define STIM_EN_A_USBIO_ENTER_SLEEP          ((uint32)((1u << STIM_EN_A_USBIO_SUSPEND_SHIFT) \
                                                        | (1u << STIM_EN_A_USBIO_SUSPEND_DEL_SHIFT)))
    #define STIM_EN_A_USBIO_EXIT_SLEEP_PH1       ((uint32)~((uint32)(1u << STIM_EN_A_USBIO_SUSPEND_SHIFT)))
    #define STIM_EN_A_USBIO_EXIT_SLEEP_PH2       ((uint32)~((uint32)(1u << STIM_EN_A_USBIO_SUSPEND_DEL_SHIFT)))
    #define STIM_EN_A_USBIO_CR1_OFF              ((uint32)0xfffffffeu)
#endif


/***************************************
*             Registers        
***************************************/
/* Main Port Registers */
#if defined(STIM_EN_A__PC)
    /* Port Configuration */
    #define STIM_EN_A_PC                 (* (reg32 *) STIM_EN_A__PC)
#endif
/* Pin State */
#define STIM_EN_A_PS                     (* (reg32 *) STIM_EN_A__PS)
/* Data Register */
#define STIM_EN_A_DR                     (* (reg32 *) STIM_EN_A__DR)
/* Input Buffer Disable Override */
#define STIM_EN_A_INP_DIS                (* (reg32 *) STIM_EN_A__PC2)

/* Interrupt configuration Registers */
#define STIM_EN_A_INTCFG                 (* (reg32 *) STIM_EN_A__INTCFG)
#define STIM_EN_A_INTSTAT                (* (reg32 *) STIM_EN_A__INTSTAT)

/* "Interrupt cause" register for Combined Port Interrupt (AllPortInt) in GSRef component */
#if defined (CYREG_GPIO_INTR_CAUSE)
    #define STIM_EN_A_INTR_CAUSE         (* (reg32 *) CYREG_GPIO_INTR_CAUSE)
#endif

/* SIO register */
#if defined(STIM_EN_A__SIO)
    #define STIM_EN_A_SIO_REG            (* (reg32 *) STIM_EN_A__SIO)
#endif /* (STIM_EN_A__SIO_CFG) */

/* USBIO registers */
#if !defined(STIM_EN_A__PC) && (CY_PSOC4_4200L)
    #define STIM_EN_A_USB_POWER_REG       (* (reg32 *) CYREG_USBDEVv2_USB_POWER_CTRL)
    #define STIM_EN_A_CR1_REG             (* (reg32 *) CYREG_USBDEVv2_CR1)
    #define STIM_EN_A_USBIO_CTRL_REG      (* (reg32 *) CYREG_USBDEVv2_USB_USBIO_CTRL)
#endif    
    
    
/***************************************
* The following code is DEPRECATED and 
* must not be used in new designs.
***************************************/
/**
* \addtogroup group_deprecated
* @{
*/
#define STIM_EN_A_DRIVE_MODE_SHIFT       (0x00u)
#define STIM_EN_A_DRIVE_MODE_MASK        (0x07u << STIM_EN_A_DRIVE_MODE_SHIFT)
/** @} deprecated */

#endif /* End Pins STIM_EN_A_H */


/* [] END OF FILE */
