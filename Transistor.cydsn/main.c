
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
    
    // ====================================================================
    // 步骤 1: 硬件复位与引脚状态强制初始化
    // ====================================================================
    printf("[INIT] Step 1: Resetting hardware...\r\n");
    
    // 1.1 确保SPI总线处于空闲状态 (Mode 0: SCLK=0, CS=1)
    // 防止引脚之前的状态导致芯片误判
    AD5940_CS_Write(1);   
    AD5940_SCLK_Write(0); 
    AD5940_MOSI_Write(0); 
    CyDelay(10);

    // 1.2 执行硬件复位
    AD5940_RST_Write(0);  // 拉低复位
    CyDelay(10);          // 保持10ms
    AD5940_RST_Write(1);  // 释放复位
    CyDelay(100);         // 等待芯片内部加载 (Boot time)
    
    printf("[INIT] Hardware Reset complete.\r\n");

    // ====================================================================
    // 步骤 2: 唤醒 SPI 接口 (关键步骤！)
    // ====================================================================
    // AD5940 复位后处于休眠状态，需要一个 CS 下降沿来唤醒 SPI 接口
    printf("[INIT] Step 2: Waking up SPI interface...\r\n");
    
    AD5940_CS_Write(0);   // 拉低 CS 唤醒
    CyDelayUs(100);       // 保持一小段时间
    AD5940_CS_Write(1);   // 拉高 CS
    CyDelay(10);          // 等待接口准备就绪

    // 初始化 MCU SPI 资源变量
    AD5940_MCUResourceInit(NULL);

    // ====================================================================
    // 步骤 3: 寄存器通信测试 (ID 检查)
    // ====================================================================
    printf("\r\n[DEBUG] Verifying SPI Communication...\r\n");
    
    uint32_t adiid = 0;
    uint32_t chipid = 0;
    uint8_t id_valid = 0;

    // 尝试读取3次，排除偶发的上电不稳定
    for(int attempt = 1; attempt <= 3; attempt++)
    {
        adiid = AD5940_ReadReg(REG_AFECON_ADIID);
        chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
        
        printf("  [Attempt %d] ADIID: 0x%08lX, CHIPID: 0x%08lX\r\n", attempt, adiid, chipid);

        // 判定标准：ADIID 应为 0x4144, CHIPID 应为 0x5502 (AD5941) 或 0x5501 (AD5940)
        if(adiid == 0x4144 && (chipid == 0x5501 || chipid == 0x5502))
        {
            id_valid = 1;
            printf("  ✅ Communication Success!\r\n");
            break;
        }
        else
        {
            // 如果读到全是 0，可能是 MISO 没连好或者芯片没电
            // 如果读到全是 F，可能是 MISO 短路到 VCC
            if(adiid == 0x0000) printf("     -> Warning: Read 0x00. Check MISO Connection or Power.\r\n");
            if(adiid == 0xFFFF) printf("     -> Warning: Read 0xFF. Check if MISO is shorted to VDD.\r\n");
            
            // 失败重试前再次尝试唤醒
            AD5940_CS_Write(0); CyDelayUs(20); AD5940_CS_Write(1);
            CyDelay(50);
        }
    }

    if(!id_valid)
    {
        printf("[ERROR] SPI Communication Failed. Halting Initialization.\r\n");
        // 这里可以选择 return，或者继续尝试(有时候是 glitch)
        // return; 
    }

    // ====================================================================
    // 步骤 4: 执行 ADI 库初始化 (AD5940_Initialize)
    // ====================================================================
    // 这个函数会向芯片写入大量的校准数据和默认配置
    printf("\r\n[INIT] Step 4: Running ADI Library Init (Table 14)...\r\n");
    AD5940_Initialize(); 
    printf("[INIT] Library Init complete.\r\n");
    
    // 再次等待 AFE 稳定
    CyDelay(100); 

    // ====================================================================
    // 步骤 5: 配置安培法 (Amperometric) 参数
    // ====================================================================
    printf("[INIT] Step 5: Configuring Application Parameters...\r\n");
    
    AppAMPGetCfg(&pAmpCfg);
    if(pAmpCfg == NULL)
    {
        printf("[ERROR] pAmpCfg is NULL!\r\n");
        return;
    }
    
    AD5940_LPModeClkS(LPMODECLK_LFOSC);

    // --- 基础配置 ---
    pAmpCfg->bParaChanged = bTRUE;
    pAmpCfg->SeqStartAddr = 0;
    pAmpCfg->MaxSeqLen = 512;
    pAmpCfg->SeqStartAddrCal = 0;
    pAmpCfg->MaxSeqLenCal = 512;
    
    // --- 时钟与电源 ---
    pAmpCfg->SysClkFreq = 16000000.0;
    pAmpCfg->WuptClkFreq = 32000.0;
    pAmpCfg->AdcClkFreq = 16000000.0;
    pAmpCfg->PwrMod = AFEPWR_LP; // 低功耗

    // --- 测量参数 ---
    pAmpCfg->AmpODR = 10.0;      // 采样率 10Hz
    pAmpCfg->NumOfData = -1;     // -1 表示无限连续测量
    pAmpCfg->FifoThresh = 4;     // FIFO 阈值

    // --- 电化学参数 (根据你的代码) ---
    pAmpCfg->RcalVal = 10000.0;     // 10k 校准电阻
    pAmpCfg->ADCRefVolt = 1.82;     // Vref 1.82V
    pAmpCfg->ExtRtia = bFALSE;      // 使用内部 RTIA

    // --- LPTIA (低功耗跨阻放大器) ---
    pAmpCfg->LptiaRtiaSel = LPTIARTIA_10K; // 反馈电阻 10k
    pAmpCfg->LpTiaRf = LPTIARF_1M;         // 滤波电阻
    pAmpCfg->LpTiaRl = LPTIARLOAD_100R;    // 负载电阻
    
    // --- 偏置电压 ---
    pAmpCfg->Vzero = 1100.0;      // Vzero = 1.1V
    pAmpCfg->SensorBias = 0.0;    // Vbias = 0V (Sensor = Vzero)

    // --- ADC ---
    pAmpCfg->ADCPgaGain = ADCPGA_1P5;
    pAmpCfg->ADCSinc3Osr = ADCSINC3OSR_4;
    pAmpCfg->ADCSinc2Osr = ADCSINC2OSR_178;
    pAmpCfg->DataFifoSrc = FIFOSRC_SINC3;

    // 清除状态
    pAmpCfg->AMPInited = bFALSE;
    pAmpCfg->StopRequired = bFALSE;
    pAmpCfg->FifoDataCount = 0;

    // ====================================================================
    // 步骤 6: 启动应用
    // ====================================================================
    printf("[INIT] Step 6: Calling AppAMPInit...\r\n");
    error = AppAMPInit(ampBuffer, 512);
    
    if(error == AD5940ERR_OK)
    {
        printf("[OK] AD5941 System Initialized Successfully.\r\n");
        printf("     AMPInited Flag: %d\r\n", pAmpCfg->AMPInited);
    }
    else
    {
        printf("[ERROR] AppAMPInit failed with error code: %d\r\n", error);
    }
}



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

/*

// 发送葡萄糖数据（浓度 + 电流）
void SendGlucoseDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    static char dataString[30];
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
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

*/

void SendGlucoseDataViaBLE(void)
{

}


// 发送乳酸数据（改为诊断日志输出）
void SendLactateDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    static char dataString[40];
    static uint8 diagStep = 0;
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        switch(diagStep % 3)
        {
            case 0:
                // 测试: 读取ADIID (应为0x4144)
                {
                    uint32_t adiid = AD5940_ReadReg(REG_AFECON_ADIID);
                    sprintf(dataString, "ADIID:%04X", (unsigned int)adiid);
                    // 预期: ADIID:4144
                }
                break;
                
            case 1:
                // 测试: 读取CHIPID (应为0x5502)
                {
                    uint32_t chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
                    sprintf(dataString, "CHIP:%04X", (unsigned int)chipid);
                    // 预期: CHIP:5502
                }
                break;
                
            case 2:
                // 测试: 同时读两个
                {
                    uint32_t adiid = AD5940_ReadReg(REG_AFECON_ADIID);
                    uint32_t chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
                    sprintf(dataString, "ID:%04X-%04X", 
                            (unsigned int)adiid, (unsigned int)chipid);
                    // 预期: ID:4144-5502
                }
                break;
        }
        
        diagStep++;
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
        notificationHandle.value.val = (uint8*)dataString;
        notificationHandle.value.len = strlen(dataString);
        CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
    }
}

// 发送温度数据（改为芯片状态诊断）
// void SendTemperatureViaBLE(void)
// {
//     CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
//     static char tempString[40];
    
//     if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
//     {
//         // 读取 SPI 寄存器检查芯片状态
//         uint32 regValue = AD5940_ReadReg(REG_AFE_AFECON);
//         uint8 initStatus = 0;
        
//         // 检查 pAmpCfg 指针是否有效
//         if(pAmpCfg != NULL)
//         {
//             initStatus = (uint8)pAmpCfg->AMPInited;
//         }
        
//         // 显示 SPI 寄存器值 + 初始化状态
//         sprintf(tempString, "SPI:0x%lX Init:%d", 
//                 (unsigned long)regValue,
//                 (int)initStatus);
        
//         notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
//         notificationHandle.value.val = (uint8*)tempString;
//         notificationHandle.value.len = strlen(tempString);
        
//         CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
//     }
// }

void SendTemperatureViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    static char tempString[60];
    static uint8 testStep = 0;
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        switch(testStep % 5)
        {
            case 0:
                // 🔧 测试1：尝试设置SCLK为0
                AD5940_SCLK_Write(0);
                CyDelay(10);
                sprintf(tempString, "SCLK=0, Read:%d", AD5940_SCLK_Read());
                break;
                
            case 1:
                // 🔧 测试2：尝试设置SCLK为1
                AD5940_SCLK_Write(1);
                CyDelay(10);
                sprintf(tempString, "SCLK=1, Read:%d", AD5940_SCLK_Read());
                break;
                
            case 2:
                // 🔧 测试3：检查CS和MOSI
                sprintf(tempString, "CS:%d MOSI:%d MISO:%d", 
                        AD5940_CS_Read(),
                        AD5940_MOSI_Read(),
                        AD5940_MISO_Read());
                break;
                
            case 3:
                // 🔧 测试4：快速翻转SCLK 10次
                for(int i=0; i<10; i++) {
                    AD5940_SCLK_Write(1);
                    CyDelayUs(10);
                    AD5940_SCLK_Write(0);
                    CyDelayUs(10);
                }
                sprintf(tempString, "SCLK toggled 10x");
                break;
                
            case 4:
                // 🔧 测试5：读取引脚配置寄存器（如果可能）
                // 这需要查PSoC寄存器，暂时显示基本状态
                sprintf(tempString, "Check TopDesign pin cfg");
                break;
        }
        
        testStep++;
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
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
    INIT_COMPLETE
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
    
    Disconnect_LED_Write(LED_OFF);
    Advertising_LED_Write(LED_OFF);
    
    printf("\n*** SYSTEM STARTUP ***\n");
    
    apiResult = CyBle_Start(AppCallBack);
    
    DRUG_EN_1_Write(0);
    STIM_EN_A_Write(0);
    AMP1_EN_Write(0);
    AMP2_EN_Write(0);
    AMP3_EN_Write(0);
    
    CySysWdtSetInterruptCallback(CY_SYS_WDT_COUNTER2, Timer_Interrupt);
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);
    
    printf("[INFO] Entering main loop...\n");
    
    while(1)
    {
        // ✅ 第一优先级：处理BLE事件
        CyBle_ProcessEvents();
        
        // ✅ 状态机方式初始化AD5940
        if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
        {
            switch(g_init_state)
            {
                case INIT_IDLE:
                    g_init_state = INIT_RESET;
                    break;
                    
                case INIT_RESET:
                    printf("[INIT] Resetting AD5940...\n");
                    AD5940_RST_Write(0);
                    CyDelay(10);
                    AD5940_RST_Write(1);
                    CyDelay(100);
                    
                    AD5940_CS_Write(1);
                    AD5940_SCLK_Write(0);
                    CyDelay(20);
                    
                    g_init_state = INIT_CHECK_ID;
                    break;
                    
                case INIT_CHECK_ID:
                    printf("[INIT] Checking ID...\n");
                    {
                        uint32_t adiid = AD5940_ReadReg(REG_AFECON_ADIID);
                        uint32_t chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
                        
                        // ✅ 保存ID用于显示
                        g_test_adiid = adiid;
                        g_test_chipid = chipid;
                        
                        if(adiid == 0x4144 && (chipid == 0x5501 || chipid == 0x5502))
                        {
                            printf("[OK] ID verified\n");
                            g_reg_index = 0;
                            g_init_state = INIT_WRITE_REGS;
                        }
                        else
                        {
                            printf("[ERROR] ID check failed\n");
                            g_test_done = 2;
                            g_init_state = INIT_COMPLETE;
                        }
                    }
                    break;
                    
                case INIT_WRITE_REGS:
                    // ✅ 每次循环只写1个寄存器
                    if(g_reg_index < REG_TABLE_SIZE)
                    {
                        AD5940_WriteReg(g_RegTable[g_reg_index].reg_addr, 
                                       g_RegTable[g_reg_index].reg_data);
                        
                        printf("[INIT] Wrote reg %d/%d\n", g_reg_index+1, REG_TABLE_SIZE);
                        
                        g_reg_index++;
                    }
                    else
                    {
                        printf("[INIT] All registers written\n");
                        g_init_state = INIT_VERIFY_REG;  // ← 改为验证寄存器
                    }
                    break;
                
                // ✅ 新增：验证寄存器写入
                case INIT_VERIFY_REG:
                    printf("[INIT] Verifying register write...\n");
                    {
                        // 读回第一个寄存器验证
                        g_test_reg_0908 = AD5940_ReadReg(0x0908);
                        printf("Reg 0x0908: 0x%08lX (expect 0x02C9)\n", g_test_reg_0908);
                        
                        g_init_state = INIT_CHECK_AFE;
                    }
                    break;
                
                // ✅ 新增：检查AFE状态
                case INIT_CHECK_AFE:
                    printf("[INIT] Checking AFE status...\n");
                    {
                        // 读取AFE配置寄存器
                        g_test_afecon = AD5940_ReadReg(REG_AFE_AFECON);
                        printf("AFECON: 0x%08lX\n", g_test_afecon);
                        
                        g_init_state = INIT_CHECK_FIFO;
                    }
                    break;
                
                // ✅ 新增：检查FIFO状态
                case INIT_CHECK_FIFO:
                    printf("[INIT] Checking FIFO (using direct addresses)...\n");
                    {
                        // 测试1: 读取 0x2080 (FIFOCON)
                        uint32_t addr_2080 = AD5940_ReadReg(0x2080);
                        
                        // 测试2: 读取 0x2084 (FIFOCNTSTA)
                        uint32_t addr_2084 = AD5940_ReadReg(0x2084);
                        
                        // 测试3: 读取 0x2088 (DATAFIFORD - 但这会弹出FIFO数据)
                        // uint32_t addr_2088 = AD5940_ReadReg(0x2088);  // 暂时不读
                        
                        printf("Addr 0x2080: 0x%08lX\n", addr_2080);
                        printf("Addr 0x2084: 0x%08lX\n", addr_2084);
                        
                        // 保存用于BLE显示
                        g_test_fifocon = addr_2080;
                        
                        // 解析 0x2084 的数据计数字段
                        g_test_fifo_count = addr_2084 & 0x7FF;  // bit 0-10
                        
                        g_init_state = INIT_CONFIG_APP;
                    }
                    break;
                    
                case INIT_CONFIG_APP:
                    printf("[INIT] Final verification...\n");
                    {
                        // 判断初始化是否成功
                        if((g_test_reg_0908 & 0xFFFF) == 0x02C9)
                        {
                            printf("[OK] All checks passed\n");
                            g_test_done = 1;
                        }
                        else
                        {
                            printf("[FAIL] Verification failed\n");
                            g_test_done = 2;
                        }
                        
                        g_init_state = INIT_COMPLETE;
                    }
                    break;
                    
                case INIT_COMPLETE:
                    // 初始化完成，什么都不做
                    break;
            }
        }
        
        // ✅ 发送状态到手机（详细版本）
        if(measurementFlag)
        {
            measurementFlag = 0;
            
            if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
            {
                CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
                static char testString[40];
                static uint8_t display_step = 0;  // 用于轮流显示不同信息
                
                switch(g_init_state)
                {
                    case INIT_IDLE:
                        sprintf(testString, "Idle...");
                        break;
                        
                    case INIT_RESET:
                        sprintf(testString, "Resetting...");
                        break;
                        
                    case INIT_CHECK_ID:
                        sprintf(testString, "Checking ID...");
                        break;
                        
                    case INIT_WRITE_REGS:
                        sprintf(testString, "Writing %d/%d", g_reg_index, REG_TABLE_SIZE);
                        break;
                        
                    case INIT_VERIFY_REG:
                        sprintf(testString, "Verifying...");
                        break;
                        
                    case INIT_CHECK_AFE:
                        sprintf(testString, "Checking AFE...");
                        break;
                        
                    case INIT_CHECK_FIFO:
                        sprintf(testString, "Checking FIFO...");
                        break;
                        
                    case INIT_CONFIG_APP:
                        sprintf(testString, "Final check...");
                        break;
                        
                    case INIT_COMPLETE:
                        // ✅ 轮流显示不同的验证结果
                        if(g_test_done == 1)
                        {
                            // 每次显示不同的信息
                            switch(display_step % 6)
                            {
                                case 0:
                                    sprintf(testString, "CNT:%d", g_test_fifo_count);
                                    break;
                                case 1:
                                    sprintf(testString, "ADIID:%04lX", g_test_adiid);
                                    break;
                                case 2:
                                    sprintf(testString, "CHIP:%04lX", g_test_chipid);
                                    break;
                                case 3:
                                    sprintf(testString, "R0908:%04lX", g_test_reg_0908 & 0xFFFF);
                                    break;
                                case 4:
                                    sprintf(testString, "AFE:%08lX", g_test_afecon);
                                    break;
                                case 5:
                                    sprintf(testString, "2080:%08lX", g_test_fifocon);  // FIFOCON值
                                    break;
                                case 6:
                                    sprintf(testString, "CNT:%d", g_test_fifo_count);   // 数据计数
                                    break;
                            }
                            display_step++;
                        }
                        else if(g_test_done == 2)
                        {
                            sprintf(testString, "Init FAIL");
                        }
                        else
                        {
                            sprintf(testString, "Waiting...");
                        }
                        break;
                }
                
                notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
                notificationHandle.value.val = (uint8*)testString;
                notificationHandle.value.len = strlen(testString);
                CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
            }
        }
        
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