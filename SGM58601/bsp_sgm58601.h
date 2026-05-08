/**
 * @file bsp_sgm58601.h
 * @brief SGM58601 adc驱动程序
 * 
 *   参数类别	  具体数值 /       特性
 *   分辨率	      24 位，无失码
 *   最大数据输出速率	           60 kSPS
 *   积分非线性	   0.0012% FSR（典型值，PGA=1）
 *   无噪声分辨率	最高 22 位
 *   有效位数	最高 21.2 位（1.4 kHz 时）
 *   输入类型	4 路差分 或 8 路单端（可配置）
 *   PGA 增益	1, 2, 4, 8, 16, 32, 64, 128（二进制步进）
 *   输入缓冲器	可选，提高输入阻抗
 *   参考电压	差分输入 VREFP - VREFN，范围 0.5V ~ 2.6V
 *   模拟电源 AVDD	5V（4.75V ~ 5.25V）
 *   数字电源 DVDD	2.7V ~ 5V（推荐 3.3V）
 *   工作温度范围	-40℃ ~ +125℃
 *   封装	SSOP-28 或 TQFN-5×5-28L
 *   典型功耗	15mW（PGA=1，buffer off，DVDD=3.3V）
 *   自校准	支持自校准、系统校准、自动校准
 * 
 * @author lichanghong
 * @date 2026-04-13
 */

#ifndef __BSP_SGM58601_H__
#define __BSP_SGM58601_H__


#include <stdint.h>
#include <stdbool.h>
#include "adc_device.h"

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// 该类型adc结果字节数,和总通道
//==============================================================================
#define SGM58601_ADC_DATA_LEN    3
#define SGM58601_ADC_CHANNEL_NUM 8  // 单端模式下最多8通道


//==============================================================================
// 寄存器地址定义
//==============================================================================
typedef enum {
    SGM58601_REG_STATUS   = 0x00, /**< Status register */
    SGM58601_REG_MUX      = 0x01, /**< Input multiplexer control */
    SGM58601_REG_ADCON    = 0x02, /**< A/D control register */
    SGM58601_REG_DRATE    = 0x03, /**< Data rate register */
    SGM58601_REG_IO       = 0x04, /**< GPIO control register */
    SGM58601_REG_OFC0     = 0x05, /**< Offset calibration byte 0 */
    SGM58601_REG_OFC1     = 0x06, /**< Offset calibration byte 1 */
    SGM58601_REG_OFC2     = 0x07, /**< Offset calibration byte 2 */
    SGM58601_REG_FSC0     = 0x08, /**< Full-scale calibration byte 0 */
    SGM58601_REG_FSC1     = 0x09, /**< Full-scale calibration byte 1 */
    SGM58601_REG_FSC2     = 0x0A, /**< Full-scale calibration byte 2 */
    SGM58601_REG_STATUS2  = 0x0B  /**< Status register 2 */
} sgm58601_reg_addr;


//==============================================================================
// 命令定义
//==============================================================================
typedef enum {
    SGM58601_CMD_WAKEUP  =    0x00,    // 或 0xFF，完成同步/退出待机
    SGM58601_CMD_RDATA   =    0x01,    // 读取单次数据
    SGM58601_CMD_RDATAC   =   0x03,    // 连续读取数据模式
    SGM58601_CMD_SDATAC   =   0x0F,    // 停止连续读取模式
    SGM58601_CMD_SELFOCAL  =  0xF1,    // 自偏移校准
    SGM58601_CMD_SELFGCAL  =  0xF2,    // 自增益校准
    SGM58601_CMD_SELFICAL  =  0xF0,    // 自偏移+增益校准
    SGM58601_CMD_SYSOCAL  =   0xF3,    // 系统偏移校准
    SGM58601_CMD_SYSGCAL  =   0xF4,    // 系统增益校准
    SGM58601_CMD_SYNC    =    0xFC,    // 同步
    SGM58601_CMD_STANDBY   =  0xFD,    // 待机模式
    SGM58601_CMD_RESET    =   0xFE,    // 复位
    SGM58601_CMD_WAKEUP_ALT = 0xFF    // 备用的WAKEUP命令
} sgm58601_cmd;


//==============================================================================
// 数据速率配置 (DRATE 寄存器值)
//==============================================================================
typedef enum {
    SGM58601_DRATE_60000 = 0xF1, /**< 60000 SPS */
    SGM58601_DRATE_30000 = 0xF0, /**< 30000 SPS (default) */
    SGM58601_DRATE_15000 = 0xE0, /**< 15000 SPS */
    SGM58601_DRATE_7500  = 0xD0, /**< 7500 SPS */
    SGM58601_DRATE_3750  = 0xC0, /**< 3750 SPS */
    SGM58601_DRATE_2000  = 0xB0, /**< 2000 SPS */
    SGM58601_DRATE_1000  = 0xA1, /**< 1000 SPS */
    SGM58601_DRATE_500   = 0x92, /**< 500 SPS */
    SGM58601_DRATE_100   = 0x82, /**< 100 SPS */
    SGM58601_DRATE_60    = 0x72, /**< 60 SPS */
    SGM58601_DRATE_50    = 0x63, /**< 50 SPS */
    SGM58601_DRATE_30    = 0x53, /**< 30 SPS */
    SGM58601_DRATE_25    = 0x43, /**< 25 SPS */
    SGM58601_DRATE_15    = 0x33, /**< 15 SPS */
    SGM58601_DRATE_10    = 0x23, /**< 10 SPS */
    SGM58601_DRATE_5     = 0x13, /**< 5 SPS */
    SGM58601_DRATE_2_5   = 0x03  /**< 2.5 SPS */
} sgm58601_data_rate;


//==============================================================================
// PGA 增益配置 (ADCON 寄存器 PGA[2:0] 位)
//==============================================================================
typedef enum{
    SGM58601_PGA_1   = 0x00, /**< Gain = 1 */
    SGM58601_PGA_2   = 0x01, /**< Gain = 2 */
    SGM58601_PGA_4   = 0x02, /**< Gain = 4 */
    SGM58601_PGA_8   = 0x03, /**< Gain = 8 */
    SGM58601_PGA_16  = 0x04, /**< Gain = 16 */
    SGM58601_PGA_32  = 0x05, /**< Gain = 32 */
    SGM58601_PGA_64  = 0x06, /**< Gain = 64 */
    SGM58601_PGA_128 = 0x07  /**< Gain = 128 */
} sgm58601_pga_gain;


//==============================================================================
// 单端模式通道选择 (MUX 寄存器值)
// PSEL = 通道号 (0-7), NSEL = 1000 (AINCOM)
//==============================================================================
typedef enum {
    SGM58601_MUX_SE_CH0  =    0x08 ,   // AIN0 - AINCOM
    SGM58601_MUX_SE_CH1  =    0x18 ,   // AIN1 - AINCOM
    SGM58601_MUX_SE_CH2  =    0x28 ,  // AIN2 - AINCOM
    SGM58601_MUX_SE_CH3  =    0x38 ,   // AIN3 - AINCOM
    SGM58601_MUX_SE_CH4  =    0x48 ,   // AIN4 - AINCOM
    SGM58601_MUX_SE_CH5  =    0x58 ,   // AIN5 - AINCOM
    SGM58601_MUX_SE_CH6  =    0x68 ,    // AIN6 - AINCOM
    SGM58601_MUX_SE_CH7   =   0x78    // AIN7 - AINCOM
}single_input_channel;


//==============================================================================
// 差分模式通道对选择 (MUX 寄存器值)
//==============================================================================
typedef enum {
    SGM58601_MUX_DIFF_CH01 =  0x01,    // AIN0 - AIN1
    SGM58601_MUX_DIFF_CH23 =  0x23,    // AIN2 - AIN3
    SGM58601_MUX_DIFF_CH45 =  0x45,   // AIN4 - AIN5
    SGM58601_MUX_DIFF_CH67 =  0x67   // AIN6 - AIN7
}multi_input_channel;



//==============================================================================
// 该类型adc对外接口函数
//==============================================================================
adc_device* adc_sgm58601_create(adc_device* device, uint32_t cs_gpio_port, 
                                        uint32_t cs_gpio_pin, uint32_t spi, adc_control_gpio control_gpio);


//==============================================================================
// 该类型adc内部函数
//==============================================================================
static uint8_t sgm58601_init(adc_device* self);

static uint8_t sgm58601_periph_init(adc_device* self);

static uint8_t send_spi_command(adc_device*  self, volatile uint8_t* command_and_data, 
                                    uint16_t transfer_size, uint16_t receive_size) ;

static uint8_t sgm58601_spi4_send(adc_device* self,volatile uint8_t* data, uint16_t transfer_size, uint16_t receive_size);

static void cs_set_func(adc_device* self, uint8_t state) ;

static void reset_set_func(adc_device* self, uint8_t state) ;

static void sync_set_func(adc_device* self, uint8_t state) ;

static uint8_t sgm58601_start_convert(adc_device* self, uint8_t channel);

static uint8_t sgm58601_stop_convert(adc_device* self);

static uint32_t sgm58601_get_result(adc_device* self, uint8_t channel);

static uint8_t sgm58601_set_pga(adc_device* self, uint8_t gain);

static uint8_t sgm58601_set_channel(adc_device* self, uint8_t channel);

static uint8_t sgm58601_set_rate(adc_device* self, uint8_t rate);

static uint8_t sgm58601_writeregister(adc_device* self, uint8_t reg_addr, uint8_t data);

static uint8_t sgm58601_readregister(adc_device* self, uint8_t reg_addr);

static uint8_t sgm58601_selfcalibrate(adc_device* self);

static uint8_t sgm58601_reset(adc_device* self);


#ifdef __cplusplus
}
#endif

#endif 
