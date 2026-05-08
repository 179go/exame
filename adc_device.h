/**
 * @file adc_device.h
 * @brief adc驱动程序总体框架
 * 
 * @author lichanghong
 * @date 2026-04-13
 */

#ifndef __ADC_DEVICE_H__
#define __ADC_DEVICE_H__

#include "gd32h7xx.h"
#include <stdbool.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif


// 前向类申明
struct adc_device; 

// ADC类型芯片需要控制引脚较多，定义控制引脚结构体
typedef struct {
    
    // 转换完成控制引脚
    uint32_t convert_complete_gpio_port;
    uint32_t convert_complete_gpio_pin;

    // 控制转换完成中断源头
    IRQn_Type convert_complete_irqn; // 中断事件源头
    uint8_t convert_complete_port_irq_source;
    uint8_t convert_complete_pin_irq_source;
    exti_line_enum convert_complete_exti_line; // 外部中断线号
    exti_trig_type_enum convert_complete_exti_trig_type; // 外部中断触发类型

    // 多adc同步引脚
    uint32_t sync_gpio_port;
    uint32_t sync_gpio_pin;

    // adc 复位引脚
    uint32_t reset_gpio_port;
    uint32_t reset_gpio_pin;

} adc_control_gpio;

/**
 * @brief SPI传输函数指针类型
 * @param self 实例化结构体
 * @param command_and_data 要发送的数据
 * @param length 数据大小（字节数）
 */
typedef uint8_t (*spi_transfer_func_t)(struct adc_device* self, volatile uint8_t* command_and_data, uint16_t transfer_size, uint16_t receive_size);

/**
 * @brief GPIO控制函数指针类型
 * @param self 实例化结构体
 * @param state 引脚状态（1=高电平，0=低电平）
 */
typedef void (*gpio_control_func_t)(struct adc_device* self, uint8_t state);

typedef struct adc_device {

    // 硬件接口函数指针
    gpio_control_func_t cs_set;         ///< CS片选引脚控制函数
    spi_transfer_func_t spi_transfer;   ///< SPI传输函数

    // 硬件spi接口参数 
    uint32_t cs_gpio_port;              ///< CS引脚端口号
    uint32_t cs_gpio_pin;               ///< CS引脚引脚号
    uint32_t spi;                       ///< 具体使用的spi外设

    // 硬件控制引脚
    adc_control_gpio control_gpio;      ///< ADC控制引脚
    
    uint8_t refcount;                   ///< 引用计数

    /**************************** 驱动方法函数指针（使用 adjres_device_t 或 struct adjres_device*）*************************** */ 
    // 初始化函数
    uint8_t  (*init)   (struct adc_device* self);

    // 开始转换函数
    uint8_t (*start_convert) (struct adc_device* self, uint8_t channel);

    // 读取函数
    uint32_t (*get_convert_value) (struct adc_device* self, uint8_t channel);

    // 停止转换函数
    uint8_t (*stop_convert) (struct adc_device* self);

}adc_device;

// 将类抽象出去
typedef adc_device* adjres_device_t;

#ifdef __cplusplus
}
#endif

#endif /* __ADC_DEVICE_H__ */
