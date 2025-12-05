
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
#include "Amperometric.h"

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
* Function Name: AD5941_Initialize
********************************************************************************
* Summary:
*   初始化AD5941电化学前端芯片 - 修复后的版本
*******************************************************************************/
void AD5941_Initialize(void)
{
    AD5940Err error;
    
    // 步骤1: 复位AD5940
    AD5940_RST_Write(0);
    CyDelay(10);
    AD5940_RST_Write(1);
    CyDelay(100);
    
    // 步骤2: 初始化MCU资源（SPI通信）
    AD5940_MCUResourceInit(NULL);
    
    // 步骤3: 获取配置指针
    AppAMPGetCfg(&pAmpCfg);
    
    // 步骤4: 配置安培法测量参数（根据Amperometric.h结构体）
    pAmpCfg->bParaChanged = bTRUE;
    pAmpCfg->SeqStartAddr = 0;
    pAmpCfg->MaxSeqLen = 512;
    pAmpCfg->SeqStartAddrCal = 0;
    pAmpCfg->MaxSeqLenCal = 512;
    
    pAmpCfg->ReDoRtiaCal = bFALSE;
    pAmpCfg->SysClkFreq = 16000000.0;   // 16MHz系统时钟
    pAmpCfg->WuptClkFreq = 32000.0;     // 32kHz唤醒时钟
    pAmpCfg->AdcClkFreq = 16000000.0;   // 16MHz ADC时钟
    pAmpCfg->FifoThresh = 4;            // FIFO阈值
    pAmpCfg->AmpODR = 10.0;             // 10Hz采样率
    pAmpCfg->NumOfData = -1;            // 连续测量
    
    // 电化学参数
    pAmpCfg->RcalVal = 10000.0;         // 10kΩ校准电阻
    pAmpCfg->ADCRefVolt = 1.82;         // ADC参考电压1.82V
    pAmpCfg->PwrMod = AFEPWR_LP;        // 低功耗模式
    
    // ADC配置
    pAmpCfg->ADCPgaGain = ADCPGA_1P5;   // PGA增益1.5
    pAmpCfg->ADCSinc3Osr = ADCSINC3OSR_4;
    pAmpCfg->ADCSinc2Osr = ADCSINC2OSR_178;
    pAmpCfg->DataFifoSrc = FIFOSRC_SINC3;
    
    // LPTIA配置（跨阻放大器）
    pAmpCfg->LptiaRtiaSel = LPTIARTIA_10K;  // 10kΩ内部RTIA
    pAmpCfg->LpTiaRf = LPTIARF_1M;          // 1MΩ滤波电阻
    pAmpCfg->LpTiaRl = LPTIARLOAD_100R;     // 100Ω负载电阻
    
    // 偏置电压
    pAmpCfg->Vzero = 1100.0;            // SE0偏置电压1.1V
    pAmpCfg->SensorBias = 0.0;          // 传感器偏置0V（普鲁士蓝介体）
    
    // 外部RTIA（不使用）
    pAmpCfg->ExtRtia = bFALSE;
    pAmpCfg->ExtRtiaVal = 0.0;
    
    pAmpCfg->AMPInited = bFALSE;
    pAmpCfg->StopRequired = bFALSE;
    pAmpCfg->FifoDataCount = 0;
    
    // 步骤5: 初始化应用（使用缓冲区）
    error = AppAMPInit(ampBuffer, 512);
    
    if(error == AD5940ERR_OK)
    {
    }
    else
    {
    }
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
    
    
    // 方法 1: 使用你已有的 AppAMP 框架
    
    // 1. 配置 AD5940 为安培法测量模式
    AppAMPGetCfg(&pAmpCfg);
    
    // 根据传感器类型设置工作电极（如果有多路复用）
    // 注意：你的硬件可能使用 AMP1_EN, AMP2_EN, AMP3_EN 来选择通道
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
* Function Name: MeasureAllSensorsWithCurrent
********************************************************************************
* Summary:
*   测量所有传感器 - 包含实际电流值
*******************************************************************************/
void MeasureAllSensorsWithCurrent(void)
{
 
    // 1. 温度测量
    sensorData.temperature = MeasureTemperature();
    
    // 2. 葡萄糖测量

    
    // 方法 A: 使用 AD5940 读取（推荐）
   sensorData.current_glucose_nA = ReadCurrentFromAD5940(SENSOR_GLUCOSE);
    
    // 方法 B: 使用模拟值测试（测试用）
    //sensorData.current_glucose_nA = ReadCurrentFromSourceMeter_Simulated(SENSOR_GLUCOSE);
    
    // 转换为浓度
    sensorData.glucose = ConvertCurrentToConcentration(sensorData.current_glucose_nA, SENSOR_GLUCOSE);
    
    
    // 3. 乳酸测量

     sensorData.current_lactate_nA = ReadCurrentFromAD5940(SENSOR_LACTATE); 
    //sensorData.current_lactate_nA = ReadCurrentFromSourceMeter_Simulated(SENSOR_LACTATE);
    sensorData.lactate = ConvertCurrentToConcentration(sensorData.current_lactate_nA, SENSOR_LACTATE);
    
    // 4. 尿酸测量

    sensorData.current_uric_nA = ReadCurrentFromAD5940(SENSOR_URIC_ACID);
    //sensorData.uric_acid = ConvertCurrentToConcentration(sensorData.current_uric_nA,SENSOR_URIC_ACID);
    // [修改] 切换为模拟数据
    // sensorData.current_uric_acid_nA = ReadCurrentFromAD5940(SENSOR_URIC_ACID);
    //sensorData.current_uric_nA = ReadCurrentFromSourceMeter_Simulated(SENSOR_URIC_ACID); 
    // [增加] 电流换算到浓度
    sensorData.uric_acid = ConvertCurrentToConcentration(sensorData.current_uric_nA, SENSOR_URIC_ACID);    

    

    
    // 6. 温度校准
    float temp_factor = 1.0 + 0.03 * (sensorData.temperature - 37.0);
    sensorData.glucose *= temp_factor;
    sensorData.lactate *= temp_factor;
    sensorData.uric_acid *= temp_factor;
    
    sensorData.timestamp = mainTimer;
    
}


/*******************************************************************************
* Function Name: ControlDrugRelease
********************************************************************************
* Summary:
*   控制药物释放（电控水凝胶）
*******************************************************************************/
void ControlDrugRelease(uint8 enable)
{
    if(enable)
    {
        DRUG_EN_1_Write(1);
    }
    else
    {
        DRUG_EN_1_Write(0);
    }
}

/*******************************************************************************
* Function Name: ControlElectricalStimulation
********************************************************************************
* Summary:
*   控制电刺激治疗
*******************************************************************************/
void ControlElectricalStimulation(uint8 enable)
{
    if(enable)
    {
        STIM_EN_A_Write(1);
    }
    else
    {
        STIM_EN_A_Write(0);
    }
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
            LowPower_LED_Write(LED_OFF);
            break;
            
        case CYBLE_EVT_GAPP_ADVERTISEMENT_START_STOP:
            if(CYBLE_STATE_DISCONNECTED == CyBle_GetState())
            {
                Advertising_LED_Write(LED_OFF);
                Disconnect_LED_Write(LED_ON);
                LowPower_LED_Write(LED_OFF);
                
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

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*   主函数
*******************************************************************************/
int main()
{
    CyGlobalIntEnable;
    
    // 初始化LED
    Disconnect_LED_Write(LED_OFF);
    Advertising_LED_Write(LED_OFF);
    LowPower_LED_Write(LED_OFF);
    
    // 初始化BLE
    apiResult = CyBle_Start(AppCallBack);
    if(apiResult != CYBLE_ERROR_OK)
    {
    }
    
    // 初始化SPI
    SPI_1_Start();
    CyDelay(10);
    
    // 初始化AD5941
    AD5941_Initialize();
    
    // 初始化控制引脚
    DRUG_EN_1_Write(0);
    STIM_EN_A_Write(0);
    AMP1_EN_Write(1);
    AMP2_EN_Write(1);
    AMP3_EN_Write(1);
    
    // 启动定时器中断
    CySysWdtSetInterruptCallback(CY_SYS_WDT_COUNTER2, Timer_Interrupt);
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);
    uint32 lastSendTime = 0;
    #define SEND_INTERVAL 3  // 每3秒刷新一次

    
    /***************************************************************************
    * 主循环
    ***************************************************************************/
    while(1)
    {
        // 处理BLE事件
        CyBle_ProcessEvents();
        
        // 低功耗管理
        LowPowerImplementation();
        
        // 每秒执行一次传感器测量
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
            
            // 智能治疗决策
            // 1. 检测感染（温度升高或乳酸升高）
            if(sensorData.temperature > 38.5 || sensorData.lactate > 5.0)
            {
                ControlDrugRelease(1);
                CyDelay(10000);  // 释放10秒（实际应该是10分钟，这里缩短用于测试）
                ControlDrugRelease(0);
            }
            
            // 2. 促进愈合（电刺激）
            if(sensorData.glucose > 10.0)
            {
                ControlElectricalStimulation(1);
            }
            else
            {
                ControlElectricalStimulation(0);
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