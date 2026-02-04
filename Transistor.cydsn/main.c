
/*******************************************************************************
* File Name: main.c
*
* Version 2.0
*
* Description:
*  多路生物传感器系统 - 修复后的完整版本
*  使用AD5940内置ADC，不依赖PSoC ADC模块
*
********************************************************************************/

// /* 链接数学库（GCC编译器） */
// #pragma comment(lib, "libm.a")

#include "main.h"
#include "ad5940.h"
#include "ad5941_platform.h"
#include "Amperometric.h"
uint8_t g_SPI_Debug_Buf[8] = {0}; // 全局变量，记录最后一次读取的原始字节
static uint8_t g_amp_running = 0;

// 在 main() 函数开头添加变量
uint32 lastSendTime = 0;  // 上次发送数据的时间
#define SEND_INTERVAL 3   // 每3秒发送一次数据

/*******************************************************************************
* 全局变量
*******************************************************************************/
volatile uint32 mainTimer = 0;
volatile uint8 measurementFlag = 0;
CYBLE_API_RESULT_T apiResult;

// 传感器数据结构
typedef struct {
    float temperature;      // 温度 (°C) - 从AD5940读取
    float glucose;         // 葡萄糖 (mM)
    float lactate;         // 乳酸 (mM)
    float uric_acid;       // 尿酸 (μM)
    uint32 timestamp;      // 时间戳
    
    // 新增：原始电流值（从源表/AD5940读取）
    float current_glucose_nA;   // 葡萄糖传感器电流 (nA)
    float current_lactate_nA;   // 乳酸传感器电流 (nA)
    float current_uric_nA;      // 尿酸传感器电流 (nA)    
} SensorData_t;

// 传感器数据全局变量（定义在main.h中的SensorData_t结构）
SensorData_t sensorData = {0};

typedef enum {
    SENSOR_GLUCOSE = 0,
    SENSOR_LACTATE = 1,
    SENSOR_URIC_ACID = 2,
    SENSOR_COUNT = 3
} AmperometricSensor_t;





// AD5941相关变量
AppAMPCfg_Type *pAmpCfg;
uint32 ampBuffer[512];  // 用于AppAMPInit的缓冲区
fAmpRes_Type ampResult;




/*******************************************************************************
* Function Name: DiagnosticsElectrodes
********************************************************************************
* Summary:
*   诊断电极配置和接线
*******************************************************************************/
void DiagnosticsElectrodes(void)
{
    printf("\n=== ELECTRODE CONFIGURATION ===\n");
    
    printf("[DEBUG] Electrode Control Pin Status:\n");
    printf("    AMP1_EN (Glucose): %d\n", (int)AMP1_EN_Read());
    printf("    AMP2_EN (Lactate): %d\n", (int)AMP2_EN_Read());
    printf("    AMP3_EN (Uric Acid): %d\n", (int)AMP3_EN_Read());
    
    printf("\n[CHECK] Please verify:\n");
    printf("    1. WE (Working Electrode): Connected to appropriate sensor\n");
    printf("    2. RE (Reference Electrode): Connected to Ag/AgCl or similar\n");
    printf("    3. CE (Counter Electrode): Connected to carbon or similar\n");
    printf("    4. Sensor bias voltage: %.1f mV\n", (double)pAmpCfg->SensorBias);
    printf("    5. RTIA value: %.0f Ohm\n", (double)pAmpCfg->RtiaCalValue.Magnitude);
    
    printf("=== ELECTRODE TEST END ===\n\n");
}



/*******************************************************************************
* Function Name: MeasureAmperometricSensor
********************************************************************************
* Summary:
*   测量安培法传感器 - 修复后版本
*   不使用AMux，直接使用AD5941的WE0/WE1/WE2通道
*******************************************************************************/
float MeasureAmperometricSensor(AmperometricSensor_t sensorType)
{
    float current_uA = 0;
    uint32_t dataCount = 0;
    AD5940Err error;
    
    
    // 注意：如果不使用AMux，AD5941会使用默认的WE0通道
    // 如果需要切换WE通道，需要在配置中修改
    
    // 启动测量
    error = AppAMPCtrl(AMPCTRL_START, NULL);
    if(error != AD5940ERR_OK)
    {
        return 0;
    }
    
    // 等待测量稳定（安培法需要500ms）
    CyDelay(500);
    
    // 读取FIFO数据
    error = AppAMPISR(&ampResult, &dataCount);
    
    if(error == AD5940ERR_OK && dataCount > 0)
    {
        // 获取电流值（fAmpRes_Type结构体中的Current成员）
        current_uA = ampResult.Current * 1000.0;  // 转换为nA
    }
    else
    {
    }
    
    // 停止测量
    AppAMPCtrl(AMPCTRL_STOPNOW, NULL);
    
    return current_uA;
}

/*******************************************************************************
* Function Name: ConvertCurrentToConcentration
********************************************************************************
* Summary:
*   将电流值转换为浓度（根据论文校准系数）
*******************************************************************************/
float ConvertCurrentToConcentration(float current_nA, uint8 sensorType)
{
    float concentration = 0;
    
    switch(sensorType)
    {
        case SENSOR_GLUCOSE:
            concentration = current_nA / 16.34;  // 16.34 nA/mM
            break;
            
        case SENSOR_LACTATE:
            concentration = current_nA / 41.44;  // 41.44 nA/mM
            break;
            
        case SENSOR_URIC_ACID:
            concentration = current_nA / 189.60; // 189.60 nA/μM
            break;
            
        default:
            concentration = 0;
            break;
    }
    
    return concentration;
}

/*******************************************************************************
* Function Name: MeasureTemperature
********************************************************************************
* Summary:
*   使用AD5940的ADC测量温度传感器
*   替代PSoC的ADC模块
*******************************************************************************/
float MeasureTemperature(void)
{
    float temperature = 25.0;  // 默认室温
    uint32_t adcCode;
    float voltage, resistance;
    
    
    // 方法1: 使用AD5940的辅助ADC（AUX ADC）测量温度传感器
    // 注意：需要配置AD5940的GPIO或者AUX输入来连接温度传感器
    
    // 这里提供一个简化的实现
    // 实际应用中，你需要：
    // 1. 配置AD5940的GPIO/AUX为ADC输入
    // 2. 读取ADC值
    // 3. 转换为温度
    
    // 示例代码（需要根据实际硬件调整）:
    /*
    AD5940_StructInit(&ADCInit, sizeof(ADCInit));
    ADCInit.ADCMuxP = ADCMUXP_AIN2;  // 假设温度传感器连接到AIN2
    ADCInit.ADCMuxN = ADCMUXN_VSET1P1;
    AD5940_ADCInit(&ADCInit);
    
    AD5940_ADCStart();
    CyDelay(10);
    adcCode = AD5940_ReadADC();
    
    voltage = (adcCode * 1.82) / 32768.0;  // 转换为电压
    resistance = 1000.0 * voltage / (3.3 - voltage);  // 假设分压电路
    temperature = 25.0 + (resistance - 1000.0) / (1000.0 * 0.0021);
    */
    
    // 临时方案：从系统读取或使用固定值
    temperature = 37.0;  // 假设体温
    
    
    return temperature;
}


/*******************************************************************************
* Function Name: ReadCurrentFromAD5940
********************************************************************************
* Summary:
*   从 AD5940 读取实际电流值（安培法测量）
*   
* Parameters:
*   sensorType: 传感器类型（GLUCOSE, LACTATE, URIC_ACID）
*
* Return:
*   float: 电流值（单位：nA）
*******************************************************************************/
float ReadCurrentFromAD5940(AmperometricSensor_t sensorType)
{
    float current_nA = 0;
    uint32_t dataCount = 0;
    AD5940Err error;
    static uint32_t ampFifoBuffer[256];  // 存储FIFO数据的缓冲区
    
    // 1. 配置 AD5940 为安培法测量模式
    AppAMPGetCfg(&pAmpCfg);
    
    // 根据传感器类型设置工作电极
    switch(sensorType)
    {
        case SENSOR_GLUCOSE:
            AMP1_EN_Write(1);  // 启用通道1
            AMP2_EN_Write(0);
            AMP3_EN_Write(0);
            break;
            
        case SENSOR_LACTATE:
            AMP1_EN_Write(0);
            AMP2_EN_Write(1);  // 启用通道2
            AMP3_EN_Write(0);
            break;
            
        case SENSOR_URIC_ACID:
            AMP1_EN_Write(0);
            AMP2_EN_Write(0);
            AMP3_EN_Write(1);  // 启用通道3
            break;
    }
    
    CyDelay(50);  // 等待通道切换稳定
    
    // 2. 启动测量
    error = AppAMPCtrl(AMPCTRL_START, NULL);
    if(error != AD5940ERR_OK)
    {
        return 0;
    }
    
    // 3. 等待测量稳定（安培法需要 500ms）
    CyDelay(500);
    
    // 4. 读取 FIFO 数据
    // 第一个参数：指向FIFO数据缓冲区，AppAMPISR会将ADC代码读到这里，然后转换为电流值
    // 第二个参数：指向数据计数，返回读取的数据点数
    error = AppAMPISR(ampFifoBuffer, &dataCount);
    
    if(error == AD5940ERR_OK && dataCount > 0)
    {
        // 5. 从缓冲区中提取电流值
        // AppAMPDataProcess 已经将ADC值转换为电流值（float）
        // 结果存储在 ampFifoBuffer 中（已转换为float指针）
        float *pCurrentData = (float *)ampFifoBuffer;
        
        // 取第一个样本的电流值（单位：A）
        current_nA = pCurrentData[0] * 1e9;  // 转换为 nA
        
    }
    else
    {
        current_nA = 0;
    }
    
    // 6. 停止测量
    AppAMPCtrl(AMPCTRL_STOPNOW, NULL);
    

    
    
    

    CyDelay(50);  // 等待通道切换稳定
    
    // 2. 启动测量
    error = AppAMPCtrl(AMPCTRL_START, NULL);
    if(error != AD5940ERR_OK)
    {
        return 0;
    }
    
    // 3. 等待测量稳定（安培法需要 500ms）
    CyDelay(500);
    
    // 4. 读取 FIFO 数据
    error = AppAMPISR(&ampResult, &dataCount);
    
    if(error == AD5940ERR_OK && dataCount > 0)
    {
        // 5. 从结果中提取电流值
        current_nA = ampResult.Current * 1e9;  // 转换为 nA
        
    }
    else
    {
        current_nA = 0;
    }
    
    // 6. 停止测量
    AppAMPCtrl(AMPCTRL_STOPNOW, NULL);
    
    return current_nA;
}

/*******************************************************************************
* Function Name: ReadCurrentFromSourceMeter_Simulated
********************************************************************************
* Summary:
*   模拟从源表读取电流值（用于测试）
*   实际使用时替换为真实的源表读取代码
*
* Return:
*   float: 电流值（单位：nA）
*******************************************************************************/
float ReadCurrentFromSourceMeter_Simulated(AmperometricSensor_t sensorType)
{
    float current_nA = 0;
    
    // 模拟不同传感器的典型电流响应
    switch(sensorType)
    {
        case SENSOR_GLUCOSE:
            // 5 mM 葡萄糖 → 约 80 nA（根据论文校准系数 16.34 nA/mM）
            current_nA = 85.0 + (rand() % 10 - 5);  // 80-90 nA
            break;
            
        case SENSOR_LACTATE:
            // 2 mM 乳酸 → 约 100 nA（根据论文校准系数 41.44 nA/mM）
            current_nA = 100.0 + (rand() % 10 - 5);  // 95-105 nA
            break;
            
        case SENSOR_URIC_ACID:
            // 300 μM 尿酸 → 约 57000 nA（根据论文校准系数 189.60 nA/μM）
            //current_nA = 57000.0 + (rand() % 100 - 50);
            current_nA = 570.0 + (rand() % 100 - 50);
            break;
            
        default:
            current_nA = 0;
            break;
    }
    

    
    return current_nA;
}

/*******************************************************************************
// * Function Name: MeasureAllSensorsWithCurrent
// ********************************************************************************
// * Summary:
// *   测量所有传感器 - 包含实际电流值
// *******************************************************************************/
// void MeasureAllSensorsWithCurrent(void)
// {
 
//     // 1. 温度测量
//     sensorData.temperature = MeasureTemperature();
    
//     // 2. 葡萄糖测量

    
//     // 方法 A: 使用 AD5940 读取（推荐）
//    sensorData.current_glucose_nA = ReadCurrentFromAD5940(SENSOR_GLUCOSE);
    
//     // 方法 B: 使用模拟值测试（测试用）
//     //sensorData.current_glucose_nA = ReadCurrentFromSourceMeter_Simulated(SENSOR_GLUCOSE);
    
//     // 转换为浓度
//     sensorData.glucose = ConvertCurrentToConcentration(sensorData.current_glucose_nA, SENSOR_GLUCOSE);
    
    
//     // 3. 乳酸测量

//      sensorData.current_lactate_nA = ReadCurrentFromAD5940(SENSOR_LACTATE); 
//     //sensorData.current_lactate_nA = ReadCurrentFromSourceMeter_Simulated(SENSOR_LACTATE);
//     sensorData.lactate = ConvertCurrentToConcentration(sensorData.current_lactate_nA, SENSOR_LACTATE);
    
//     // 4. 尿酸测量

//     sensorData.current_uric_nA = ReadCurrentFromAD5940(SENSOR_URIC_ACID);
//     //sensorData.uric_acid = ConvertCurrentToConcentration(sensorData.current_uric_nA,SENSOR_URIC_ACID);
//     // [修改] 切换为模拟数据
//     // sensorData.current_uric_acid_nA = ReadCurrentFromAD5940(SENSOR_URIC_ACID);
//     //sensorData.current_uric_nA = ReadCurrentFromSourceMeter_Simulated(SENSOR_URIC_ACID); 
//     // [增加] 电流换算到浓度
//     sensorData.uric_acid = ConvertCurrentToConcentration(sensorData.current_uric_nA, SENSOR_URIC_ACID);    

    

    
//     // 6. 温度校准
//     float temp_factor = 1.0 + 0.03 * (sensorData.temperature - 37.0);
//     sensorData.glucose *= temp_factor;
//     sensorData.lactate *= temp_factor;
//     sensorData.uric_acid *= temp_factor;
    
//     sensorData.timestamp = mainTimer;
    
// }


// 修改 main.c 中的 MeasureAllSensorsWithCurrent 函数
void MeasureAllSensorsWithCurrent(void)
{
    // 1. 强制打开 AMP2 (Lactate) 并保持打开，用于测试
    AMP1_EN_Write(0);
    AMP2_EN_Write(1); // 🔴 强制导通 Lactate 通道
    AMP3_EN_Write(0);
    
    // 2. 仅测量 Lactate
    // 注意：这里我们调用 ReadCurrentFromAD5940，但要确保该函数内部不会很快就把开关关掉
    // 由于该函数内部会重新配置开关，可能会有短暂跳变，建议修改 ReadCurrentFromAD5940 
    // 或者直接在这里等待稳定后读取
    
    sensorData.current_lactate_nA = ReadCurrentFromAD5940(SENSOR_LACTATE); 
    sensorData.lactate = ConvertCurrentToConcentration(sensorData.current_lactate_nA, SENSOR_LACTATE);

    // 调试打印 (如果有串口)
    printf("Lactate Current: %.2f nA\r\n", sensorData.current_lactate_nA);

    // 3. 暂时屏蔽其他测量，防止通道切换
    sensorData.temperature = 25.0; 
    sensorData.glucose = 0;
    sensorData.uric_acid = 0;
    
    sensorData.timestamp = mainTimer;
}


/*******************************************************************************
* Function Name: SendCurrentDataViaBLE
********************************************************************************
* Summary:
*   发送电流值到 BLE（显示在手机 App 上）
*******************************************************************************/

// 发送葡萄糖数据（浓度 + 电流）
void SendGlucoseDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    char dataString[30];
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        // 格式：浓度 + 电流值
        sprintf(dataString, "%.2f mM (%.1f nA)", 
                sensorData.glucose, 
                sensorData.current_glucose_nA);
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_GLUCOSE_MEASUREMENT_CHAR_HANDLE;
        notificationHandle.value.val = (uint8*)dataString;
        notificationHandle.value.len = strlen(dataString);
        
        if(CyBle_GattsNotification(cyBle_connHandle, &notificationHandle) == CYBLE_ERROR_OK)
        {
        }
    }
}

// 发送乳酸数据（浓度 + 电流）
void SendLactateDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    char dataString[30];
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        sprintf(dataString, "%.2f mM (%.1f nA)", 
                sensorData.lactate, 
                sensorData.current_lactate_nA);
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
        notificationHandle.value.val = (uint8*)dataString;
        notificationHandle.value.len = strlen(dataString);
        
        if(CyBle_GattsNotification(cyBle_connHandle, &notificationHandle) == CYBLE_ERROR_OK)
        {
        }
    }
}

// 发送温度数据
void SendTemperatureViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    char tempString[20];
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        sprintf(tempString, "%.1f C", sensorData.temperature);
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_TEMPERATURE_MEASUREMENT_CHAR_HANDLE;
        notificationHandle.value.val = (uint8*)tempString;
        notificationHandle.value.len = strlen(tempString);
        
        CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
    }
}








/*******************************************************************************
* Function Name: SendAllSensorDataViaBLE
********************************************************************************
* Summary:
*   发送所有传感器数据（包含电流值）
*******************************************************************************/
void SendAllSensorDataViaBLE(void)
{
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        
        SendTemperatureViaBLE();
        CyDelay(50);
        
        SendGlucoseDataViaBLE();  // 包含电流值
        CyDelay(50);
        
        SendLactateDataViaBLE();  // 包含电流值
        CyDelay(50);
        
       
    }
}

/*******************************************************************************
* Function Name: Timer_Interrupt
********************************************************************************
* Summary:
*   定时器中断 - 每秒触发一次
*******************************************************************************/
CY_ISR(Timer_Interrupt)
{
    static uint8 led = LED_OFF;
    
    if(CYBLE_STATE_ADVERTISING == CyBle_GetState())
    {
        led ^= LED_OFF;
        Advertising_LED_Write(led);
    }
    
    mainTimer++;
    measurementFlag = 1;  // 每秒设置测量标志
}

/*******************************************************************************
* Function Name: AppCallBack
********************************************************************************
* Summary:
*   BLE事件回调函数
*******************************************************************************/
void AppCallBack(uint32 event, void* eventParam)
{
    uint16 i;
    
    switch(event)
    {
        case CYBLE_EVT_STACK_ON:
            StartAdvertisement();
            break;

        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            Advertising_LED_Write(LED_OFF);
            break;

        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            StartAdvertisement();
            break;
            
        case CYBLE_EVT_GAPP_ADVERTISEMENT_START_STOP:
            if(CYBLE_STATE_DISCONNECTED == CyBle_GetState())
            {
                Advertising_LED_Write(LED_OFF);
                Disconnect_LED_Write(LED_ON);
                
                // 清除中断并进入休眠
                AD5940_EXTI_ClearInterrupt();
                AD5940_Interrupt_ClearPending();
                AD5940_Interrupt_Start();
                CySysPmHibernate();
            }
            break;

        case CYBLE_EVT_PENDING_FLASH_WRITE:
            break;

        default:
            break;
    }
}

/*******************************************************************************
* ADC码值转电流
*******************************************************************************/
float ADCCode_to_Current_nA(uint32_t adc_code, float rtia, float vref, float pga_gain)
{
    int32_t adc_signed;
    if(adc_code & 0x8000)
    {
        adc_signed = (int32_t)adc_code - 65536;
    }
    else
    {
        adc_signed = (int32_t)adc_code;
    }
    
    float v_adc = (float)adc_signed * vref / 32768.0 / pga_gain;
    float current_A = v_adc / rtia;
    float current_nA = current_A * 1e9;
    
    return current_nA;
}

// ===== 在全局变量区域添加测量状态机相关变量 =====
typedef enum {
    MEAS_IDLE = 0,
    MEAS_SWITCH_CHANNEL,
    MEAS_WAIT_STABLE,
    MEAS_READ_FIFO,
    MEAS_NEXT_SENSOR,
    MEAS_COMPLETE
} MeasState_t;

static MeasState_t g_meas_state = MEAS_IDLE;
static AmperometricSensor_t g_current_sensor = SENSOR_GLUCOSE;
static uint32_t g_meas_start_time = 0;

// ===== 添加测量处理函数（在main函数之前） =====
void ProcessMeasurement(void) 
{
    uint32_t fifosta;
    uint16_t data_count;
    uint32_t fifo_data;
    uint32_t adc_code;
    float current_nA;
    
    switch(g_meas_state)
    {
        case MEAS_IDLE:
            // 等待触发
            break;
            
        case MEAS_SWITCH_CHANNEL:
            // ✅ 切换通道（不阻塞）
            switch(g_current_sensor)
            {
                case SENSOR_GLUCOSE:
                    AMP1_EN_Write(1);
                    AMP2_EN_Write(0);
                    AMP3_EN_Write(0);
                    break;
                    
                case SENSOR_LACTATE:
                    AMP1_EN_Write(0);
                    AMP2_EN_Write(1);
                    AMP3_EN_Write(0);
                    break;
                    
                case SENSOR_URIC_ACID:
                    AMP1_EN_Write(0);
                    AMP2_EN_Write(0);
                    AMP3_EN_Write(1);
                    break;
            }
            
            // ✅ 记录时间戳，不用CyDelay
            g_meas_start_time = mainTimer;
            g_meas_state = MEAS_WAIT_STABLE;
            break;
            
        case MEAS_WAIT_STABLE:
            // ✅ 检查时间戳，不阻塞
            if((mainTimer - g_meas_start_time) >= 1)  // 等待1秒（可调整）
            {
                g_meas_state = MEAS_READ_FIFO;
            }
            // 否则继续等待，但不阻塞主循环
            break;
            
        case MEAS_READ_FIFO:
            // ✅ 读取FIFO（快速，不阻塞）
            fifosta = AD5940_ReadReg(0x2084);
            data_count = fifosta & 0x7FF;
            
            if(data_count > 0)
            {
                fifo_data = AD5940_ReadReg(0x2088);
                adc_code = fifo_data & 0xFFFF;
                
                // 转换为电流
                float rtia = 10000.0;
                float vref = 1.82;
                float pga_gain = 1.5;
                
                current_nA = ADCCode_to_Current_nA(adc_code, rtia, vref, pga_gain);
                
                // 保存结果
                switch(g_current_sensor)
                {
                    case SENSOR_GLUCOSE:
                        sensorData.current_glucose_nA = current_nA;
                        sensorData.glucose = ConvertCurrentToConcentration(current_nA, SENSOR_GLUCOSE);
                        break;
                        
                    case SENSOR_LACTATE:
                        sensorData.current_lactate_nA = current_nA;
                        sensorData.lactate = ConvertCurrentToConcentration(current_nA, SENSOR_LACTATE);
                        break;
                        
                    case SENSOR_URIC_ACID:
                        sensorData.current_uric_nA = current_nA;
                        sensorData.uric_acid = ConvertCurrentToConcentration(current_nA, SENSOR_URIC_ACID);
                        break;
                }
            }
            
            g_meas_state = MEAS_NEXT_SENSOR;
            break;
            
        case MEAS_NEXT_SENSOR:
            // ✅ 切换到下一个传感器
            g_current_sensor++;
            if(g_current_sensor >= SENSOR_COUNT)
            {
                g_current_sensor = SENSOR_GLUCOSE;
                
                // 温度补偿
                float temp_factor = 1.0 + 0.03 * (sensorData.temperature - 37.0);
                sensorData.glucose *= temp_factor;
                sensorData.lactate *= temp_factor;
                sensorData.uric_acid *= temp_factor;
                
                sensorData.timestamp = mainTimer;
                
                g_meas_state = MEAS_COMPLETE;
            }
            else
            {
                g_meas_state = MEAS_SWITCH_CHANNEL;
            }
            break;
            
        case MEAS_COMPLETE:
            // 测量完成，回到空闲
            g_meas_state = MEAS_IDLE;
            break;
    }
}

/*******************************************************************************
* Function Name: LowPowerImplementation
********************************************************************************
* Summary:
*   低功耗实现
*******************************************************************************/
static void LowPowerImplementation(void)
{
    CYBLE_LP_MODE_T bleMode;
    uint8 interruptStatus;
    
    if((CyBle_GetState() == CYBLE_STATE_ADVERTISING) || 
       (CyBle_GetState() == CYBLE_STATE_CONNECTED))
    {
        bleMode = CyBle_EnterLPM(CYBLE_BLESS_DEEPSLEEP);
        interruptStatus = CyEnterCriticalSection();
        
        if(bleMode == CYBLE_BLESS_DEEPSLEEP)
        {
            if((CyBle_GetBleSsState() == CYBLE_BLESS_STATE_ECO_ON) || 
               (CyBle_GetBleSsState() == CYBLE_BLESS_STATE_DEEPSLEEP))
            {
                CySysPmDeepSleep();
            }
        }
        else
        {
            if(CyBle_GetBleSsState() != CYBLE_BLESS_STATE_EVENT_CLOSE)
            {
                CySysPmSleep();
            }
        }
        
        CyExitCriticalSection(interruptStatus);
    }
}

void AD5941_HardReset(void)
{
    AD5940_RST_Write(0);
    CyDelay(20);          // ≥10ms
    AD5940_RST_Write(1);
    CyDelay(100);         // ≥50ms，给足
}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*   主函数
*******************************************************************************/
// ========== 在main.c文件开头添加这些全局变量 ==========

static uint32_t g_test_adiid = 0;
static uint32_t g_test_chipid = 0;
static uint32_t g_test_reg_0908 = 0;
static uint32_t g_test_afecon = 0;
static uint32_t g_test_fifocon = 0;
static uint16_t g_test_fifo_count = 0;
static uint8_t g_test_done = 0;

// 状态机定义
typedef enum {
    INIT_IDLE = 0,
    INIT_RESET,
    INIT_CHECK_ID,
    INIT_WRITE_REGS,
    INIT_VERIFY_REG,      // ← 新增：验证寄存器写入
    INIT_CHECK_AFE,       // ← 新增：检查AFE状态
    INIT_CHECK_FIFO,      // ← 新增：检查FIFO状态
    INIT_CONFIG_APP,
    INIT_COMPLETE,
    INIT_CONFIG_LPTIA,
    INIT_CONFIG_ADC,
    INIT_CONFIG_FIFO_EN,
    INIT_START_MEASUREMENT
} InitState_t;

static InitState_t g_init_state = INIT_IDLE;
static uint8_t g_reg_index = 0;

// 寄存器表
const struct {
    uint16_t reg_addr;
    uint32_t reg_data;
} g_RegTable[] = {
    {0x0908, 0x02c9},
    {0x0c08, 0x206C},
    {0x21F0, 0x0010},
    {0x0410, 0x02c9},
    {0x0A28, 0x0009},
    {0x238c, 0x0104},
    {0x0a04, 0x4859},
    {0x0a04, 0xF27B},
    {0x0a00, 0x8009},
    {0x22F0, 0x0000},
    {0x2230, 0xDE87A5AF},
    {0x2250, 0x103F},
    {0x22B0, 0x203C},
    {0x2230, 0xDE87A5A0},
};

#define REG_TABLE_SIZE (sizeof(g_RegTable)/sizeof(g_RegTable[0]))
#define REG_AFE_FIFO_STA      0x2084

// ========== main函数 ==========

int main()
{
    CyGlobalIntEnable;
    static uint32_t last_send_time = 0;  // 添加到main函数开头
    
    Disconnect_LED_Write(LED_OFF);
    Advertising_LED_Write(LED_OFF);
    
    printf("\n*** SYSTEM STARTUP ***\n");
    
    apiResult = CyBle_Start(AppCallBack);
    
    DRUG_EN_1_Write(0);
    STIM_EN_A_Write(0);
    AMP1_EN_Write(0);
    AMP2_EN_Write(1);
    AMP3_EN_Write(0);
    
    CySysWdtSetInterruptCallback(CY_SYS_WDT_COUNTER2, Timer_Interrupt);
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);
    
    printf("[INFO] Entering main loop...\n");
    
    while(1)
    {
        // BLE事件处理
        CyBle_ProcessEvents();
        
        // ✅ 测量状态机（非阻塞）
        if(g_init_state == INIT_COMPLETE && g_test_done == 1)
        {
            // 触发测量
            if(measurementFlag && g_meas_state == MEAS_IDLE)
            {
                measurementFlag = 0;
                g_meas_state = MEAS_SWITCH_CHANNEL;
                g_current_sensor = SENSOR_GLUCOSE;
            }
            
            // 执行测量（非阻塞）
            ProcessMeasurement();
        }
        
        // ✅ 发送状态到手机（详细版本）
         if(measurementFlag)
        {
            measurementFlag = 0;
            
            // 测量所有传感器
            MeasureAllSensorsWithCurrent();
            
            // 每3秒发送一次数据（自动刷新）
            if((mainTimer - lastSendTime) >= SEND_INTERVAL)
            {
                lastSendTime = mainTimer;
                
                if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
                {
                    SendAllSensorDataViaBLE();  // 使用新的发送函数
                }
            }
            
        }
        
        // Flash写入
        if(cyBle_pendingFlashWrite != 0u)
        {
            apiResult = CyBle_StoreBondingData(0u);
        }
    }
}
/*******************************************************************************
* Function Name: StartAdvertisement
********************************************************************************
* Summary:
*   启动BLE广播
*******************************************************************************/
void StartAdvertisement(void)
{
    uint16 i;
    CYBLE_GAP_BD_ADDR_T localAddr;
    
    apiResult = CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
    if(apiResult != CYBLE_ERROR_OK)
    {
        DBG_PRINTF("广播启动失败: %d\r\n", apiResult);
    }
    else
    {
        DBG_PRINTF("开始广播, 地址: ");
        localAddr.type = 0u;
        CyBle_GetDeviceAddress(&localAddr);
        for(i = CYBLE_GAP_BD_ADDR_SIZE; i > 0u; i--)
        {
            DBG_PRINTF("%2.2x", localAddr.bdAddr[i-1]);
        }
        DBG_PRINTF("\r\n");
    }
}

/* [] END OF FILE */