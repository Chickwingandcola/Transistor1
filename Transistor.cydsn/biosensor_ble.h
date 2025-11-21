/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#ifndef BIOSENSOR_BLE_H
#define BIOSENSOR_BLE_H

#include "project.h"

// 传感器通道定义
typedef enum {
    SENSOR_PH = 0,
    SENSOR_LACTATE = 1,
    SENSOR_GLUCOSE = 2,
    SENSOR_URIC_ACID = 3,
    SENSOR_COUNT = 4
} SensorChannel_t;

// 传感器数据结构
typedef struct {
    uint16_t ph;          // pH值 (放大100倍, 如pH=7.35 -> 735)
    uint16_t lactate;     // 乳酸浓度 (mmol/L * 100)
    uint16_t glucose;     // 葡萄糖浓度 (mg/dL)
    uint16_t uricAcid;    // 尿酸浓度 (mg/dL)
} BioSensorData_t;

// 全局变量声明
extern BioSensorData_t sensorData;
extern volatile uint8_t notificationEnabled[SENSOR_COUNT];

// 函数声明
void BioSensor_Init(void);
void BioSensor_ReadAllChannels(void);
void BioSensor_UpdateBLE(SensorChannel_t channel);
void BioSensor_SendNotification(SensorChannel_t channel);
uint16_t BioSensor_ReadAD5940(SensorChannel_t channel);

#endif // BIOSENSOR_BLE_H
/* [] END OF FILE */
