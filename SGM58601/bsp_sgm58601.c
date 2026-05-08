/**
 * @file bsp_sgm58601.c
 * @brief bsp_sgm58601 驱动程序
 * 
 * 该文件实现了bsp_sgm58601的驱动程序,驱动程序的面向对象设计。
 * 关于该芯片具体描述间头文件中有详细的注释。
 * 
 * @author lichanghong
 * @date 2026-04-15
 * 
 */

 #include "bsp_sgm58601.h"

 /* 这里先静态创建要用的电位器实例，再传给create函数，以避免使用malloc函数造成不确定性*/
static struct adc_device sgm58601_1;

// 定义发送和接收数组，全局避免使用malloc开辟
static volatile uint8_t spi4_transfer_data[4]; // 根据芯片手册，该类型的芯片发送控制命令最多2字节, 外加一个字节的数据位
static volatile uint8_t spi4_receive_data[SGM58601_ADC_DATA_LEN * SGM58601_ADC_CHANNEL_NUM]; // 接收数组, 3个字节，最多8通道

// 存放转换后原始值
static volatile uint32_t adc_raw_value[SGM58601_ADC_CHANNEL_NUM]; // 存放转换后的原始值

// 由于每次发送的数据大小和接收的数据量有关，因此定义一个全局变量用来指示当前需要传输的字节数
static volatile uint8_t spi4_dma_receive_complete = 0;

// 本芯片每次的接收数据链也不一样，因此需要全局变量记录一下
static volatile uint16_t spi4_receive_size = 0;

// 定义adc通道切换变量
static volatile uint8_t adc_current_channel = 0;



/******************************** 外部接口函数 **************************************/
/**
 * @brief 创建并初始化w25q64jvssiq驱动实例
 * @param device 驱动实例指针,外部创建指针传入
 * @param bank_select 选择具体的bank
 * @return 初始化好的电位器设备指针
 */
adc_device* adc_sgm58601_create(adc_device* device, uint32_t cs_gpio_port, 
                                        uint32_t cs_gpio_pin, uint32_t spi, adc_control_gpio control_gpio)
{
    // 这里采用静态分配方法较为安全
    device = &sgm58601_1; 

    // 初始化硬件接口
    device->cs_gpio_port = cs_gpio_port;
    device->cs_gpio_pin = cs_gpio_pin;
    device->spi = spi;
    
    // 初始化硬件控制引脚
    device->control_gpio = control_gpio;

    // 初始化硬件接口函数 
    device->cs_set = cs_set_func;
    device->spi_transfer = sgm58601_spi4_send;
    
    // 绑定总设备共享方法
    device->init = sgm58601_init;
    device->start_convert = sgm58601_start_convert;
    device->get_convert_value = sgm58601_get_result;
    device->stop_convert = sgm58601_stop_convert;


    device->refcount = 0; // 实例化次数初始化为0
    
    return device;
}


/******************************** 与sgm58601相关的硬件初始化函数 **************************************/
/**
 * @brief 默认初始化函数
 */
static uint8_t sgm58601_init(adc_device* self)
{
    if (self == NULL) {
        return false;
    }
    
    // 检查是否被初始化过
    if (self->refcount > 0) {
        self->refcount += 1;
        return true;
    }

    // 开启硬件初始化
    if(sgm58601_periph_init(self) == 0)
    {
        self->refcount -= 1;
        return 0;
    }

    // 开启软件初始化
    /* 1、先进行复位操作 */
    if(sgm58601_reset(self) == 0)
    {
        return 0;
    }

    /* 2、配置PGA参数 */
    if(sgm58601_set_pga(self, SGM58601_PGA_1) == 0)
    {
        return 0;
    }

    /* 3、配置ADC速率 */
    if(sgm58601_set_rate(self, SGM58601_DRATE_60000) == 0)
    {
        return 0;
    }

    /* 4、配置ADC通道 */
    adc_current_channel = 0; // 选择单端输入0通道
    if(sgm58601_set_channel(self, adc_current_channel) == 0) // 配置为单端输入0通道
    {
        return 0;
    }

    /* 4、工作前先子校准一次 */
    if(sgm58601_selfcalibrate(self) == 0) // 配置为单端输入0通道
    {
        return 0;
    }

    /* 5、开始验证上述配置 */
    uint8_t register_data = sgm58601_readregister(self, SGM58601_REG_ADCON);
    if(register_data != (SGM58601_PGA_1 & 0x07))
    {
        rt_kprintf("sgm58601_init: PGA WRONG !!\n");
        return 0;
    }

    register_data = sgm58601_readregister(self, SGM58601_REG_DRATE);
    if(register_data != SGM58601_DRATE_60000)
    {
        rt_kprintf("sgm58601_init: DRATE WRONG !!\n");
        return 0;
    }   

    return  1;
}

/**
 * @brief 初始化外设硬件根据具体原理图调整
 * @note 初始化函数在GD32标准库没有返回参数，因此该函数恒为1
 */
static uint8_t sgm58601_periph_init(adc_device* self)
{
 /* enable the CS GPIO AND SPI clock */
    rcu_periph_clock_enable(RCU_SYSCFG);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_GPIOF);
    rcu_periph_clock_enable(RCU_GPIOG);
    

    
    // 初始化转换完成引脚，配置为中断模式
    gpio_mode_set(self->control_gpio.convert_complete_gpio_port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, self->control_gpio.convert_complete_gpio_pin);

    /* enable and set key EXTI interrupt to the lowest priority */
    nvic_irq_enable(self->control_gpio.convert_complete_irqn, 2U, 0U); // 优先保证spi通讯的中断不被打断

    /* connect key EXTI line to key GPIO pin */
    syscfg_exti_line_config(self->control_gpio.convert_complete_port_irq_source, self->control_gpio.convert_complete_pin_irq_source);

    /* configure key EXTI line */
    exti_init(self->control_gpio.convert_complete_exti_line, EXTI_INTERRUPT, self->control_gpio.convert_complete_exti_trig_type); // 配置下降沿触发
    exti_interrupt_flag_clear(self->control_gpio.convert_complete_exti_line);



    // 初始化同步引脚，配置为输出模式
    gpio_mode_set(self->control_gpio.sync_gpio_port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, self->control_gpio.sync_gpio_pin);
    gpio_output_options_set(self->control_gpio.sync_gpio_port, GPIO_OTYPE_PP, GPIO_OSPEED_100_220MHZ, self->control_gpio.sync_gpio_pin);



    // 初始化复位引脚
    gpio_mode_set(self->control_gpio.reset_gpio_port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, self->control_gpio.reset_gpio_pin);
    gpio_output_options_set(self->control_gpio.reset_gpio_port, GPIO_OTYPE_PP, GPIO_OSPEED_100_220MHZ, self->control_gpio.reset_gpio_pin);



    /* configure CS GPIO pin  CS-PE4*/
    // 根据电路图初始化不同cs
    gpio_mode_set(self->cs_gpio_port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, self->cs_gpio_pin);
    gpio_output_options_set(self->cs_gpio_port, GPIO_OTYPE_PP, GPIO_OSPEED_100_220MHZ, self->cs_gpio_pin);

    /* configure SPI4 */
    // 先开启外设时钟
    rcu_periph_clock_enable(RCU_SPI4);
    rcu_spi_clock_config(IDX_SPI4, RCU_SPISRC_PLL0Q); // 注意这个RCU_SPISRC_PLL0Q要配置使能并且合适分频

    // 配置SPI的通讯引脚
    /* connect port to SPI4_CS->PF6
                       SPI4_SCK->PF7
                       SPI4_MISO->PF8   
                       SPI4_MOSI->PF9 */
    gpio_af_set(GPIOF, GPIO_AF_5, GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);
    
    gpio_mode_set(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);
    gpio_output_options_set(GPIOF, GPIO_OTYPE_PP, GPIO_OSPEED_100_220MHZ, GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);

    // 配置spi外设 ---- 这里关于dma的配置以及spi的配置固定为spi2
    spi_parameter_struct spi_init_struct;

    /* deinitialize SPI and the parameters */
    spi_i2s_deinit(self->spi);
    spi_struct_para_init(&spi_init_struct);

    /* SPI4 parameter configuration */
    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.data_size            = SPI_DATASIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;  // 数据在上升沿被锁存
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = SPI_PSC_16;  // 分频系数需要根据具体原理图调整，时钟来源是PLLQ
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(self->spi, &spi_init_struct);

    /* enable SPI byte access */
    spi_byte_access_enable(self->spi);

    // 配置dma时钟
    rcu_periph_clock_enable(RCU_DMA1);
    rcu_periph_clock_enable(RCU_DMAMUX); 

    /* deinitialize DMA registers of a channel */
    dma_single_data_parameter_struct dma_init_struct;
    dma_deinit(DMA1, DMA_CH0); // 这里只需要初始化发送
    dma_deinit(DMA1, DMA_CH1); // 这里只需要初始化发送
    dma_single_data_para_struct_init(&dma_init_struct);

    /* SPI4 transmit DMA config: DMA1_CH0  SPI4_TX*/
    dma_init_struct.request = DMA_REQUEST_SPI4_TX;
    dma_init_struct.direction = DMA_MEMORY_TO_PERIPH;    
    dma_init_struct.memory0_addr = (uint32_t)spi4_transfer_data;   // 在具体发送时候配置
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.number = 8; // 初始时候先设置8个字节
    dma_init_struct.periph_addr = (uint32_t)&SPI_TDATA(SPI4);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH0, &dma_init_struct);

    /* SPI4 receive DMA config: DMA1_CH1  SPI4_RX*/
    dma_init_struct.request = DMA_REQUEST_SPI4_RX;
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;    
    dma_init_struct.memory0_addr = (uint32_t)spi4_receive_data;
    dma_init_struct.periph_addr = (uint32_t)&SPI_RDATA(SPI4);
    dma_init_struct.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_single_data_mode_init(DMA1, DMA_CH1, &dma_init_struct);   

    /* enable DMA channel */
    dma_channel_enable(DMA1, DMA_CH0);
    dma_channel_enable(DMA1, DMA_CH1);

    // 配置中断
    /* 使能DMA传输完成中断 */
    dma_interrupt_enable(DMA1, DMA_CH0, DMA_INT_FTF);  // 使能传输完成中断
    dma_interrupt_enable(DMA1, DMA_CH1, DMA_INT_FTF);  // 使能接收完成中断

    /* 配置NVIC中断组 */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);

    /* 配置NVIC中断优先级 */
    nvic_irq_enable(DMA1_Channel0_IRQn, 1, 0);  // 优先级可根据需要调整
    nvic_irq_enable(DMA1_Channel1_IRQn, 1, 1);  // 优先级可根据需要调整
    dma_flag_clear(DMA1, DMA_CH0, DMA_FLAG_FTF); 
    dma_flag_clear(DMA1, DMA_CH1, DMA_FLAG_FTF); 

    return 1;
}

/**
 * @brief 私有函数：发送SPI命令到MCP42050
 * 
 * @param self 驱动实例指针
 * @param command 命令码
 * @param data 数据值
 * @return 1 发送成功
 * @return 0 发送失败
 * @note gd32的标准库函数发送后不反悔数据，因此该函数恒定为1
 */
static uint8_t send_spi_command(adc_device*  self, volatile uint8_t* command_and_data, 
                                    uint16_t transfer_size, uint16_t receive_size) 
{
    if (self == NULL || self->spi_transfer == NULL) {
        return 0;
    }
    
    // 拉低CS片选引脚
    if (self->cs_set != NULL) {
        self->cs_set(self, 0);
    }
    
    spi4_receive_size = receive_size; // 记录接收字节数

    // 发送数据和命令字节
    self->spi_transfer(self, command_and_data, transfer_size, receive_size);  // spi_transfer函数指针指向了sgm58601_spi4_send
    
    // 拉高在dma发送完成中断里面拉高

    return 1;
}

/**
 * @brief 针对本硬件的spi3发送函数
 */
static uint8_t sgm58601_spi4_send(adc_device* self, volatile uint8_t* data, uint16_t transfer_size, uint16_t receive_size)
{
    // 确保先关闭，配置，再打开 --- 确保接收已经完成
    spi_disable(self->spi);
    spi_dma_disable(self->spi, SPI_DMA_TRANSMIT);
    spi_dma_disable(self->spi, SPI_DMA_RECEIVE);
    
    // 先配置数据, 更换硬件时候需要修改本dma通道
    dma_memory_address_config(DMA1, DMA_CH0, DMA_MEMORY_0, (uint32_t)data); // 这里的data要用全局变量，dma非阻塞，所以传参要穿全局变量
    dma_transfer_number_config(DMA1, DMA_CH0, transfer_size); // 配置发送字节数
    
    /* SPI enable and dma enable*/
    spi_dma_enable(self->spi, SPI_DMA_TRANSMIT);

    // 需要接收时候配置并使能
    if(receive_size > 0)
    {
        // 再配置接收数据, 更换硬件时候需要修改本dma通道
        dma_memory_address_config(DMA1, DMA_CH1, DMA_MEMORY_0, (uint32_t)spi4_receive_data); // 这里的spi4_receive_data要用全局变量，dma非阻塞，所以传参要穿全局变量
        dma_transfer_number_config(DMA1, DMA_CH1, receive_size); // 配置接收字节数

        /* SPI enable and dma enable*/
        spi_dma_enable(self->spi, SPI_DMA_RECEIVE);
    }

    /* SPI enable and dma enable*/
    spi_enable(self->spi);

    /* SPI master start transfer */
    spi_master_transfer_start(self->spi, SPI_TRANS_START); // 开始发送

    return 1;
}

/**
 * @brief 针对本硬件的spi4片选GPIO拉高拉低函数
 * @param self 驱动实例指针
 * @param state 状态，1/0
 */
static void cs_set_func(adc_device* self, uint8_t state) 
{
    if(state)
    {
        gpio_bit_set(self->cs_gpio_port, self->cs_gpio_pin);
    }
    else 
    {
        gpio_bit_reset(self->cs_gpio_port, self->cs_gpio_pin);
    }
}

/**
 * @brief 复位引脚控制函数
 * @param self 驱动实例指针
 * @param state 状态，1/0
 */
static void reset_set_func(adc_device* self, uint8_t state) 
{
    if(state)
    {
        gpio_bit_set(self->control_gpio.reset_gpio_port, self->control_gpio.reset_gpio_pin);
    }
    else 
    {
        gpio_bit_reset(self->control_gpio.reset_gpio_port, self->control_gpio.reset_gpio_pin);
    }
}

/**
 * @brief 同步引脚控制函数
 * @param self 驱动实例指针
 * @param state 状态，1/0
 */
static void sync_set_func(adc_device* self, uint8_t state) 
{
    if(state)
    {
        gpio_bit_set(self->control_gpio.sync_gpio_port, self->control_gpio.sync_gpio_pin);
    }
    else 
    {
        gpio_bit_reset(self->control_gpio.sync_gpio_port, self->control_gpio.sync_gpio_pin);
    }
}

/**
 * @brief  SPI4 DMA发送完成中断服务函数
 * @note   处理SPI4 DMA发送完成中断，并清除中断标志位
 * @retval 无
 */
void DMA1_Channel0_IRQHandler(void)
{
    // 判断是否是发送完成中断
    if(dma_interrupt_flag_get(DMA1, DMA_CH0, DMA_INT_FLAG_FTF))
    {
        // 开始清理标志位
        dma_flag_clear(DMA1, DMA_CH0, DMA_FLAG_FTF);

        if(spi4_receive_size == 0)
        {
            // 拉高CS片选引脚
            cs_set_func(&sgm58601_1, 1);
        }
    }
}

/**
 * @brief  SPI4 DMA接收完成中断服务函数
 * @note   处理SPI4 DMA接收完成中断，并清除中断标志位
 * @retval 无
 */
void DMA1_Channel1_IRQHandler(void)
{
    // 判断是否是发送完成中断
    if(dma_interrupt_flag_get(DMA1, DMA_CH1, DMA_INT_FLAG_FTF))
    {
        dma_channel_disable(DMA1, DMA_CH1); // 关闭接收通道

        // 开始清理标志位
        dma_flag_clear(DMA1, DMA_CH1, DMA_FLAG_FTF);

        // 开始检查是否完全接受了数据,可加可不加，增加鲁棒性DMA_INT_FLAG_FTF代表以及完成接收指定数量字节
        if(spi4_receive_size - dma_transfer_number_get(DMA1, DMA_CH1) >= spi4_receive_size)
        {
            spi4_receive_size =0;
            spi4_dma_receive_complete = 1; // 接收完成标志位置1

            // 整合数据
            if(spi4_receive_size >= 3) // 至少需要三个字节才是adc转换结果
            {
                // 本驱动设计方式每次读取三个字节(1个通道)
                for(uint8_t i = 0; i <  3; i++)
                {
                    adc_raw_value[adc_current_channel] = spi4_receive_data[0] << 16 | 
                                        spi4_receive_data[1] << 8 | 
                                        spi4_receive_data[2]; // 左移16位，因为spi4接收的字节是24位的，右移8位，因为spi4接收的字节是24位的，右移0位，因为spi4接收的字节是24位的
                }
            }
            // 否则是其他寄存器返回的结果

            // 拉高CS片选引脚
            cs_set_func(&sgm58601_1, 1);
        }
        
        // 开启dma通道
        dma_channel_enable(DMA1, DMA_CH1);
    }
}

/**
 * @brief  转换完成中断
 * @note   下降沿触发，在转换完成时产生中断，当前引脚是PD11
 * @retval 无
 */
void EXTI15_10_IRQHandler(void)
{
    if(RESET != exti_interrupt_flag_get(sgm58601_1.control_gpio.convert_complete_exti_line))
    {
        // 开始清理标志位
        exti_interrupt_flag_clear(sgm58601_1.control_gpio.convert_complete_exti_line);

        if(spi4_receive_size == 0)
        {
            return ;
        }

        // 开始读取数据，这里发送三字节dummy数据，触发spi时钟
        // 但是这里再转换完成后，需要再次设置下一个通道，所以刚好抵消了上述三个字节，每次加一个通道

        // !!!!!!!!!!!
        // 这里需要注意,sgm58601_set_channel函数写入寄存器后，adc芯片立马开始计时，14个adc芯片内部时钟周期后，下一次开始转换
        // 所以这里可以尝试使用同步时钟来控制，但是芯片手册没说该方法，可能失败
        // !!!!!!!!!!!
        // sync_set_func(&sgm58601_1, 0);
        if(sgm58601_set_channel(&sgm58601_1,  ++adc_current_channel) == 0) // 这里切换通道会发送三个字节，刚好为DMA接收提供了三字节时钟
        {
            return ;
        }
        // sync_set_func(&sgm58601_1, 1); // 同步时钟恢复
    }
}


/**
 * @brief  开启转换函数
 * @note   这里用的是连续转换方式
 * @retval 无
 */
static uint8_t sgm58601_start_convert(adc_device* self, uint8_t channel)
{
    // 先停止任何可能正在进行的连续模式
    if(sgm58601_stop_convert(self) == 0)
    {
        return 0;
    }
    
    // 设置通道
    if(sgm58601_set_channel(self, channel) == 0)
    {
        return 0;
    }

    // 启动连续转换
    // 准备数据
    rt_memset((void*)spi4_transfer_data, 0, 1);
    rt_memset((void*)spi4_receive_data, 0, sizeof(spi4_receive_data));

    spi4_transfer_data[0] = SGM58601_CMD_RDATAC;  // 开始连续转换

    // 发送数据
    if(send_spi_command(self, spi4_transfer_data, 1, 3) == 0) // 24位，因此读取三个字节
    {
        return 0;
    }

    return 1;
}

/**
 * @brief  停止转换函数
 * @note   这里用的是连续转换方式
 * @retval 无
 */
static uint8_t sgm58601_stop_convert(adc_device* self)
{
    // 准备数据
    rt_memset((void*)spi4_transfer_data, 0, 1);

    spi4_transfer_data[0] = SGM58601_CMD_SDATAC;  // 停止连续转换

    // 发送数据
    if(send_spi_command(self, spi4_transfer_data, 1, 0) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief  获取转换结果函数
 * @note   直接从全局数组获取转换结果
 * @retval 无
 */
static uint32_t sgm58601_get_result(adc_device* self, uint8_t channel)
{
    if(channel > 0x07)
    {
        rt_kprintf("adc channel error");
        return 0;
    }
    else if(spi4_dma_receive_complete != 1)
    {
        rt_kprintf("adc 当前读取通道为转换完成");
        return 0;
    }
        spi4_dma_receive_complete = 0; // 接收完成清空

        return adc_raw_value[channel];

}

/**
 * @brief  配置PGA增益函数
 * @note   这里用的是连续转换方式
 * @retval 无
 */
static uint8_t sgm58601_set_pga(adc_device* self, uint8_t gain)
{
    // 主要配置ADCON寄存器，增益和SDCS保持默认，由于没有到di do口所有关闭时钟输出
    uint8_t adcon_value = (gain & 0x07) ; // 增益控制位是低三位

    // 关闭IO口时钟输出
    adcon_value |= (0x00 << 5); // 时钟输出关闭

    // 调用写寄存器函数
    if(sgm58601_writeregister(self, SGM58601_REG_ADCON, adcon_value) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 设置转换通道函数
 */
static uint8_t sgm58601_set_channel(adc_device* self, uint8_t channel)
{
    if(channel > 0x0F)
    {
        adc_current_channel = 0x00; // 超出通道范围，设置为0
        channel = 0x00;
    }

    channel = (channel << 4) | 0x08; // 记录当前通道

    if(sgm58601_writeregister(self, SGM58601_REG_MUX, channel) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 设置转换速率
 */
static uint8_t sgm58601_set_rate(adc_device* self, uint8_t rate)
{
    if(sgm58601_writeregister(self, SGM58601_REG_DRATE, rate) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 写入寄存器函数
 */
static uint8_t sgm58601_writeregister(adc_device* self, uint8_t reg_addr, uint8_t data)
{
    // 准备数据
    rt_memset((void*)spi4_transfer_data, 0, 3);

    spi4_transfer_data[0] = 0x50 | reg_addr;  // WREG命令: 0101 rrrr + 0000 qqqq（几个寄存器）
    spi4_transfer_data[1] = 0x00;             // 写入1个寄存器，一般一次配置一个寄存器
    spi4_transfer_data[2] = data;            // 写入数据
    
    // 发送数据
    if(send_spi_command(self, spi4_transfer_data, 3, 0) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 读取寄存器函数
 */
static uint8_t sgm58601_readregister(adc_device* self, uint8_t reg_addr)
{
    uint8_t value;
    // 准备数据
    rt_memset((void*)spi4_transfer_data, 0, 3); // 3字节是为了后续读取数据
    rt_memset((void*)spi4_receive_data, 0, 1);

    spi4_transfer_data[0] = 0x10 | reg_addr;  // WREG命令: 0100 rrrr + 0000 qqqq（几个寄存器）
    spi4_transfer_data[1] = 0x00;             // 读取1个寄存器，一般一次读取个寄存器
    spi4_transfer_data[1] = 0xFF;             // dummy数据

    // 发送数据
    if(send_spi_command(self, spi4_transfer_data, 3, 1) == 0)
    {
        return 0;
    }

    // 等待dma接收完成，超过100ms则接收超时, 根据具体接收字节数量判断时间
    while(spi4_dma_receive_complete != 1)
    {
        rt_tick_t start_tick = rt_tick_get();
        rt_tick_t timeout_ms = 100;

        // 超过100ms则超时
        if(rt_tick_get_delta(start_tick) > timeout_ms)
        {
            return 0;
        }
    }

    spi4_dma_receive_complete = 0; // 接收完成标志位置0

    value = spi4_receive_data[0]; // 读取数据

    return value;
}

/**
 * @brief 自校准函数
 */
static uint8_t sgm58601_selfcalibrate(adc_device* self)
{
    // 准备数据
    rt_memset((void*)spi4_transfer_data, 0, 1);

    spi4_transfer_data[0] = SGM58601_CMD_SELFICAL; 

    // 发送自偏移+增益校准命令
    if(send_spi_command(self, spi4_transfer_data, 1, 0) == 0)
    {
        return 0;
    }

    // 等待校准完成 (nDRDY会变高，校准完成后变低)
    while(SET == gpio_input_bit_get(self->control_gpio.convert_complete_gpio_port, self->control_gpio.convert_complete_gpio_pin))
    {
        rt_tick_t start_tick = rt_tick_get();
        rt_tick_t timeout_ms = 100;

        // 超过100ms则超时
        if(rt_tick_get_delta(start_tick) > timeout_ms)
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 复位芯片
 */
static uint8_t sgm58601_reset(adc_device* self)
{
    // 方法1: 通过nRESET引脚 (低电平至少5μs)
    reset_set_func(self, 0);
    rt_thread_mdelay(100);
    reset_set_func(self, 1);
    rt_thread_mdelay(100);
    
    // 方法2: 通过RESET命令
    // 准备数据
    rt_memset((void*)spi4_transfer_data, 0, 1);

    spi4_transfer_data[0] = SGM58601_CMD_RESET;  

    // 发送数据
    if(send_spi_command(&sgm58601_1, spi4_transfer_data, 1, 0) == 0)
    {
        return 0;
    }
    
    return 1;
}
