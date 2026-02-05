# AD5941初始化卡死 - 快速修复总结

## 🔴 问题
调用 `AD5941_Initialize()` 后系统卡死，无响应

## 🎯 根本原因
`AD5940_WakeUp()` 函数在SPI通信失败时进入**无限循环**，无硬超时保护

## ✅ 三个关键修复

### 修复1：ad5940.c - AD5940_WakeUp()
```c
// 第2953行附近，添加硬超时保护
uint32_t hardTimeout = 500000;  // 约5秒
if(count >= hardTimeout) break;  // 防止无限循环
```

### 修复2：Amperometric.c - AppAMPInit()  
```c
// 第327行，改进错误处理
uint32_t wakeup_count = AD5940_WakeUp(10);
if(wakeup_count > 50)  // 增加容差
    return AD5940ERR_WAKEUP;
```

### 修复3：main.c - AD5941_Initialize()
```c
// 第198行，添加诊断信息和容错
if(!id_valid)
    printf("[ERROR] SPI Communication Failed. Continuing anyway...\r\n");
    // 不返回，而是继续（已有硬超时保护）
```

## 📋 修复文件列表
- ✅ `Transistor.cydsn/ad5940.c` 
- ✅ `Transistor.cydsn/Amperometric.c`
- ✅ `Transistor.cydsn/main.c`

## 🧪 验证方法
1. 重新编译项目
2. 如果初始化失败，应该在 ~5秒后超时并返回错误码
3. 查看串口输出中的诊断信息

## 📍 关键寄存器地址参考
- `0x0908` - ADIID（应为 0x4144）
- `0x0A28` - CHIPID（应为 0x5501 或 0x5502）
- `0x2080` - FIFOCON（FIFO配置）
- `0x2084` - FIFOSTA（FIFO状态）

## 🔧 硬件检查清单
- [ ] SCLK/MOSI/MISO/CS/RST 接线正确
- [ ] GND 连接良好
- [ ] 供电电压 3.3V
- [ ] SPI 时钟频率 ≤ 400 kHz
- [ ] 使用示波器检查 SPI 波形
