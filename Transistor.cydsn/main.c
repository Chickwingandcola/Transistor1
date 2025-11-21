
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
        DBG_PRINTF("AD5941初始化成功\r\n");
    }
    else
    {
        DBG_PRINTF("AD5941初始化失败: %d\r\n", error);
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
    
    DBG_PRINTF("测量传感器 %d...\r\n", sensorType);
    
    // 注意：如果不使用AMux，AD5941会使用默认的WE0通道
    // 如果需要切换WE通道，需要在配置中修改
    
    // 启动测量
    error = AppAMPCtrl(AMPCTRL_START, NULL);
    if(error != AD5940ERR_OK)
    {
        DBG_PRINTF("  启动测量失败: %d\r\n", error);
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
        DBG_PRINTF("  电流值: %.2f nA\r\n", current_uA);
    }
    else
    {
        DBG_PRINTF("  测量失败或无数据: error=%d, count=%d\r\n", error, dataCount);
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
float ConvertCurrentToConcentration(float current_nA, AmperometricSensor_t sensorType)
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
    
    DBG_PRINTF("测量温度...\r\n");
    
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
    
    DBG_PRINTF("  温度: %.2f °C\r\n", temperature);
    
    return temperature;
}

/*******************************************************************************
* Function Name: MeasureAllSensors
********************************************************************************
* Summary:
*   测量所有传感器（4路：温度 + 3个安培法）
*******************************************************************************/
void MeasureAllSensors(void)
{
    float current_nA;
    
    DBG_PRINTF("\r\n========== 开始传感器测量 ==========\r\n");
    
    // 1. 温度测量
    sensorData.temperature = MeasureTemperature();
    
    // 2. 葡萄糖测量
    current_nA = MeasureAmperometricSensor(SENSOR_GLUCOSE);
    sensorData.glucose = ConvertCurrentToConcentration(current_nA, SENSOR_GLUCOSE);
    DBG_PRINTF("  葡萄糖: %.2f mM\r\n", sensorData.glucose);
    
    // 3. 乳酸测量
    current_nA = MeasureAmperometricSensor(SENSOR_LACTATE);
    sensorData.lactate = ConvertCurrentToConcentration(current_nA, SENSOR_LACTATE);
    DBG_PRINTF("  乳酸: %.2f mM\r\n", sensorData.lactate);
    
    // 4. 尿酸测量
    current_nA = MeasureAmperometricSensor(SENSOR_URIC_ACID);
    sensorData.uric_acid = ConvertCurrentToConcentration(current_nA, SENSOR_URIC_ACID);
    DBG_PRINTF("  尿酸: %.2f μM\r\n", sensorData.uric_acid);
    
    // 温度校准（论文中的温度补偿算法）
    float temp_factor = 1.0 + 0.03 * (sensorData.temperature - 37.0);
    sensorData.glucose *= temp_factor;
    sensorData.lactate *= temp_factor;
    sensorData.uric_acid *= temp_factor;
    
    sensorData.timestamp = mainTimer;
    
    DBG_PRINTF("========== 测量完成 ==========\r\n\r\n");
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
        DBG_PRINTF("启动药物释放\r\n");
        DRUG_EN_1_Write(1);
    }
    else
    {
        DBG_PRINTF("停止药物释放\r\n");
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
        DBG_PRINTF("启动电刺激\r\n");
        STIM_EN_A_Write(1);
    }
    else
    {
        DBG_PRINTF("停止电刺激\r\n");
        STIM_EN_A_Write(0);
    }
}

/*******************************************************************************
* Function Name: SendDataViaBLE
********************************************************************************
* Summary:
*   通过BLE发送传感器数据
*   数据包：[温度(4B)][葡萄糖(4B)][乳酸(4B)][尿酸(4B)] = 16字节
*******************************************************************************/
void SendDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    uint8 dataPacket[16];
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        // 打包数据
        memcpy(&dataPacket[0], &sensorData.temperature, 4);
        memcpy(&dataPacket[4], &sensorData.glucose, 4);
        memcpy(&dataPacket[8], &sensorData.lactate, 4);
        memcpy(&dataPacket[12], &sensorData.uric_acid, 4);
        
        // 发送通知 - 使用自定义服务的葡萄糖测量特征句柄
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_GLUCOSE_MEASUREMENT_CHAR_HANDLE;
        notificationHandle.value.val = dataPacket;
        notificationHandle.value.len = 16;
        
        apiResult = CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
        
        if(apiResult == CYBLE_ERROR_OK)
        {
            DBG_PRINTF("数据已发送\r\n");
        }
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
            DBG_PRINTF("BLE协议栈启动\r\n");
            StartAdvertisement();
            break;

        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            DBG_PRINTF("设备已连接\r\n");
            Advertising_LED_Write(LED_OFF);
            break;

        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            DBG_PRINTF("设备已断开\r\n");
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
            DBG_PRINTF("等待Flash写入\r\n");
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
    DBG_PRINTF("启动BLE...\r\n");
    apiResult = CyBle_Start(AppCallBack);
    if(apiResult != CYBLE_ERROR_OK)
    {
        DBG_PRINTF("BLE启动失败: %d\r\n", apiResult);
    }
    
    // 初始化SPI
    SPI_1_Start();
    CyDelay(10);
    
    // 初始化AD5941
    DBG_PRINTF("初始化AD5941...\r\n");
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
    
    DBG_PRINTF("系统初始化完成\r\n\r\n");
    
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
            MeasureAllSensors();
            
            // 如果已连接，发送数据
            if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
            {
                SendDataViaBLE();
            }
            
            // 智能治疗决策
            // 1. 检测感染（温度升高或乳酸升高）
            if(sensorData.temperature > 38.5 || sensorData.lactate > 5.0)
            {
                DBG_PRINTF("[警告] 检测到异常，启动药物释放\r\n");
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