/*******************************************************************************
* File Name: main.h
*
* Version 1.0
*
* Description:
*  Contains the function prototypes and constants available to the example
*  project.
*
********************************************************************************
* Copyright 2016, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#if !defined(MAIN_H)
#define MAIN_H
    
#include <project.h>
#include <stdio.h>

#include "debug.h"
#include <math.h>
#include <string.h>
/* Profile specific includes */
#include "bass.h"
#include "hrss.h"


#define LED_ON                      (0u)
#define LED_OFF                     (1u)
    
#define PASSKEY                     (0x04u)


/***************************************
*      API Function Prototypes
***************************************/
void StartAdvertisement(void);


/***************************************
*      External data references
***************************************/
extern CYBLE_API_RESULT_T apiResult;
extern uint16 i;
extern uint8 flag;


/*******************************************************************************
* LED控制宏定义
*******************************************************************************/
#define LED_ON                  (0u)
#define LED_OFF                 (1u)

/*******************************************************************************
* 传感器配置参数
*******************************************************************************/
// 测量间隔（秒）
#define MEASUREMENT_INTERVAL    (1u)

// 安培法传感器校准系数（来自论文图2B-D）
#define GLUCOSE_SENSITIVITY     (16.34f)    // nA/mM
#define LACTATE_SENSITIVITY     (41.44f)    // nA/mM
#define URIC_ACID_SENSITIVITY   (189.60f)   // nA/μM

// 电位法传感器参数（能斯特方程）
#define NERNST_SLOPE            (59.7f)     // mV/decade at 25°C
#define PH_REFERENCE            (7.0f)      // pH参考值

// 温度传感器参数（Au的温度系数）
#define TEMP_COEFFICIENT        (0.0021f)   // /°C
#define TEMP_REFERENCE          (25.0f)     // °C
#define RESISTANCE_REFERENCE    (1000.0f)   // Ω

// 治疗阈值
#define INFECTION_TEMP_THRESHOLD    (38.5f)     // °C
#define INFECTION_PH_THRESHOLD      (8.0f)
#define INFECTION_LACTATE_THRESHOLD (5.0f)      // mM
#define HEALING_GLUCOSE_THRESHOLD   (10.0f)     // mM

// 药物释放控制
#define DRUG_RELEASE_DURATION   (600000u)   // 10分钟 (ms)
#define DRUG_RELEASE_VOLTAGE    (1.0f)      // 1V

// 电刺激参数（论文图3L）
#define ESTIM_FREQUENCY         (50u)       // 50 Hz
#define ESTIM_VOLTAGE           (1.0f)      // 1V
#define ESTIM_DUTY_CYCLE        (10u)       // 10%

/*******************************************************************************
* 函数声明
*******************************************************************************/
// 初始化函数
void AD5941_Initialize(void);

// 传感器测量函数
float MeasureAmperometricSensor(uint8 sensorType);
float ConvertCurrentToConcentration(float current_nA, uint8 sensorType);
float MeasureTemperature(void);
float MeasurePotentiometric(uint8 sensorType);
void MeasureAllSensors(void);

// FIFO诊断函数
void DiagnoseFIFO(void);
void ForceFIFOEnable(void);

// 治疗控制函数
void ControlDrugRelease(uint8 enable);
void ControlElectricalStimulation(uint8 enable);

// BLE通信函数
void SendDataViaBLE(void);
void StartAdvertisement(void);
void AppCallBack(uint32 event, void* eventParam);

// 系统函数
CY_ISR_PROTO(Timer_Interrupt);
static void LowPowerImplementation(void);

/*******************************************************************************
* 外部变量声明
*******************************************************************************/
extern volatile uint32 mainTimer;
extern volatile uint8 measurementFlag;
extern CYBLE_API_RESULT_T apiResult;


#endif /* MAIN_H */



/* [] END OF FILE */
