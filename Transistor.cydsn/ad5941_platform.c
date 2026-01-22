/*******************************************************************************
* File Name: ad5941_platform.c
*
* Description:
*   AD5940硬件抽象层 - PSoC平台适配（完整可运行版本）
*   提供SPI通信和GPIO控制的底层接口
*
* 编译说明：
*   此文件包含AD5940库所需的所有平台相关函数实现
*   无需修改ad5940.h，该文件自动提供所有必需接口
*
* 注意：
*   - PSoC Creator中已配置SPI_1组件和GPIO引脚
*   - AD5940_CS 和 AD5940_RST 已在TopDesign中定义
*
********************************************************************************/

#include "project.h"

/*******************************************************************************
* 底层SPI通信函数 - 使用软件SPI实现（解决硬件SPI时钟问题）
*******************************************************************************/

/* ===== 软件 SPI 引脚定义 ===== */
/* 使用GPIO模拟SPI，避免TopDesign时钟配置问题 */

#define SPI_CS_HIGH()      AD5940_CS_Write(1)        // CS 高电平
#define SPI_CS_LOW()       AD5940_CS_Write(0)        // CS 低电平
#define SPI_SCLK_SET()     AD5940_SCLK_Write(1)      // SCLK 设高
#define SPI_SCLK_CLR()     AD5940_SCLK_Write(0)      // SCLK 设低
#define SPI_MOSI_SET()     AD5940_MOSI_Write(1)      // MOSI 设高
#define SPI_MOSI_CLR()     AD5940_MOSI_Write(0)      // MOSI 设低
#define SPI_MISO_GET()     AD5940_MISO_Read()        // MISO 读取

/**
 * @brief 软件SPI延时 - 控制时钟频率
 * 2us延时对应约250 kHz时钟频率（符合AD5940规范 ≤400 kHz）
 */
static inline void SPI_Delay(void)
{
    // 增加延时，确保波形稳定，特别是对于长线连接
    CyDelayUs(5); 
}

uint8_t SoftSPI_TxRxByte(uint8_t data)
{
    uint8_t i;
    uint8_t receive = 0;

    for (i = 0; i < 8; i++)
    {
        // 1. 准备 MOSI 数据
        if (data & 0x80) AD5940_MOSI_Write(1);
        else AD5940_MOSI_Write(0);
        data <<= 1;
        
        CyDelayUs(1); // ⭐ 增加 1us 确保电平稳定
        
        // 2. 拉高时钟 (Mode 0: 在上升沿采样)
        AD5940_SCLK_Write(1);
        CyDelayUs(2); // ⭐ 时钟高电平宽度
        
        // 3. 读取 MISO
        receive <<= 1;
        if (AD5940_MISO_Read()) receive |= 1;
        
        // 4. 拉低时钟
        AD5940_SCLK_Write(0);
        CyDelayUs(1); // ⭐ 时钟低电平宽度
    }
    return receive;
}

/**
 * @brief SPI读写多字节 - 修正空闲态
 */
int32_t AD5940_ReadWriteNBytes(uint8_t *pSendBuffer, uint8_t *pRecvBuff, uint32_t length)
{
    uint32_t i;

    if (!pSendBuffer || !pRecvBuff || length == 0)
        return -1;

    // ⭐ 不控制CS，只负责传输数据
    for (i = 0; i < length; i++)
    {
        pRecvBuff[i] = SoftSPI_TxRxByte(pSendBuffer[i]);
        CyDelayUs(2);  // 字节间延时
    }

    return 0;
}

/*******************************************************************************

* GPIO控制函数
*******************************************************************************/

/**
 * @brief 拉低CS片选信号
 * 
 * CS为低电平时AD5940被选中，可以进行SPI通信
 */
void AD5940_CsClr(void)
{
    AD5940_CS_Write(0);
}

/**
 * @brief 拉高CS片选信号
 * 
 * CS为高电平时AD5940不被选中，SPI通信结束
 */
void AD5940_CsSet(void)
{
    AD5940_CS_Write(1);
}

/**
 * @brief 拉高复位引脚（芯片正常工作）
 * 
 * RST=1: 芯片正常工作
 * RST=0: 芯片处于复位状态
 */
void AD5940_RstSet(void)
{
    AD5940_RST_Write(1);
}

/**
 * @brief 拉低复位引脚（复位芯片）
 * 
 * 将AD5940置于复位状态
 * 通常需要保持低电平至少10us，然后拉高以完成复位
 */
void AD5940_RstClr(void)
{
    AD5940_RST_Write(0);
}

/*******************************************************************************
* 延时和中断函数
*******************************************************************************/

/**
 * @brief 延时函数（以10微秒为单位）
 * @param time: 延时时间，单位为10us
 * 
 * 例如：AD5940_Delay10us(100) 延时1000us（1ms）
 *      AD5940_Delay10us(1000) 延时10ms
 */
void AD5940_Delay10us(uint32_t time)
{
    uint32_t i;
    
    if(time == 0) 
        return;
    
    /* 延时 time*10 微秒 */
    for(i = 0; i < time; i++)
    {
        CyDelayUs(10);
    }
}

/**
 * @brief 获取MCU中断标志
 * @return 中断状态（0=无中断, 非0=有中断）
 * 
 * 如果硬件中连接了AD5940的中断输出引脚，可在此读取其状态
 * 当前实现返回0，表示未检测到中断
 */
uint32_t AD5940_GetMCUIntFlag(void)
{
    /* 如果硬件中连接了AD5940的中断引脚，可在此读取
     * 例如：return AD5940_Interrupt_Read();
     */
    
    /* 如果未使用中断引脚，返回0表示无中断 */
    return 0;
}

/**
 * @brief 清除MCU中断标志
 * @return 1=成功, 0=失败
 * 
 * 清除之前检测到的中断标志，为下一次中断做准备
 */
uint32_t AD5940_ClrMCUIntFlag(void)
{
    /* 如果使用了中断引脚，在此清除中断
     * 例如：AD5940_Interrupt_ClearInterrupt();
     */
    
    return 1;  /* 成功 */
}

/*******************************************************************************
* 初始化函数
*******************************************************************************/

/**
 * @brief 初始化MCU资源（SPI、GPIO等）
 * @param pCfg: 配置参数（通常为NULL）
 * @return 0=成功, -1=失败
 * 
 * 该函数初始化：
 * 1. CS和RST引脚的初始状态
 * 2. SPI通信参数（通常已在TopDesign中配置）
 * 3. 芯片工作环境
 * 
 * 调用时机：
 * - 在main()函数早期调用
 * - 在SPI_1_Start()之后调用
 * - 在任何AD5940操作之前调用
 */
int32_t AD5940_MCUResourceInit(void *pCfg)
{
    #ifdef AD5940_CS_SetDriveMode
        AD5940_CS_SetDriveMode(AD5940_CS_DM_STRONG);
    #endif
    
    #ifdef AD5940_SCLK_SetDriveMode
        AD5940_SCLK_SetDriveMode(AD5940_SCLK_DM_STRONG);
    #endif
    
    #ifdef AD5940_MOSI_SetDriveMode
        AD5940_MOSI_SetDriveMode(AD5940_MOSI_DM_STRONG);
    #endif
    
    #ifdef AD5940_RST_SetDriveMode
        AD5940_RST_SetDriveMode(AD5940_RST_DM_STRONG);
    #endif
    
    /* 设置引脚初始状态 */
    AD5940_CS_Write(1);      /* CS 高电平 */
    AD5940_RST_Write(1);     /* RST 高电平 */
    AD5940_SCLK_Write(0);    /* SCLK 低电平（CPOL=0）*/
    AD5940_MOSI_Write(0);    /* MOSI 低电平 */
    
    /* 等待系统稳定 */
    CyDelay(10);
    
    return 0;  /* 成功 */
}

/* [] END OF FILE */
