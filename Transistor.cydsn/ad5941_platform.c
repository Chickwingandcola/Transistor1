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
* 底层SPI通信函数 - AD5940库的核心接口
*******************************************************************************/

/**
 * @brief SPI读写函数 - AD5940库需要的核心函数
 * @param pSendBuffer: 发送缓冲区指针
 * @param pRecvBuff: 接收缓冲区指针  
 * @param length: 数据长度（字节数）
 * @return 0=成功, -1=参数错误或超时
 * 
 * 实现步骤：
 * 1. 参数验证
 * 2. 拉低CS片选信号
 * 3. 逐字节SPI收发数据
 * 4. 拉高CS片选信号
 * 5. 返回成功状态
 */
int32_t AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuff, unsigned long length)
{
    uint32_t i;
    uint32_t timeout;
    
    /* 参数检查 */
    if(pSendBuffer == NULL || pRecvBuff == NULL || length == 0)
    {
        return -1;  /* 参数错误 */
    }
    
    /* 拉低CS片选信号 */
    AD5940_CS_Write(0);
    CyDelayUs(2);  /* 等待CS建立时间 */
    
    /* 逐字节收发数据 */
    for(i = 0; i < length; i++)
    {
        /* 清空接收缓冲区中的旧数据 */
        while(SPI_1_SpiUartGetRxBufferSize() > 0)
        {
            SPI_1_SpiUartReadRxData();
        }
        
        /* 发送一个字节 */
        SPI_1_SpiUartWriteTxData(pSendBuffer[i]);
        
        /* 等待接收完成（带超时保护） */
        timeout = 10000;
        while(SPI_1_SpiUartGetRxBufferSize() == 0 && timeout > 0)
        {
            timeout--;
            CyDelayUs(1);
        }
        
        if(timeout == 0)
        {
            AD5940_CS_Write(1);
            return -1;  /* SPI超时 */
        }
        
        /* 读取接收到的字节 */
        pRecvBuff[i] = SPI_1_SpiUartReadRxData();
    }
    
    /* 拉高CS片选信号 */
    CyDelayUs(2);
    AD5940_CS_Write(1);
    
    return 0;  /* 成功 */
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
    /* 设置CS和RST的初始状态 */
    AD5940_CS_Write(1);      /* CS默认高电平（未选中） */
    AD5940_RST_Write(1);     /* RST默认高电平（正常工作） */
    
    /* SPI通常在main()中已启动，这里只做验证
     * 如果需要在此启动SPI，取消下行注释
     */
    /* SPI_1_Start(); */
    
    /* 等待系统稳定 */
    CyDelay(10);
    
    return 0;  /* 成功 */
}

/* [] END OF FILE */
