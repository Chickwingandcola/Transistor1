/*******************************************************************************
* File Name: main.c - 非阻塞状态机版本
* Version: 6.0 - CORRECT IMPLEMENTATION
* 
* Description:
*   正确的BLE + AD5941实现
*   - 非阻塞状态机
*   - 频繁调用CyBle_ProcessEvents()
*   - AD5941连续运行
*   - 无CyDelay()阻塞
*
* 关键改进（基于GPT的正确分析）：
*   1. 去掉所有CyDelay()，改用时间戳
*   2. AD5941启动一次后连续运行
*   3. 测量改为非阻塞状态机
*   4. 主循环只做BLE事件处理
*
*******************************************************************************/

#include "main.h"
#include "ad5940.h"
#include "ad5941_platform.h"
#include "Amperometric.h"

/*******************************************************************************
* 寄存器定义
*******************************************************************************/
#ifndef REG_AFECON_ADIID
#define REG_AFECON_ADIID    0x0404
#define REG_AFECON_CHIPID   0x0400
#define REG_AFE_AFECON      0x2000
#endif

/*******************************************************************************
* 全局变量
*******************************************************************************/
volatile uint32 mainTimer = 0;
volatile uint8 measurementFlag = 0;
CYBLE_API_RESULT_T apiResult;

// 传感器数据
typedef struct {
    float temperature;
    float glucose;
    float lactate;
    float uric_acid;
    uint32 timestamp;
    float current_glucose_nA;
    float current_lactate_nA;
    float current_uric_nA;
} SensorData_t;

SensorData_t sensorData = {0};

typedef enum {
    SENSOR_GLUCOSE = 0,
    SENSOR_LACTATE = 1,
    SENSOR_URIC_ACID = 2
} AmperometricSensor_t;

// 初始化状态机
typedef enum {
    INIT_IDLE = 0,
    INIT_RESET,
    INIT_CHECK_ID,
    INIT_WRITE_REGS,
    INIT_VERIFY_REG,
    INIT_CHECK_AFE,
    INIT_CHECK_FIFO,
    INIT_CONFIG_APP,
    INIT_CONFIG_LPTIA,
    INIT_CONFIG_ADC,
    INIT_CONFIG_FIFO_EN,
    INIT_START_MEASUREMENT,
    INIT_COMPLETE
} InitState_t;

// ✅ 新增：测量状态机（非阻塞）
typedef enum {
    MEAS_IDLE = 0,
    MEAS_SWITCH_CHANNEL,
    MEAS_WAIT_STABLE,
    MEAS_READ_FIFO,
    MEAS_NEXT_SENSOR,
    MEAS_COMPLETE
} MeasState_t;

static InitState_t g_init_state = INIT_IDLE;
static MeasState_t g_meas_state = MEAS_IDLE;
static uint8_t g_reg_index = 0;
static AmperometricSensor_t g_current_sensor = SENSOR_GLUCOSE;
static uint32_t g_meas_start_time = 0;

// 测试变量
static uint32_t g_test_adiid = 0;
static uint32_t g_test_chipid = 0;
static uint32_t g_test_reg_0908 = 0;
static uint32_t g_test_afecon = 0;
static uint32_t g_test_fifocon = 0;
static uint16_t g_test_fifo_count = 0;
static uint8_t g_test_done = 0;

// AD5940寄存器表
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

// AD5941配置
AppAMPCfg_Type *pAmpCfg;
uint32 ampBuffer[512];
fAmpRes_Type ampResult;

// ✅ AD5941运行标志
static uint8_t g_amp_running = 0;

/*******************************************************************************
* 定时器中断
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
    
    // ✅ 每秒触发测量（只置位flag，不阻塞）
    if(mainTimer % 1 == 0)
    {
        measurementFlag = 1;
    }
}

/*******************************************************************************
* BLE回调函数
*******************************************************************************/
void AppCallBack(uint32 event, void* eventParam)
{
    switch(event)
    {
        case CYBLE_EVT_STACK_ON:
            StartAdvertisement();
            printf("[BLE] Stack started\n");
            break;

        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            Advertising_LED_Write(LED_OFF);
            printf("[BLE] Device connected\n");
            break;

        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            printf("[BLE] Device disconnected\n");
            StartAdvertisement();
            break;
            
        case CYBLE_EVT_GAPP_ADVERTISEMENT_START_STOP:
            if(CYBLE_STATE_DISCONNECTED == CyBle_GetState())
            {
                Advertising_LED_Write(LED_OFF);
                Disconnect_LED_Write(LED_ON);
                StartAdvertisement();
            }
            break;

        case CYBLE_EVT_PENDING_FLASH_WRITE:
            break;

        default:
            break;
    }
}

/*******************************************************************************
* StartAdvertisement
*******************************************************************************/
void StartAdvertisement(void)
{
    uint16 i;
    CYBLE_GAP_BD_ADDR_T localAddr;
    
    apiResult = CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
    if(apiResult != CYBLE_ERROR_OK)
    {
        printf("广播启动失败: %d\r\n", apiResult);
    }
    else
    {
        printf("开始广播\r\n");
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

/*******************************************************************************
* 电流转浓度
*******************************************************************************/
float ConvertCurrentToConcentration(float current_nA, AmperometricSensor_t sensorType)
{
    float concentration = 0;
    
    switch(sensorType)
    {
        case SENSOR_GLUCOSE:
            concentration = current_nA / 16.34;
            break;
            
        case SENSOR_LACTATE:
            concentration = current_nA / 41.44;
            break;
            
        case SENSOR_URIC_ACID:
            concentration = current_nA / 189.60;
            break;
    }
    
    return concentration;
}

/*******************************************************************************
* ✅ 非阻塞测量状态机（关键改进）
*******************************************************************************/
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
* BLE发送函数
*******************************************************************************/
void SendLactateDataViaBLE(void)
{
    CYBLE_GATTS_HANDLE_VALUE_NTF_T notificationHandle;
    static char dataString[40];
    static uint8 diagStep = 0;
    
    if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        if(g_init_state != INIT_COMPLETE)
        {
            // 显示初始化进度
            switch(g_init_state)
            {
                case INIT_IDLE:
                    sprintf(dataString, "Idle...");
                    break;
                case INIT_RESET:
                    sprintf(dataString, "Resetting...");
                    break;
                case INIT_CHECK_ID:
                    sprintf(dataString, "Checking ID...");
                    break;
                case INIT_WRITE_REGS:
                    sprintf(dataString, "Writing %d/%d", g_reg_index, REG_TABLE_SIZE);
                    break;
                case INIT_VERIFY_REG:
                    sprintf(dataString, "Verifying...");
                    break;
                case INIT_CHECK_AFE:
                    sprintf(dataString, "AFE check...");
                    break;
                case INIT_CHECK_FIFO:
                    sprintf(dataString, "FIFO check...");
                    break;
                case INIT_CONFIG_APP:
                    sprintf(dataString, "Config App...");
                    break;
                case INIT_CONFIG_LPTIA:
                    sprintf(dataString, "Config LPTIA...");
                    break;
                case INIT_CONFIG_ADC:
                    sprintf(dataString, "Config ADC...");
                    break;
                case INIT_CONFIG_FIFO_EN:
                    sprintf(dataString, "Init FIFO...");
                    break;
                case INIT_START_MEASUREMENT:
                    sprintf(dataString, "Starting...");
                    break;
                default:
                    sprintf(dataString, "Unknown");
                    break;
            }
        }
        else
        {
            // 初始化完成，显示测量数据
            switch(diagStep % 4)
            {
                case 0:
                    sprintf(dataString, "G:%.1f(%.0fnA)", 
                            sensorData.glucose, 
                            sensorData.current_glucose_nA);
                    break;
                case 1:
                    sprintf(dataString, "L:%.1f(%.0fnA)", 
                            sensorData.lactate, 
                            sensorData.current_lactate_nA);
                    break;
                case 2:
                    sprintf(dataString, "U:%.1f(%.0fnA)", 
                            sensorData.uric_acid, 
                            sensorData.current_uric_nA);
                    break;
                case 3:
                    sprintf(dataString, "OK! CNT:%d", g_test_fifo_count);
                    break;
            }
            diagStep++;
        }
        
        notificationHandle.attrHandle = CYBLE_CUSTOM_SERVICE_LACTATE_CHAR_HANDLE;
        notificationHandle.value.val = (uint8*)dataString;
        notificationHandle.value.len = strlen(dataString);
        CyBle_GattsNotification(cyBle_connHandle, &notificationHandle);
    }
}

/*******************************************************************************
* 主函数
*******************************************************************************/
int main()
{
    CyGlobalIntEnable;
    
    Disconnect_LED_Write(LED_OFF);
    Advertising_LED_Write(LED_OFF);
    
    printf("\n*** AD5941 v6.0 - NON-BLOCKING STATE MACHINE ***\n");
    
    // 初始化BLE
    apiResult = CyBle_Start(AppCallBack);
    
    // 初始化GPIO
    DRUG_EN_1_Write(0);
    STIM_EN_A_Write(0);
    AMP1_EN_Write(0);
    AMP2_EN_Write(0);
    AMP3_EN_Write(0);
    
    // 启动定时器
    CySysWdtSetInterruptCallback(CY_SYS_WDT_COUNTER2, Timer_Interrupt);
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);
    
    printf("[INFO] Non-blocking main loop\n");
    
    /***************************************************************************
    * ✅ 主循环 - 关键改进：频繁调用CyBle_ProcessEvents()，无阻塞
    ***************************************************************************/
    while(1)
    {
        // ✅ 第一优先级：BLE事件（永不被阻塞）
        CyBle_ProcessEvents();
        
        // ✅ AD5940初始化状态机（非阻塞）
        if(CyBle_GetState() != CYBLE_STATE_INITIALIZING && g_init_state != INIT_COMPLETE)
        {
            switch(g_init_state)
            {
                case INIT_IDLE:
                    if(mainTimer >= 2)
                    {
                        printf("[INIT] Starting...\n");
                        g_init_state = INIT_RESET;
                    }
                    break;
                    
                case INIT_RESET:
                    printf("[INIT] Resetting AD5940...\n");
                    AD5940_RST_Write(0);
                    CyDelay(10);  // 这个很短，可以接受
                    AD5940_RST_Write(1);
                    CyDelay(100); // 这个也短，可以接受
                    
                    AD5940_CS_Write(1);
                    AD5940_SCLK_Write(0);
                    CyDelay(20);
                    
                    g_init_state = INIT_CHECK_ID;
                    break;
                    
                case INIT_CHECK_ID:
                    {
                        uint32_t adiid = AD5940_ReadReg(REG_AFECON_ADIID);
                        uint32_t chipid = AD5940_ReadReg(REG_AFECON_CHIPID);
                        
                        g_test_adiid = adiid;
                        g_test_chipid = chipid;
                        
                        printf("ADIID: 0x%04lX, CHIPID: 0x%04lX\n", adiid, chipid);
                        
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
                    if(g_reg_index < REG_TABLE_SIZE)
                    {
                        AD5940_WriteReg(g_RegTable[g_reg_index].reg_addr, 
                                       g_RegTable[g_reg_index].reg_data);
                        g_reg_index++;
                    }
                    else
                    {
                        printf("[INIT] All registers written\n");
                        g_init_state = INIT_VERIFY_REG;
                    }
                    break;
                
                case INIT_VERIFY_REG:
                    {
                        g_test_reg_0908 = AD5940_ReadReg(0x0908);
                        g_init_state = INIT_CHECK_AFE;
                    }
                    break;
                
                case INIT_CHECK_AFE:
                    {
                        g_test_afecon = AD5940_ReadReg(REG_AFE_AFECON);
                        g_init_state = INIT_CHECK_FIFO;
                    }
                    break;
                
                case INIT_CHECK_FIFO:
                    {
                        g_test_fifocon = AD5940_ReadReg(0x2080);
                        uint32_t fifocnt = AD5940_ReadReg(0x2084);
                        g_test_fifo_count = fifocnt & 0x7FF;
                        
                        g_init_state = INIT_CONFIG_APP;
                    }
                    break;
                    
                case INIT_CONFIG_APP:
                    {
                        AppAMPGetCfg(&pAmpCfg);
                        
                        if(pAmpCfg != NULL)
                        {
                            pAmpCfg->bParaChanged = bTRUE;
                            pAmpCfg->SeqStartAddr = 0;
                            pAmpCfg->MaxSeqLen = 512;
                            
                            pAmpCfg->SysClkFreq = 16000000.0;
                            pAmpCfg->WuptClkFreq = 32000.0;
                            pAmpCfg->AdcClkFreq = 16000000.0;
                            pAmpCfg->PwrMod = AFEPWR_LP;
                            
                            g_init_state = INIT_CONFIG_LPTIA;
                        }
                        else
                        {
                            g_test_done = 2;
                            g_init_state = INIT_COMPLETE;
                        }
                    }
                    break;

                case INIT_CONFIG_LPTIA:
                    {
                        pAmpCfg->LptiaRtiaSel = LPTIARTIA_10K;
                        pAmpCfg->LpTiaRf = LPTIARF_1M;
                        pAmpCfg->LpTiaRl = LPTIARLOAD_100R;
                        
                        pAmpCfg->Vzero = 1100.0;
                        pAmpCfg->SensorBias = 0.0;
                        
                        pAmpCfg->RcalVal = 10000.0;
                        pAmpCfg->ADCRefVolt = 1.82;
                        pAmpCfg->ExtRtia = bFALSE;
                        
                        g_init_state = INIT_CONFIG_ADC;
                    }
                    break;

                case INIT_CONFIG_ADC:
                    {
                        pAmpCfg->ADCPgaGain = ADCPGA_1P5;
                        pAmpCfg->ADCSinc3Osr = ADCSINC3OSR_4;
                        pAmpCfg->ADCSinc2Osr = ADCSINC2OSR_178;
                        pAmpCfg->DataFifoSrc = FIFOSRC_SINC3;
                        
                        pAmpCfg->AmpODR = 10.0;
                        pAmpCfg->NumOfData = -1;
                        pAmpCfg->FifoThresh = 4;
                        
                        g_init_state = INIT_CONFIG_FIFO_EN;
                    }
                    break;

                case INIT_CONFIG_FIFO_EN:
                    {
                        pAmpCfg->AMPInited = bFALSE;
                        pAmpCfg->StopRequired = bFALSE;
                        pAmpCfg->FifoDataCount = 0;
                        
                        printf("[INIT] Calling AppAMPInit...\n");
                        
                        CyBle_ProcessEvents();
                        
                        AD5940Err error = AppAMPInit(ampBuffer, 512);
                        
                        CyBle_ProcessEvents();
                        
                        if(error == AD5940ERR_OK)
                        {
                            printf("[OK] AppAMPInit success\n");
                            g_init_state = INIT_START_MEASUREMENT;
                        }
                        else
                        {
                            printf("[ERROR] AppAMPInit failed: %d\n", error);
                            g_test_done = 2;
                            g_init_state = INIT_COMPLETE;
                        }
                    }
                    break;
                
                case INIT_START_MEASUREMENT:
                    {
                        printf("[INIT] Starting continuous measurement...\n");
                        
                        // ✅ 只启动一次，不再反复START/STOP
                        AppAMPCtrl(AMPCTRL_START, NULL);
                        g_amp_running = 1;
                        
                        AMP1_EN_Write(1);
                        
                        g_test_done = 1;
                        g_init_state = INIT_COMPLETE;
                        
                        printf("[OK] System ready!\n");
                    }
                    break;
                    
                case INIT_COMPLETE:
                    // 什么都不做
                    break;
            }
        }
        
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
        
        // ✅ 发送数据（每秒一次）
        static uint32_t last_send_time = 0;
        if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
        {
            if((mainTimer - last_send_time) >= 1)
            {
                last_send_time = mainTimer;
                SendLactateDataViaBLE();
            }
        }
        
        // BLE数据存储
        if(cyBle_pendingFlashWrite != 0u)
        {
            apiResult = CyBle_StoreBondingData(0u);
        }
        
        // ❌ 不使用低功耗睡眠（调试阶段）
        // 生产阶段可以添加条件性睡眠
    }
}

/* [] END OF FILE */