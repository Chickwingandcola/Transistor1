/*******************************************************************************
* File Name: ad5941_platform.h
*
* Description:
*   AD5940平台接口头文件
*   声明所有平台相关的函数
*
********************************************************************************/

#ifndef AD5941_PLATFORM_H
#define AD5941_PLATFORM_H

#include <stdint.h>

/*******************************************************************************
* SPI引脚宏定义
*******************************************************************************/

#define SPI_CS_HIGH()      AD5940_CS_Write(1)        // CS 高电平
#define SPI_CS_LOW()       AD5940_CS_Write(0)        // CS 低电平
#define SPI_SCLK_SET()     AD5940_SCLK_Write(1)      // SCLK 设高
#define SPI_SCLK_CLR()     AD5940_SCLK_Write(0)      // SCLK 设低
#define SPI_MOSI_SET()     AD5940_MOSI_Write(1)      // MOSI 设高
#define SPI_MOSI_CLR()     AD5940_MOSI_Write(0)      // MOSI 设低
#define SPI_MISO_GET()     AD5940_MISO_Read()        // MISO 读取

/*******************************************************************************
* 核心SPI通信函数
*******************************************************************************/

/**
 * @brief 软件SPI传输单字节
 * @param TxByte: 要发送的字节
 * @return 接收到的字节
 */
uint8_t SoftSPI_TxRxByte(uint8_t TxByte);

/**
 * @brief SPI读写函数
 * @param pSendBuffer: 发送缓冲区
 * @param pRecvBuff: 接收缓冲区
 * @param length: 数据长度
 * @return 0=成功, -1=失败
 */
int32_t AD5940_ReadWriteNBytes(uint8_t *pSendBuffer, 
                                uint8_t *pRecvBuff, 
                                uint32_t length);

/*******************************************************************************
* GPIO控制函数
*******************************************************************************/

/**
 * @brief CS片选控制
 */
void AD5940_CsClr(void);
void AD5940_CsSet(void);

/**
 * @brief 复位引脚控制
 */
void AD5940_RstSet(void);
void AD5940_RstClr(void);

/*******************************************************************************
* 延时和中断函数
*******************************************************************************/

/**
 * @brief 延时函数（10us为单位）
 */
void AD5940_Delay10us(uint32_t time);

/**
 * @brief 中断管理
 */
uint32_t AD5940_GetMCUIntFlag(void);
uint32_t AD5940_ClrMCUIntFlag(void);

/*******************************************************************************
* 初始化函数
*******************************************************************************/

/**
 * @brief 初始化MCU资源
 * @param pCfg: 配置参数（通常为NULL）
 * @return 0=成功, -1=失败
 */
int32_t AD5940_MCUResourceInit(void *pCfg);

#endif /* AD5941_PLATFORM_H */

/* [] END OF FILE */
