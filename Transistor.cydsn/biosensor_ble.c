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
#include "biosensor_ble.h"
#include "ad5940.h"
#include "Impedance.h"
#include "Amperometric.h"

// 全局变量
BioSensorData_t bioSensorData = {0};
volatile uint8_t notificationEnabled[SENSOR_COUNT] = {0};

// BLE连接状态
static uint8_t bleConnected = 0;

/**
 * @brief 初始化生物传感器系统
 */
void BioSensor_Init(void)
{
    // 初始化AD5940
    AD5940_Initialize();
    
    // 初始化各传感器通道
    // pH传感器 - 使用阻抗测量
    AppIMPCfg_Type phConfig;
    AppIMPGetCfg(&phConfig);
    phConfig.DftNum = DFTNUM_16384;
    phConfig.SinFreq = 1000.0; // 1kHz
    AppIMPInit(NULL, 0);
    
    // 乳酸/葡萄糖/尿酸 - 使用电流测量
    AppAMPCfg_Type *pAmpConfig;
    AppAMPGetCfg(&pAmpConfig);
    pAmpConfig->LptiaRtiaSel = LPTIARTIA_512K;  // 正确的成员名称
    pAmpConfig->PwrMod = AFEPWR_LP;           // 使用正确的功率模式成员
    AppAMPInit(NULL, 0);
    
    // 初始化传感器数据
    bioSensorData.ph = 700;        // 默认pH=7.00
    bioSensorData.lactate = 0;
    bioSensorData.glucose = 0;
    bioSensorData.uricAcid = 0;
}

/**
 * @brief 从AD5940读取指定通道数据
 * @param channel 传感器通道
 * @return 测量值(16位)
 */
uint16_t BioSensor_ReadAD5940(SensorChannel_t channel)
{
    uint32_t tempResult = 0;
    uint16_t sensorValue = 0;
    
    switch(channel)
    {
        case SENSOR_PH:
            // pH传感器 - 通过阻抗测量
            // 使能CH0测量
            STIM_EN_A_Write(1);
            AMP1_EN_Write(1);
            
            AppIMPISR(NULL, 0);
            AD5940_ReadAfeResult(AFERESULT_SINC3);
            
            // 转换为pH值 (示例算法,需根据实际校准)
            // 假设: 阻抗范围 1k-10k -> pH 4-10
            float impedance = (float)tempResult / 1000.0;
            float ph = 4.0 + (impedance - 1.0) / 9.0 * 6.0;
            sensorValue = (uint16_t)(ph * 100); // pH*100
            
            AMP1_EN_Write(0);
            STIM_EN_A_Write(0);
            break;
            
        case SENSOR_LACTATE:
            // 乳酸传感器 - 电流测量
            AMP2_EN_Write(1);
            CyDelay(10); // 稳定时间
            
            AppAMPISR(NULL, 0);
            AD5940_ReadAfeResult(AFERESULT_SINC2);
            
            // 转换为浓度 (mmol/L * 100)
            // 假设: 0-20mmol/L范围
            sensorValue = (uint16_t)((tempResult * 2000) / 65535);
            
            AMP2_EN_Write(0);
            break;
            
        case SENSOR_GLUCOSE:
            // 葡萄糖传感器
            AMP3_EN_Write(1);
            CyDelay(10);
            
            AppAMPISR(NULL, 0);
            AD5940_ReadAfeResult(AFERESULT_SINC2);
            
            // 转换为浓度 (mg/dL)
            // 假设: 0-400mg/dL范围
            sensorValue = (uint16_t)((tempResult * 400) / 65535);
            
            AMP3_EN_Write(0);
            break;
            
        case SENSOR_URIC_ACID:
            // 尿酸传感器
            DRUG_EN_1_Write(1);
            CyDelay(10);
            
            AppAMPISR(NULL, 0);
            AD5940_ReadAfeResult(AFERESULT_SINC2);
            
            // 转换为浓度 (mg/dL)
            // 假设: 0-20mg/dL范围
            sensorValue = (uint16_t)((tempResult * 20) / 65535);
            
            DRUG_EN_1_Write(0);
            break;
            
        default:
            break;
    }
    
    return sensorValue;
}

/**
 * @brief 读取所有传感器通道数据
 */
void BioSensor_ReadAllChannels(void)
{
    bioSensorData.ph = BioSensor_ReadAD5940(SENSOR_PH);
    CyDelay(50);
    
    bioSensorData.lactate = BioSensor_ReadAD5940(SENSOR_LACTATE);
    CyDelay(50);
    
    bioSensorData.glucose = BioSensor_ReadAD5940(SENSOR_GLUCOSE);
    CyDelay(50);
    
    bioSensorData.uricAcid = BioSensor_ReadAD5940(SENSOR_URIC_ACID);
}

/**
 * @brief 更新指定通道的BLE特征值
 * @param channel 传感器通道
 */
void BioSensor_UpdateBLE(SensorChannel_t channel)
{
    CYBLE_GATT_HANDLE_VALUE_PAIR_T handleValuePair;
    uint16_t value;
    
    // 获取对应传感器的值
    switch(channel)
    {
        case SENSOR_PH:
            value = bioSensorData.ph;
            handleValuePair.attrHandle = CYBLE_CUSTOMS_SERVICE_COUNT;
            break;
        case SENSOR_LACTATE:
            value = bioSensorData.lactate;
            handleValuePair.attrHandle = CYBLE_CUSTOMC_SERVICE_COUNT;
            break;
        case SENSOR_GLUCOSE:
            value = bioSensorData.glucose;
            handleValuePair.attrHandle = CYBLE_CUSTOM_SERVICE_CHAR_COUNT;
            break;
        case SENSOR_URIC_ACID:
            value = bioSensorData.uricAcid;
            handleValuePair.attrHandle = CYBLE_CUSTOM_SERVICE_CHAR_DESCRIPTORS_COUNT;
            break;
        default:
            return;
    }

    // 设置数据(小端格式)
    uint8_t data[2];
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    
    handleValuePair.value.val = data;
    handleValuePair.value.len = 2;
    
    // 更新GATT数据库
    CyBle_GattsWriteAttributeValue(&handleValuePair, 0, NULL, CYBLE_GATT_DB_LOCALLY_INITIATED);
    
    // 如果通知已使能,发送通知
    if(notificationEnabled[channel] && bleConnected)
    {
        BioSensor_SendNotification(channel);
    }
}

/**
 * @brief 发送BLE通知
 * @param channel 传感器通道
 */
void BioSensor_SendNotification(SensorChannel_t channel)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    uint16_t value;
    
    switch(channel)
    {
        case SENSOR_PH:
            value = bioSensorData.ph;
            notificationHandle.attrHandle = CYBLE_CUSTOMS_SERVICE_COUNT;
            break;
        case SENSOR_LACTATE:
            value = bioSensorData.lactate;
            notificationHandle.attrHandle = CYBLE_CUSTOMC_SERVICE_COUNT;
            break;
        case SENSOR_GLUCOSE:
            value = bioSensorData.glucose;
            notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_CHAR_COUNT;
            break;
        case SENSOR_URIC_ACID:
            value = bioSensorData.uricAcid;
            notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_CHAR_DESCRIPTORS_COUNT;
            break;
        default:
            return;
    }

    uint8_t data[2];
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    
    notificationHandle.value.val = data;
    notificationHandle.value.len = 2;
    
    CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
}

/**
 * @brief BLE事件回调函数
 */
void BioSensor_BleEventHandler(uint32 event, void* eventParam)
{
    switch(event)
    {
        case CYBLE_EVT_STACK_ON:
        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            bleConnected = 0;
            // 开始广播
            CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
            Advertising_LED_Write(1);
            break;
            
        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            bleConnected = 1;
            Advertising_LED_Write(0);
            Disconnect_LED_Write(0);
            break;
            
        case CYBLE_EVT_GATTS_WRITE_REQ:
        {
            CYBLE_GATTS_WRITE_REQ_PARAM_T *writeReq = (CYBLE_GATTS_WRITE_REQ_PARAM_T*)eventParam;
            
            // 检查是否是CCCD写入(通知使能/禁用)
            if(writeReq->handleValPair.value.len == 2)
            {
                uint16_t cccdValue = writeReq->handleValPair.value.val[0] | 
                                    (writeReq->handleValPair.value.val[1] << 8);
                
                // 根据handle确定是哪个传感器的通知
                if(writeReq->handleValPair.attrHandle == CYBLE_CUSTOMS_SERVICE_COUNT)
                    notificationEnabled[SENSOR_PH] = (cccdValue == 0x0001);
                else if(writeReq->handleValPair.attrHandle == CYBLE_CUSTOMC_SERVICE_COUNT)
                    notificationEnabled[SENSOR_LACTATE] = (cccdValue == 0x0001);
                else if(writeReq->handleValPair.attrHandle == CYBLE_CUSTOM_SERVICE_CHAR_COUNT)
                    notificationEnabled[SENSOR_GLUCOSE] = (cccdValue == 0x0001);
                else if(writeReq->handleValPair.attrHandle == CYBLE_CUSTOM_SERVICE_CHAR_DESCRIPTORS_COUNT)
                    notificationEnabled[SENSOR_URIC_ACID] = (cccdValue == 0x0001);
            }

            CyBle_GattsWriteRsp(cyBle_connHandle);
            break;
        }
        
        default:
            break;
    }
}
/* [] END OF FILE */
