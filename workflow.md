# 芯片驱动开发协作工作流 v1.0

## 1. 第一阶段：手册解析与功能总结

拿到芯片数据手册后，必须输出以下内容，**每一项均标注手册页码**：

- **基本功能与核心参数**：例如分辨率、通道数、接口类型、工作电压/温度、功耗、封装等。
- **不同型号/封装差异**（若有）。
- **引脚功能表**。
- **基准电压**：（若有）内/外部基准、输入范围、是否可配，需特别明确。
- **寄存器/命令格式**：
  - 若有寄存器，列出地址、功能、位定义、读写方法。
  - 若无传统寄存器，仅通过命令帧配置（如本例DAC的24位移位寄存器），必须说明，并给出字段定义。
- **操作芯片的完整步骤**：初始化流程、配置发送、输出/读取触发、时序要求等，逐步对应手册出处。

## 2. 第二阶段：驱动框架与代码生成

### 2.1 设计原则
- **面向对象风格**：抽象设备结构体 + 函数指针，方便替换芯片。
- **硬件解耦**：GPIO、SPI收发等硬件操作均定义为函数指针，驱动只实现芯片逻辑，用户填充具体操作。
- **静态分配**：设备实例及缓冲区使用静态全局变量，避免动态内存。
- **注释完善**：函数头含功能、参数、返回值、注意事项，关键步骤注明手册页码。

### 2.2 接口要求（以DAC为例）
抽象设备结构体应包含至少以下方法指针（可扩展）：
- `init`：初始化（地址、模式、初始输出）
- `set_voltage` / `write`：设置输出值
- `get_voltage` / `read`：读回当前值（可选）
- `power_down` / `standby`：低功耗模式
- `update_all`：触发硬件更新（若支持）
- `broadcast_cmd`：广播指令（若有）

### 2.3 多配置选项适配（关键）
当芯片支持多种工作模式（如软件/硬件更新、连续/单次转换），**必须实现所有选项的适配逻辑**，通过设备结构体中的模式字段和初始化配置接口选择。代码完成后主动询问用户选用哪种模式，不可仅实现一种。

### 2.4 文件组织
adc_device.h / dac_device.h # 抽象设备接口
bsp_<芯片型号>.h / .c # 具体驱动实现

### 2.5 示例代码
**抽象设备代码示例（dac_device.h 核心）**
/**
 * @file dac_device.h
 * @brief DAC 驱动程序总体框架（面向对象）
 *
 * @author lichanghong
 * @date 2026-05-06
 */

#ifndef __DAC_DEVICE_H__
#define __DAC_DEVICE_H__

#include "gd32h7xx.h"
#include <stdbool.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dac_device;  // 前向声明

/**
 * @brief DAC 控制引脚结构体
 * @note  根据具体芯片需要添加，本驱动未使用全部
 */
typedef struct {
    /* 使能引脚（可选） */
    uint32_t enable_gpio_port;
    uint32_t enable_gpio_pin;

    /* 硬件触发更新引脚 */
    uint32_t ldac_gpio_port;
    uint32_t ldac_gpio_pin;

    /* 硬件地址引脚 A0, A1 - 由驱动初始化时拉低/拉高 */
    uint32_t addr_a0_gpio_port;
    uint32_t addr_a0_gpio_pin;
    uint32_t addr_a1_gpio_port;
    uint32_t addr_a1_gpio_pin;
} dac_control_gpio;

/**
 * @brief SPI 传输函数指针类型
 * @param self         设备实例
 * @param tx_data      发送数据缓冲区（多字节命令）
 * @param transfer_size 发送字节数
 * @param receive_size  接收字节数（0表示只写）
 * @return 1=成功，0=失败
 */
typedef uint8_t (*spi_transfer_func_t)(struct dac_device* self,
                                       volatile uint8_t* tx_data,
                                       uint16_t transfer_size,
                                       uint16_t receive_size);

/**
 * @brief GPIO 控制函数指针类型
 * @param self  设备实例
 * @param state 1=高电平，0=低电平
 */
typedef void (*gpio_control_func_t)(struct dac_device* self, uint8_t state);

/**
 * @brief DAC 设备结构体（抽象类）
 */
typedef struct dac_device {
    /*------ 硬件接口（由用户实现）------*/
    gpio_control_func_t cs_set;         // CS 片选控制
    spi_transfer_func_t spi_transfer;   // SPI 传输函数
    gpio_control_func_t ldac_set;       // LDAC 引脚控制（硬件更新模式时需要）
    gpio_control_func_t enable_set;     // nENABLE 引脚控制

    /*------ 硬件接口参数 ------*/
    uint32_t cs_gpio_port;
    uint32_t cs_gpio_pin;
    uint32_t spi;                       // SPI 外设编号（如 SPI4）

    /*------ 控制引脚配置 ------*/
    dac_control_gpio control;

    /*------ 芯片状态 ------*/
    uint8_t  refcount;                  // 引用计数
    uint8_t  update_mode;               // 更新模式（见下宏定义）
    uint8_t  channel_count;             // 通道数
    uint8_t  resolution;                // 分辨率（位）

    /*------ 驱动方法（由具体芯片实现）------*/
    uint8_t (*init)       (struct dac_device* self);
    uint8_t (*set_voltage)(struct dac_device* self, uint8_t channel, uint16_t value);
    uint8_t (*get_voltage)(struct dac_device* self, uint8_t channel, uint16_t* value);
    uint8_t (*power_down)(struct dac_device* self, uint8_t channel, uint8_t mode);
    uint8_t (*update_all)(struct dac_device* self);                      // 硬件模式下触发更新
    uint8_t (*broadcast_cmd)(struct dac_device* self, uint8_t cmd_type, uint16_t data);
} dac_device;

/* 更新模式 */
#define DAC_UPDATE_MODE_SOFTWARE  1    // LDAC 接地，SPI 命令控制更新
#define DAC_UPDATE_MODE_HARDWARE  0    // LDAC 引脚触发更新

typedef dac_device* dac_device_t;

#ifdef __cplusplus
}
#endif

#endif /* __DAC_DEVICE_H__ */

**芯片驱动具体实现示例（bsp_sgm5352.h 核心）**
/**
 * @file bsp_sgm5352_16.h
 * @brief SGM5352-16 DAC 驱动程序
 *
 *   参数类别              具体数值 / 特性
 *   分辨率                16 位，单调性保证
 *   通道数                4 通道电压输出
 *   基准电压              外部基准 VREFH，VREFL=0V
 *   接口                  3线 SPI，24位指令
 *   更新方式              软件同步更新 / 硬件LDAC更新
 *   关断模式              可编程输出高阻、1kΩ/100kΩ到地
 *   工作电压  AVDD        2.7 ~ 5.5V
 *   数字供电  IOVDD       1.8 ~ 5.5V
 *   封装                 TSSOP-16 / WLCSP-1.64×1.62-16B
 *
 * @note  详细功能参见芯片手册 SGM5352-16
 * @author lichanghong
 * @date 2026-05-06
 */

#ifndef __BSP_SGM5352_16_H__
#define __BSP_SGM5352_16_H__

#include "dac_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 芯片参数宏
 *============================================================================*/
#define SGM5352_RESOLUTION      16
#define SGM5352_CHANNELS        4

/*==============================================================================
 * 24位指令位定义（便于构造命令）
 * Page 17, Figure 4
 *============================================================================*/
#define SGM5352_CMD_READ           0x00080000   /* DB19=1 读操作 */
#define SGM5352_CMD_WRITE          0x00000000   /* DB19=0 写操作 */
#define SGM5352_CH_A               0x00000000   /* DB[18:17]=00 通道A */
#define SGM5352_CH_B               0x00020000
#define SGM5352_CH_C               0x00040000
#define SGM5352_CH_D               0x00060000

/* LD[1:0] 加载模式 */
#define SGM5352_LD_BUFFER_ONLY     0x00000000   /* 00 只写缓冲器 */
#define SGM5352_LD_UPDATE_SEL      0x00100000   /* 01 更新选中通道 */
#define SGM5352_LD_UPDATE_ALL      0x00200000   /* 10 同步更新所有通道 */
#define SGM5352_LD_BROADCAST       0x00300000   /* 11 广播模式 */

/* 关断模式 PD[2:0] (DB16:14) */
#define SGM5352_PD_NORMAL          0x00000000   /* 000 正常工作 */
#define SGM5352_PD_HIGH_Z          0x00008000   /* 001 输出高阻 */
#define SGM5352_PD_1K_TO_GND       0x00010000   /* 010 1kΩ到GND */
#define SGM5352_PD_100K_TO_GND     0x00018000   /* 011 100kΩ到GND */

/* 广播命令子类型（DB[16:14]组合）*/
#define SGM5352_BCAST_UPDATE_ALL   0x00120000   /* 更新所有设备同步输出 */
#define SGM5352_BCAST_WRITE_ALL    0x001A0000   /* 写并更新所有设备 */
#define SGM5352_BCAST_PD_ALL       0x001B0000   /* 所有设备关断 */

#define SGM5352_DATA_MASK          0x0000FFFF

/*==============================================================================
 * 芯片专用方法，外部接口函数
 *============================================================================*/
dac_device* dac_sgm5352_create(dac_device* device,
                                  uint32_t cs_gpio_port, uint32_t cs_gpio_pin,
                                  uint32_t spi, dac_control_gpio control);

/* 内部函数声明（供 create 绑定） */
static uint8_t sgm5352_init(dac_device* self);
static uint8_t sgm5352_set_voltage(dac_device* self, uint8_t channel, uint16_t value);
static uint8_t sgm5352_get_voltage(dac_device* self, uint8_t channel, uint16_t* value);
static uint8_t sgm5352_power_down(dac_device* self, uint8_t channel, uint8_t mode);
static uint8_t sgm5352_update_all(dac_device* self);
static uint8_t sgm5352_broadcast_cmd(dac_device* self, uint8_t cmd_type, uint16_t data);

static uint8_t sgm5352_send_command(dac_device* self, uint32_t command);
static uint32_t sgm5352_build_command(dac_device* self, uint8_t channel, uint16_t data,
                                         uint8_t load_mode, uint8_t pd_mode, uint8_t rw);

static void cs_set_func(dac_device* self, uint8_t state);
static void ldac_set_func(dac_device* self, uint8_t state);
static void enable_set_func(dac_device* self, uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SGM5352_16_H__ */

**芯片驱动具体实现示例（bsp_sgm5352.c 核心）**
/**
 * @file bsp_sgm5352_16.c
 * @brief SGM5352-16 DAC 驱动程序实现
 *
 * 面向对象设计，所有操作封装为方法。
 * 硬件相关接口（GPIO拉高拉低、SPI收发）留给用户实现，通过函数指针绑定。
 *
 * @note  芯片手册：SGM5352-16
 * @author lichanghong
 * @date 2026-05-06
 */

#include "bsp_sgm5352.h"

/* 静态实例，避免动态分配 */
static struct dac_device sgm5352_dev;

/* SPI 传输用的全局缓冲区（24位 = 3字节） */
static volatile uint8_t spi_tx_buf[3];
static volatile uint8_t spi_rx_buf[3];

/*==============================================================================
 * 外部接口：创建具体设备实例，绑定硬件接口和方法
 *============================================================================*/
dac_device* dac_sgm5352_create(dac_device* device,
                                  uint32_t cs_gpio_port, uint32_t cs_gpio_pin,
                                  uint32_t spi, dac_control_gpio control)
{
    device = &sgm5352_dev;

    /* 绑定硬件接口参数 */
    device->cs_gpio_port = cs_gpio_port;
    device->cs_gpio_pin  = cs_gpio_pin;
    device->spi          = spi;
    device->control      = control;

    /* 绑定硬件操作函数指针 - 用户需实现具体GPIO操作 */
    device->cs_set     = cs_set_func;
    device->ldac_set   = ldac_set_func;
    device->enable_set = enable_set_func;
    device->spi_transfer = NULL;   // 用户在 init 中需要自己赋值

    /* 绑定 DAC 驱动方法 */
    device->init         = sgm5352_init;
    device->set_voltage  = sgm5352_set_voltage;
    device->get_voltage  = sgm5352_get_voltage;
    device->power_down   = sgm5352_power_down;
    device->update_all   = sgm5352_update_all;
    device->broadcast_cmd = sgm5352_broadcast_cmd;

    /* 芯片属性 */
    device->channel_count = SGM5352_CHANNELS;
    device->resolution    = SGM5352_RESOLUTION;
    device->update_mode   = DAC_UPDATE_MODE_HARDWARE;  // 默认硬件模式
    device->refcount = 0;

    return device;
}

/*==============================================================================
 * 内部硬件操作桩（用户需在具体工程中实现）
 * 这里只给出接口，实际函数体需替换为你的 GPIO 操作代码。
 *============================================================================*/
static void cs_set_func(dac_device* self, uint8_t state)
{
    /* 用户实现：根据 self->cs_gpio_port/pin 拉高拉低 */
    // if(state) gpio_bit_set(self->cs_gpio_port, self->cs_gpio_pin);
    // else      gpio_bit_reset(self->cs_gpio_port, self->cs_gpio_pin);
}

static void ldac_set_func(dac_device* self, uint8_t state)
{
    /* 用户实现：LDAC 引脚控制，仅硬件更新模式使用 */
    // if(state) gpio_bit_set(self->control.ldac_gpio_port, self->control.ldac_gpio_pin);
    // else      gpio_bit_reset(self->control.ldac_gpio_port, self->control.ldac_gpio_pin);
}

static void enable_set_func(dac_device* self, uint8_t state)
{
    /* 用户实现：nENABLE 引脚控制，正常工作时为低电平 */
    // if(state) gpio_bit_set(self->control.enable_gpio_port, self->control.enable_gpio_pin);
    // else      gpio_bit_reset(self->control.enable_gpio_port, self->control.enable_gpio_pin);
}

/*==============================================================================
 * 私有函数：构建24位指令（根据手册 Page 17 Figure 4）
 *============================================================================*/
static uint32_t sgm5352_build_command(dac_device* self, uint8_t channel, uint16_t data,
                                         uint8_t load_mode, uint8_t pd_mode, uint8_t rw)
{
    uint32_t cmd = 0;

    /* DB[23:22] 地址位，必须匹配硬件引脚电平 (Page 17) */
    if (self->control.addr_a1) cmd |= 0x00800000;
    if (self->control.addr_a0) cmd |= 0x00400000;

    /* DB[21:20] 加载模式 */
    cmd |= ((uint32_t)load_mode & 0x03) << 20; // 软件模式下，写入就更新

    /* DB[19] 读/写 */
    cmd |= ((uint32_t)rw & 0x01) << 19;

    /* DB[18:17] 通道选择 */
    cmd |= ((uint32_t)(channel & 0x03)) << 17;

    /* DB[16:14] 关断模式 */
    cmd |= ((uint32_t)(pd_mode & 0x07)) << 14;

    /* DB[15:0] 数据（16位） */
    cmd |= (uint32_t)(data & 0xFFFF);

    return cmd;
}

/*==============================================================================
 * 核心SPI发送函数（发送24位命令，可选接收）
 *============================================================================*/
static uint8_t sgm5352_send_command(dac_device* self, uint32_t command)
{
    uint8_t rx_size = (command & SGM5352_CMD_READ) ? 3 : 0; // 读操作需接收3字节

    /* 将32位命令拆为3字节，MSB先发 (Page 6 Figure 1) */
    spi_tx_buf[0] = (uint8_t)((command >> 16) & 0xFF);
    spi_tx_buf[1] = (uint8_t)((command >> 8) & 0xFF);
    spi_tx_buf[2] = (uint8_t)(command & 0xFF);

    /* 拉低CS */
    if (self->cs_set) self->cs_set(self, 0);

    /* 调用用户提供的SPI传输函数 */
    if (self->spi_transfer) {
        if (self->spi_transfer(self, spi_tx_buf, 3, rx_size) == 0)
            return 0;
    } else {
        return 0;  // SPI传输函数未绑定
    }

    /* 如果是写操作，在发送完成后拉高CS（读操作在接收完成后拉高，此处简化处理） */
    if (rx_size == 0) {
        if (self->cs_set) self->cs_set(self, 1);
    }

    return 1;
}

/*==============================================================================
 * 驱动方法实现
 *============================================================================*/

/**
 * @brief 芯片初始化
 * @note  1. 硬件初始化（SPI、GPIO），用户需自行在 periph_init 中完成
 *        2. 拉低 nENABLE 使能芯片
 *        3. 设置地址匹配
 *        4. 所有通道输出清零
 */
static uint8_t sgm5352_init(dac_device* self)
{
    if (!self) return 0;

    if (self->refcount > 0) {
        self->refcount++;
        return 1;
    }

    /* 1. 用户需在此完成 SPI、GPIO 时钟、复用等初始化 */
    // sgm5352_16_periph_init(self);  // 用户自行实现

    /* 2. 使能芯片：nENABLE 拉低 (Page 18) */
    if (self->enable_set) self->enable_set(self, 0);

    /* 3. 若使用硬件更新模式，LDAC 初始为低电平 (Page 18) */
    if (self->update_mode == DAC_UPDATE_MODE_HARDWARE) {
        if (self->ldac_set) self->ldac_set(self, 0);
    }

    /* 4. 上电后 DAC 输出自动为 0V (Page 18)，无需额外操作。
     *    但可主动向所有通道写 0 确保状态。
     */
    for (uint8_t ch = 0; ch < SGM5352_CHANNELS; ch++) {
        self->set_voltage(self, ch, 0);
    }

    self->refcount = 1;
    return 1;
}

/**
 * @brief 设置指定通道输出电压
 * @param channel 通道号 0~3 (A~D)
 * @param value   16位数字量 (0~65535)
 * @note  内部会根据更新模式自动选择 LD[1:0] 设置
 */
static uint8_t sgm5352_set_voltage(dac_device* self, uint8_t channel, uint16_t value)
{
    if (!self || channel >= SGM5352_CHANNELS) return 0;

    uint32_t cmd;
    uint8_t ret;
    uint8_t load_mode;

    if (self->update_mode == DAC_UPDATE_MODE_SOFTWARE) {
        /* 软件同步更新模式：写缓冲并立即更新选定通道 (Page 18)
         * 也可以先写缓冲 (LD=00)，再发广播更新所有通道，此处直接更新选中通道。
         */
        load_mode = 0x01;  // LD[1:0]=01 写缓冲并更新选中通道
    } else {
        /* 硬件更新模式：只写缓冲器 (LD=00)，等待 LDAC 上升沿 */
        load_mode = 0x00;
    }

    cmd = sgm5352_build_command(self, channel, value, load_mode, 0x00, 0x00); // 正常模式

    // 开始发送数据
    ret = sgm5352_send_command(self, cmd);
    if(ret == 0) return 0;

    // 如果发送完成则更新,这里应该写在dma发送回调函数里面
    if(self->update_mode == DAC_UPDATE_MODE_HARDWARE) 
    {
        // 这里通过dma是否发送完成来老ladc
        if(sgm5352_update_all(self) == 0)
        {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 读取指定通道的当前数字量（读回 DAC 寄存器）
 * @note  仅当 LD[1:0] 为 00 或 01 时可读 (Page 18)
 *        实际使用时很少读回，这里实现完整逻辑。
 */
static uint8_t sgm5352_get_voltage(dac_device* self, uint8_t channel, uint16_t* value)
{
    if (!self || !value || channel >= SGM5352_CHANNELS) return 0;

    uint32_t cmd;

    /* 构建读指令：R/W=1，LD[1:0] 需设为 00 或 01 */
    cmd = sgm5352_build_command(self, channel, 0x0000, 0x00, 0x00, 0x01);

    /* 发送读指令（阻塞等待接收完成） */
    if (sgm5352_send_command(self, cmd) == 0)
        return 0;

    /* 从接收缓冲区合并16位数据（由 spi_transfer 写入） */
    *value = ((uint16_t)spi_rx_buf[0] << 8) | spi_rx_buf[1];
    return 1;
}

/**
 * @brief 设置指定通道的关断模式
 * @param mode 关断模式代码 (0=正常, 1=高阻, 2=1kΩ到GND, 3=100kΩ到GND)
 */
static uint8_t sgm5352_power_down(dac_device* self, uint8_t channel, uint8_t mode)
{
    if (!self || channel >= SGM5352_CHANNELS) return 0;

    uint32_t cmd;
    uint8_t pd_code;

    switch (mode) {
        case 1:  pd_code = 0x01; break;  // 高阻
        case 2:  pd_code = 0x02; break;  // 1kΩ到GND
        case 3:  pd_code = 0x03; break;  // 100kΩ到GND
        default: pd_code = 0x00; break;  // 正常
    }

    /* 关断模式写入，数据部分可为任意值，但建议写0 */
    /* 注意：根据手册，退出关断模式只需写入新数据即可 (Page 18) */
    cmd = sgm5352_build_command(self, channel, 0x0000,
                                   (self->update_mode == DAC_UPDATE_MODE_SOFTWARE) ? 0x01 : 0x00,
                                   pd_code, 0x00);
    return sgm5352_send_command(self, cmd);
}

/**
 * @brief 硬件更新模式：产生 LDAC 上升沿，同步更新所有通道
 */
static uint8_t sgm5352_update_all(dac_device* self)
{
    if (!self) return 0;
    if (self->update_mode != DAC_UPDATE_MODE_HARDWARE) return 0;
    if (self->ldac_set == NULL) return 0;

    /* 产生上升沿：低 -> 高 (Page 18) */
    self->ldac_set(self, 0);
    /* 脉冲宽度需满足手册要求（一般几个μs），这里用简单延时示意 */
    // rt_thread_mdelay(1);
    self->ldac_set(self, 1);
    return 1;
}

/**
 * @brief 广播命令（用于多芯片系统）
 * @param cmd_type 命令类型：0=同步更新，1=写并加载，2=全局关断
 * @param data     附加数据（关断码等）
 */
static uint8_t sgm5352_broadcast_cmd(dac_device* self, uint8_t cmd_type, uint16_t data)
{
    if (!self) return 0;

    uint32_t cmd = 0;
    /* 广播模式 LD[1:0] 固定为 11 */
    cmd |= SGM5352_LD_BROADCAST;

    /* 构建广播子类型 (Page 18 Table 3) */
    switch (cmd_type) {
        case 0: // 同步更新所有设备的缓冲器值到输出
            cmd |= SGM5352_BCAST_UPDATE_ALL;
            break;
        case 1: // 将所有设备写入相同数据并加载
            cmd |= SGM5352_BCAST_WRITE_ALL;
            cmd |= (data & 0xFFFF);
            break;
        case 2: // 全局关断
            cmd |= SGM5352_BCAST_PD_ALL;
            break;
        default:
            return 0;
    }

    return sgm5352_send_command(self, cmd);
}
