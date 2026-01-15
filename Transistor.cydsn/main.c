
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
    printf("[INIT] Step 1: Resetting AD5940...\n");
    AD5940_RST_Write(0);
    CyDelay(10);
    AD5940_RST_Write(1);
    CyDelay(100);
    printf("[INIT] Reset complete\n");
    
    // 步骤2: 初始化MCU资源（SPI通信）
    printf("[INIT] Step 2: Initializing MCU SPI resources...\n");
    AD5940_MCUResourceInit(NULL);
    printf("[INIT] SPI resources initialized\n");

    // ===== 新增：在 AD5940_Initialize() 之前读取寄存器 =====
    printf("\n[DEBUG] Reading registers BEFORE AD5940_Initialize():\n");
    uint32 adiid_before = AD5940_ReadReg(REG_AFECON_ADIID);
    uint32 chipid_before = AD5940_ReadReg(REG_AFECON_CHIPID);
    printf("  ADIID (before):  0x%08lX (expect 0x00004144)\n", adiid_before);
    printf("  CHIPID (before): 0x%08lX (expect 0x00005501 or 0x00005502)\n", chipid_before);
  
    
    // 步骤2.5: 执行AD5940硬件初始化 (Table 14 - 必须在复位后立即执行)
    // 这是关键步骤：写入初始化寄存器序列以激活芯片内部SPI接口和模拟前端
    printf("[INIT] Step 2.5: Running AD5940_Initialize (Table 14 sequence)...\n");
    AD5940_Initialize();
    printf("[INIT] AD5940_Initialize complete\n");

    CyDelay(100);  // 重要！等待AFE唤醒（数据手册要求≥30ms，这里用100ms确保充分）

    // ===== 新增：读取多次验证 =====
    printf("\n[DEBUG] Reading registers AFTER AD5940_Initialize() (multiple attempts):\n");
    for(int attempt = 0; attempt < 3; attempt++)
    {
        uint32 adiid = AD5940_ReadReg(REG_AFECON_ADIID);
        uint32 chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
        printf("  [Attempt %d] ADIID: 0x%08lX, CHIPID: 0x%08lX\n", 
               attempt + 1, adiid, chipid);
        
        if(adiid == 0x4144 && (chipid == 0x5501 || chipid == 0x5502))
        {
            printf("  ✅ Correct ID detected on attempt %d!\n", attempt + 1);
            break;
        }
        CyDelay(50);  // 每次尝试间隔 50ms
    }
    
    // ===== 新增：读取 AFE 状态寄存器 =====
    printf("\n[DEBUG] Reading AFE status registers:\n");
    uint32 afecon = AD5940_ReadReg(REG_AFE_AFECON);
    uint32 intcflag = AD5940_ReadReg(REG_INTC_INTCFLAG0);
    printf("  AFECON:    0x%08lX\n", afecon);
    printf("  INTCFLAG0: 0x%08lX\n", intcflag);


        // 🔍 立即检查 ID
    uint32 chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
    printf("[DEBUG] CHIPID after 100ms wait: 0x%lX\n", chipid);
    
    if(chipid != 0x5502 && chipid != 0x5501)
    {
        printf("[ERROR] Chip not responding! Got: 0x%lX\n", chipid);
        // 尝试更长的等待
        CyDelay(100);
        chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
        printf("[RETRY] CHIPID after 150ms: 0x%lX\n", chipid);
    }
    
    // 步骤3: 获取配置指针
    printf("[INIT] Step 3: Getting config pointer...\n");
    AppAMPGetCfg(&pAmpCfg);
    if(pAmpCfg == NULL)
    {
        printf("[ERROR] pAmpCfg is NULL!\n");
        return;
    }
    printf("[INIT] Config pointer obtained: %p\n", (void*)pAmpCfg);
    
    // 步骤4: 配置安培法测量参数（根据Amperometric.h结构体）
    printf("[INIT] Step 4: Configuring amperometric parameters...\n");
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
    printf("[INIT] Parameters configured\n");
    printf("[INIT] AMPInited before AppAMPInit: %d\n", (int)pAmpCfg->AMPInited);
    
    // 步骤5: 初始化应用（使用缓冲区）
    printf("[INIT] Step 5: Calling AppAMPInit...\n");
    error = AppAMPInit(ampBuffer, 512);
    printf("[INIT] AppAMPInit returned: %d\n", (int)error);
    printf("[INIT] AMPInited after AppAMPInit: %d\n", (int)pAmpCfg->AMPInited);
    
    if(error == AD5940ERR_OK)
    {
        printf("[OK] AD5941 initialization successful\n");
    }
    else
    {
        printf("[ERROR] AD5941 initialization failed: error=%d\n", (int)error);
    }
    
    // 快速ID寄存器检查（在初始化后）
    printf("\n[ID CHECK] Reading AD5941 ID registers...\n");
    uint32 id_adiid = AD5940_ReadReg(REG_AFECON_CHIPID);
    uint32 id_chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
    printf("ADIID: 0x%lX\n", (unsigned long)id_adiid);
    printf("CHIPID: 0x%lX\n", (unsigned long)id_chipid);
    if(id_adiid == 0x4144 && id_chipid == 0x5501)
    {
        printf("[OK] ID registers correct!\n");
    }
    else if(id_adiid == 0x808080 || id_chipid == 0x808080)
    {
        printf("[ERROR] Still getting 0x808080 - MISO issue persists!\n");
    }
    else
    {
        printf("[WARNING] Unexpected ID values\n");
    }
}

/*******************************************************************************
* Function Name: DiagnosticsSPI
********************************************************************************
* Summary:
*   诊断 SPI 通讯 - 增强版本，多寄存器测试
*******************************************************************************/
void DiagnosticsSPI(void)
{
    printf("\n=== SPI COMMUNICATION TEST (ENHANCED) ===\n");
    
    uint32 regValue = 0;
    uint32 regValue2 = 0;
    uint32 regValue3 = 0;
    uint32 regValue4 = 0;
    int testCount = 0;
    
    // 尝试多次读取，看是否有任何变化
    printf("[TEST] Attempting 3 consecutive reads of REG_AFE_AFECON...\n");
    for(testCount = 0; testCount < 3; testCount++)
    {
        regValue = AD5940_ReadReg(BITM_AFE_AFECON_INAMPEN);
        printf("[ATTEMPT %d] Register Value: 0x%lX\n", testCount + 1, (unsigned long)regValue);
        CyDelay(50);
    }
    
    // 尝试读取不同的寄存器
    printf("\n[TEST] Reading different AD5940 registers...\n");
    regValue = AD5940_ReadReg(BITM_AFE_AFECON_INAMPEN);
    printf("  REG_AFE_AFECON:  0x%lX\n", (unsigned long)regValue);
    CyDelay(20);
    
    regValue2 = AD5940_ReadReg(BITM_AFE_AFECON_INAMPEN);
    printf("  REG_AFE_ADCCON:  0x%lX\n", (unsigned long)regValue2);
    CyDelay(20);
    
    regValue3 = AD5940_ReadReg(BITM_AFE_AFECON_INAMPEN);
    printf("  REG_AFE_FIFOCON: 0x%lX\n", (unsigned long)regValue3);
    CyDelay(20);
    
    regValue4 = AD5940_ReadReg(REG_AFE_ADCDAT);
    printf("  REG_AFE_ADCDAT:  0x%lX\n", (unsigned long)regValue4);
    
    // 分析读取结果
    printf("\n[ANALYSIS]\n");
    
    if((regValue == 0x808080 || regValue == 0x80) && 
       (regValue2 == 0x808080 || regValue2 == 0x80) &&
       (regValue3 == 0x808080 || regValue3 == 0x80))
    {
        printf("[CRITICAL] Consistent 0x80 pattern detected!\n");
        printf("   This is NOT random garbage - it's a systematic issue:\n");
        printf("   Possible Causes:\n");
        printf("   1. **MISO line floating** - Not connected or open circuit\n");
        printf("   2. **CS timing** - Signal not toggling correctly\n");
        printf("   3. **DUMMY READ issue** - SPI read sequence problem\n");
        printf("   4. **AD5940 not responding** - Check power, reset, and connections\n");
        printf("\n   ACTION REQUIRED:\n");
        printf("   1. Check physical connection: PSoC P0.5 <-> AD5940 DOUT\n");
        printf("   2. Measure voltage on DOUT pin (should toggle with SCLK)\n");
        printf("   3. Verify AD5940 VDD = 3.3V and GND is connected\n");
        printf("   4. Try reducing SPI speed further (now at 100 kbps)\n");
    }
    else if(regValue != regValue2 || regValue != regValue3)
    {
        printf("[GOOD SIGN] Different values in different registers!\n");
        printf("   This suggests SPI communication is partially working\n");
    }
    else
    {
        printf("[OK] Register values appear reasonable\n");
    }
    
    printf("\n[SPI CONFIGURATION]\n");
    printf("   - SPI Speed: 100 kbps (reduced from 1 Mbps)\n");
    printf("   - Mode: Motorola (CPHA=0, CPOL=0)\n");
    printf("   - Data Width: 8-bit\n");
    
    printf("=== SPI TEST END ===\n\n");
}

/*******************************************************************************
* Function Name: DiagnosticsFIFO
********************************************************************************
* Summary:
*   诊断 FIFO 和中断状态 - 增强版本
*******************************************************************************/
void DiagnosticsFIFO(void)
{
    printf("\n=== FIFO & INTERRUPT STATUS (ENHANCED) ===\n");
    
    uint32 fifoCount = 0;
    
    printf("[DEBUG] Checking FIFO status...\n");
    if(pAmpCfg != NULL)
    {
        printf("    FifoDataCount: %lu\n", (unsigned long)pAmpCfg->FifoDataCount);
        printf("    FifoThresh: %d\n", (int)pAmpCfg->FifoThresh);
        printf("    AMPInited: %d\n", (int)pAmpCfg->AMPInited);
        printf("    StopRequired: %d\n", (int)pAmpCfg->StopRequired);
    }
    else
    {
        printf("[ERROR] pAmpCfg is NULL!\n");
        printf("=== FIFO TEST END ===\n\n");
        return;
    }
    
    if(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH) == bTRUE)
    {
        printf("    [OK] FIFO Threshold Interrupt: DETECTED\n");
    }
    else
    {
        printf("    [WARNING] FIFO Threshold Interrupt: NOT DETECTED\n");
        printf("            Check if ADC is running and FIFO has data\n");
    }
    
    printf("\n[DIAGNOSIS]\n");
    if(pAmpCfg->AMPInited == 0)
    {
        printf("    [CRITICAL] AMPInited = 0, initialization incomplete\n");
        printf("    This prevents any measurements from starting\n");
    }
    else
    {
        printf("    [OK] AMPInited = 1, initialization complete\n");
    }
    
    printf("=== FIFO TEST END ===\n\n");
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
* Function Name: TestRawSPI
********************************************************************************
* Summary:
*   测试原生 SPI 通讯（使用底层AD5940接口）
*   用于确认 SPI 硬件是否工作
*******************************************************************************/
void TestRawSPI(void)
{
    printf("\n=== RAW SPI TEST ===\n");
    
    uint8 txBuffer[8];
    uint8 rxBuffer[8];
    int i;
    
    // 测试 1: 发送 0x00 模式
    printf("[TEST 1] Sending 0x00, 0x00, 0x00, 0x00...\n");
    for(i = 0; i < 4; i++)
        txBuffer[i] = 0x00;
    
    AD5940_CsClr();
    CyDelay(2);
    AD5940_ReadWriteNBytes(txBuffer, rxBuffer, 4);
    CyDelay(2);
    AD5940_CsSet();
    
    printf("  TX: 0x00 0x00 0x00 0x00\n");
    printf("  RX: 0x%02X 0x%02X 0x%02X 0x%02X\n", rxBuffer[0], rxBuffer[1], rxBuffer[2], rxBuffer[3]);
    
    CyDelay(100);
    
    // 测试 2: 发送 0xFF 模式
    printf("[TEST 2] Sending 0xFF, 0xFF, 0xFF, 0xFF...\n");
    for(i = 0; i < 4; i++)
        txBuffer[i] = 0xFF;
    
    AD5940_CsClr();
    CyDelay(2);
    AD5940_ReadWriteNBytes(txBuffer, rxBuffer, 4);
    CyDelay(2);
    AD5940_CsSet();
    
    printf("  TX: 0xFF 0xFF 0xFF 0xFF\n");
    printf("  RX: 0x%02X 0x%02X 0x%02X 0x%02X\n", rxBuffer[0], rxBuffer[1], rxBuffer[2], rxBuffer[3]);
    
    CyDelay(100);
    
    // 测试 3: 发送 0xAA 0x55 交替模式
    printf("[TEST 3] Sending 0xAA, 0x55, 0xAA, 0x55...\n");
    txBuffer[0] = 0xAA;
    txBuffer[1] = 0x55;
    txBuffer[2] = 0xAA;
    txBuffer[3] = 0x55;
    
    AD5940_CsClr();
    CyDelay(2);
    AD5940_ReadWriteNBytes(txBuffer, rxBuffer, 4);
    CyDelay(2);
    AD5940_CsSet();
    
    printf("  TX: 0xAA 0x55 0xAA 0x55\n");
    printf("  RX: 0x%02X 0x%02X 0x%02X 0x%02X\n", rxBuffer[0], rxBuffer[1], rxBuffer[2], rxBuffer[3]);
    
    // 分析结果
    printf("\n[ANALYSIS]\n");
    
    // 如果所有测试都返回相同的值，说明有严重的硬件问题
    if((rxBuffer[0] == 0x80 && rxBuffer[1] == 0x80 && rxBuffer[2] == 0x80 && rxBuffer[3] == 0x80) ||
       (rxBuffer[0] == 0xFF && rxBuffer[1] == 0xFF && rxBuffer[2] == 0xFF && rxBuffer[3] == 0xFF) ||
       (rxBuffer[0] == 0x00 && rxBuffer[1] == 0x00 && rxBuffer[2] == 0x00 && rxBuffer[3] == 0x00))
    {
        printf("  [CRITICAL] All tests return same value!\n");
        printf("  This indicates MISO line is:\n");
        
        if(rxBuffer[0] == 0x80)
            printf("  - Stuck or floating with 0x80 pattern (10000000 binary)\n");
        else if(rxBuffer[0] == 0xFF)
            printf("  - Pulled high (0xFF = 11111111 binary, likely floating/not connected)\n");
        else if(rxBuffer[0] == 0x00)
            printf("  - Stuck low or shorted to ground\n");
            
        printf("\n  HARDWARE CHECK REQUIRED:\n");
        printf("  1. Verify MISO pin (PSoC P0.5) is physically connected to AD5940 DOUT\n");
        printf("  2. Check for shorts or cold solder joints\n");
        printf("  3. Verify AD5940 is powered correctly (3.3V)\n");
        printf("  4. Check CS and SCLK connections\n");
    }
    else
    {
        printf("  [OK] SPI is responding with variable data\n");
    }
    
    printf("\n=== RAW SPI TEST END ===\n\n");
}

/*******************************************************************************
* Function Name: FullDiagnostics
********************************************************************************
* Summary:
*   执行完整诊断 - 包含原生SPI测试
*******************************************************************************/
void FullDiagnostics(void)
{
    printf("\n\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║        AD5941 COMPLETE DIAGNOSTIC SUITE                ║\n");
    printf("║         Troubleshooting BLE Data Transmission          ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    
    printf("\n[STEP 0] Testing Raw SPI Communication...\n");
    TestRawSPI();
    
    CyDelay(100);
    
    printf("[STEP 1] Verifying AD5940 SPI Communication...\n");
    DiagnosticsSPI();
    
    CyDelay(100);
    
    printf("[STEP 2] Checking FIFO and Interrupts...\n");
    DiagnosticsFIFO();
    
    CyDelay(100);
    
    printf("[STEP 3] Verifying Electrode Configuration...\n");
    DiagnosticsElectrodes();
    
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║  DIAGNOSTIC COMPLETE - Check output above for issues   ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");
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

// 发送乳酸数据（改为诊断日志输出）
void SendLactateDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    static char dataString[40];
    static uint8 diagStep = 0;
    uint32 regValue;
    uint8 initStatus;
    uint32 fifoCount;
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        // 轮流显示不同的诊断信息
        switch(diagStep % 4)
        {
            case 0:
                // 显示原始寄存器值（不做 & 0xFFFFFF）
                regValue = AD5940_ReadReg(REG_AFECON_CHIPID);
                sprintf(dataString, "Reg:0x%lX", (unsigned long)regValue);
                break;
            case 1:
                // 显示初始化状态
                initStatus = (pAmpCfg != NULL) ? (uint8)pAmpCfg->AMPInited : 99;
                sprintf(dataString, "Init:%d", (int)initStatus);
                break;
            case 2:
                // 显示 FIFO 计数
                fifoCount = (pAmpCfg != NULL) ? pAmpCfg->FifoDataCount : 0;
                sprintf(dataString, "FIFO:%lu", fifoCount);
                break;
            case 3:
                // 显示电极引脚状态
                sprintf(dataString, "A1:%d A2:%d A3:%d", 
                        (int)AMP1_EN_Read(), 
                        (int)AMP2_EN_Read(), 
                        (int)AMP3_EN_Read());
                break;
        }
        diagStep++;
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
        notificationHandle.value.val = (uint8*)dataString;
        notificationHandle.value.len = strlen(dataString);
        
        if(CyBle_GattsNotification(cyBle_connHandle, &notificationHandle) == CYBLE_ERROR_OK)
        {
        }
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
    static uint8 diagStep = 0;  // 轮询步骤标志
    
    // 定义发送和接收缓冲区（与TestReadADIID保持一致）
    uint8_t tx[6] = {0x6D, 0x04, 0x00, 0xFF, 0xFF, 0xFF};
    uint8_t rx[6] = {0};
    uint16_t result1, result2, result3;
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        // 每次调用都执行一次ADIID读取（与TestReadADIID逻辑相同）
        AD5940_CsClr();
        CyDelayUs(2);
        AD5940_ReadWriteNBytes(tx, rx, 6);
        CyDelayUs(2);
        AD5940_CsSet();
        
        // 计算解析结果
        result1 = (rx[2] << 8) | rx[3];  // 从索引2解析
        result2 = (rx[3] << 8) | rx[4];  // 从索引3解析
        result3 = (rx[4] << 8) | rx[5];  // 从索引4解析（预期0x4144）
        
        // 轮询显示不同的调试信息
        switch(diagStep % 4)
        {
            case 0:
                // 显示发送和接收的原始数据
                sprintf(tempString, "TX:%02X %02X %02X %02X %02X %02X",
                        tx[0], tx[1], tx[2], tx[3], tx[4], tx[5]);
                break;
            case 1:
                sprintf(tempString, "RX:%02X %02X %02X %02X %02X %02X",
                        rx[0], rx[1], rx[2], rx[3], rx[4], rx[5]);
                break;
            case 2:
                // 显示前两种解析结果
                sprintf(tempString, "Res1:0x%04X Res2:0x%04X", result1, result2);
                break;
            case 3:
                // 显示第三种解析结果（预期值）
                sprintf(tempString, "Res3:0x%04X (exp 0x4144)", result3);
                break;
        }
        diagStep++;  // 切换到下一个步骤
        
        // 发送BLE通知
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
int main()
{
    CyGlobalIntEnable;
    
    // 初始化LED
    Disconnect_LED_Write(LED_OFF);
    Advertising_LED_Write(LED_OFF);
    
    printf("\n*** SYSTEM STARTUP ***\n");
    
    // 初始化BLE
    apiResult = CyBle_Start(AppCallBack);
    if(apiResult != CYBLE_ERROR_OK)
    {
        printf("[ERROR] BLE initialization failed\n");
    }
    else
    {
        printf("[OK] BLE initialized\n");
    }
    
    // 初始化SPI引脚为GPIO（软件SPI不需要硬件SPI_1）
    printf("[INFO] Initializing Software SPI on GPIO pins...\n");
    
    // 初始化SPI引脚为输出
    // SCLK、MOSI、CS设置为输出
    // MISO设置为输入（在read函数中）
    
    // 设置SCLK初始状态为高（空闲）
    AD5940_SCLK_Write(0);  // 正确：CPOL=0, 空闲状态为低电平
    CyDelay(10);
    printf("[OK] Software SPI initialized\n");
    
    // 🔍 软件SPI诊断
    printf("\n[DIAGNOSTIC] Software SPI GPIO Pin Status:\n");
    
    // 检查各引脚的初始状态
    uint8_t sclk_data = AD5940_SCLK_Read();
    uint8_t mosi_data = AD5940_MOSI_Read();
    uint8_t miso_data = AD5940_MISO_Read();
    uint8_t cs_data = AD5940_CS_Read();
    
    printf("  SCLK (P1.7): %d (should be 1 at idle)\n", sclk_data);
    printf("  MOSI (P0.4): %d\n", mosi_data);
    printf("  MISO (P0.5): %d\n", miso_data);
    printf("  CS   (P?.?): %d (should be 1 at idle)\n", cs_data);
    
    // 验证SCLK能否正确跳变（快速测试）
    printf("\n[DIAGNOSTIC] SCLK Toggle Test:\n");
    AD5940_SCLK_Write(0);
    CyDelayUs(1);
    uint8_t sclk_low = AD5940_SCLK_Read();
    
    AD5940_SCLK_Write(1);
    CyDelayUs(1);
    uint8_t sclk_high = AD5940_SCLK_Read();
    
    printf("  SCLK Low: %d (expect 0)\n", sclk_low);
    printf("  SCLK High: %d (expect 1)\n", sclk_high);
    
    if(sclk_low == 0 && sclk_high == 1)
    {
        printf("  ✅ SCLK can toggle correctly!\n");
    }
    else
    {
        printf("  ❌ SCLK toggle test FAILED - pin may be stuck!\n");
    }
    printf("\n");
    
    AD5941_HardReset();   // ← 必须在任何 SPI 前

    
    
   
    // 🔍 快速验证 CHIPID - 在 AD5941_Initialize 前进行简单测试
  
    uint32_t testChipID = AD5940_ReadReg(REG_AFECON_CHIPID);

    
    // 初始化控制引脚
    printf("[INFO] Initializing control pins...\n");
    DRUG_EN_1_Write(0);
    STIM_EN_A_Write(0);
    AMP1_EN_Write(0);
    AMP2_EN_Write(0);
    AMP3_EN_Write(0);
    printf("[OK] Control pins initialized\n");
        // 初始化AD5941
    printf("[INFO] Initializing AD5941...\n");
    AD5941_Initialize();
    // 启动定时器中断
    printf("[INFO] Starting timer interrupt...\n");
    CySysWdtSetInterruptCallback(CY_SYS_WDT_COUNTER2, Timer_Interrupt);
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);
    printf("[OK] Timer started\n");
    
    // ===== 完整诊断 =====
    printf("\n[DIAGNOSTIC] Running full system diagnostics...\n");
    CyDelay(500);
    FullDiagnostics();
    CyDelay(500);
    
    // 等待初始化完成
    printf("[INFO] Waiting for AD5941 initialization...\n");
    uint32 initWaitTime = 0;
    while((pAmpCfg->AMPInited == bFALSE) && (initWaitTime < 5000))
    {
        CyDelay(100);
        initWaitTime += 100;
        printf("[INFO] Init status: %d (waited %lu ms)\n", (int)pAmpCfg->AMPInited, initWaitTime);
    }
        if(pAmpCfg->AMPInited == bTRUE)
    {
        printf("[INFO] Enabling electrode channels...\n");
        AMP1_EN_Write(1);
        AMP2_EN_Write(1);
        AMP3_EN_Write(1);
    }
    if(pAmpCfg->AMPInited == bTRUE)
    {
        printf("[OK] AD5941 initialization COMPLETE!\n");
    }
    else
    {
        printf("[WARNING] AD5941 initialization timeout or failed\n");
    }
    
    uint32 lastSendTime = 0;
    #define SEND_INTERVAL 3  // 每3秒刷新一次
    
    printf("[INFO] Entering main loop...\n");
    printf("*** SYSTEM READY ***\n\n");
    /***************************************************************************
    * 主循环
    ***************************************************************************/
    while(1)
    {
        // 处理BLE事件
        CyBle_ProcessEvents();
        
        // 低功耗管理
        //LowPowerImplementation();
        
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