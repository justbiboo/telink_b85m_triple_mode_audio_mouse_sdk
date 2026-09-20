/********************************************************************************************************
 * @file     aaa_sensor.c
 *
 * @brief    for TLSR chips
 *
 * @author	 telink
 * @date     Sep. 30, 2010
 *
 * @par      Copyright (c) 2010, Telink Semiconductor (Shanghai) Co., Ltd.
 *           All rights reserved.
 *
 *			 The information contained herein is confidential and proprietary property of Telink
 * 		     Semiconductor (Shanghai) Co., Ltd. and is available under the terms
 *			 of Commercial License Agreement between Telink Semiconductor (Shanghai)
 *			 Co., Ltd. and the licensee in separate contract or the terms described here-in.
 *           This heading MUST NOT be removed from this file.
 *
 * 			 Licensees are granted free, non-transferable use of the information in this
 *			 file under Mutual Non-Disclosure Agreement. NO WARRENTY of ANY KIND is provided.
 *
 *******************************************************************************************************/

#include "AAA_public_config.h"
#if SENSOR_FUN_ENABLE_AAA




#define sif_spi_clk_low    gpio_write(PIN_SIF_SCL, 0)  //0
#define sif_spi_clk_high   gpio_write(PIN_SIF_SCL, 1)  //1

#define sif_spi_sda_low    gpio_write(PIN_SIF_SDA, 0)  //0
#define sif_spi_sda_high   gpio_write(PIN_SIF_SDA, 1)  //1
#define get_spi_sda_vaule  gpio_read(PIN_SIF_SDA)// read sda

#define sif_spi_sda_output_enable    gpio_set_output_en(PIN_SIF_SDA, 1)//output sda en
#define sif_spi_sda_output_disable   gpio_set_output_en(PIN_SIF_SDA, 0)//output sda dis

#define sif_spi_sda_input_enable    gpio_set_input_en(PIN_SIF_SDA, 1) //sda  input en
#define sif_spi_sda_input_disable   gpio_set_input_en(PIN_SIF_SDA, 0) //sda input dis
#if SENSOR_CS_ENABLE //cs en

    #define sif_spi_cs_high	 gpio_write(PIN_SENSOR_CS,1) //cs 1
    #define sif_spi_cs_low   gpio_write(PIN_SENSOR_CS,0) //cs 0
    #define CS_DELAY        sleep_us(2) //2us
#else
    #define sif_spi_cs_high //nothing
    #define sif_spi_cs_low //
    #define CS_DELAY //
#endif


//CLOCK_DLY_6_CYC   370khz

#define		DLY_200NS    CLOCK_DLY_6_CYC//sleep_us(5)//asm("tnop");asm("tnop")
#define		DLY_100NS    CLOCK_DLY_10_CYC//sleep_us(5)//asm("tnop")




#define		SENSOR_RECOVER_FAIL				1             //1


////////////////////////////////////////////////////////////////////////////
// serial interface function
////////////////////////////////////////////////////////////////////////////


_attribute_data_retention_user u16	check_spi_counter;  //spi counter
_attribute_data_retention_user u8	check_resync_counter; //resync cnter
_attribute_data_retention_user u8	product_id1, product_id2, product_id3; //pid 1,2,3
//u8  reg_0d;
_attribute_data_retention_user  u8	sensor_type = 0xff; //ff
_attribute_data_retention_user u8   mouse_cust_fct3065xy = 0; //0
_attribute_data_retention_user u8 dbg_sensor_cpi;//cpi


/**
 * @brief       This function set sensor motion gpio as wakeup gpio
 * @param[in]   enable	- 
 * @return      
 * @note        
 */
void sensor_set_wakeup_level_suspend(u8 enable)
{

//#if 1
    if (gpio_read(PIN_SIF_MOTION)) //motion 1
    {
        cpu_set_gpio_wakeup(PIN_SIF_MOTION, 0, enable); //low wakeup suspend

    }
    else
    {
        cpu_set_gpio_wakeup(PIN_SIF_MOTION, 1, enable);//high wakeup suspend

    }
/*#else
    cpu_set_gpio_wakeup(PIN_SIF_MOTION, 0, enable); //low wakeup suspend
#endif*/


}


/**
 * @brief       This function set sensor motion gpio as deepsleep wakeup gpio
 * @param[in]   enable	- 
 * @return      
 * @note        
 */
void sensor_set_wakeup_level_deepsleep(u8 enable)
{

/*#if 0
    if (gpio_read(PIN_SIF_MOTION))
    {
        cpu_set_gpio_wakeup(PIN_SIF_MOTION, 0, enable); //low wakeup suspend

    }
    else
    {
        cpu_set_gpio_wakeup(PIN_SIF_MOTION, 1, enable);

    }
#else*/
    cpu_set_gpio_wakeup(PIN_SIF_MOTION, 0, enable); //low wakeup suspend
//#endif


}


/**
 * @brief       This function init sif
 * @return      
 * @note        
 */
void sif_init(void)
{
    //init in mouse.h
/*#if 0
    gpio_set_input_en(PIN_SIF_SCL, 0);
    gpio_set_output_en(PIN_SIF_SCL, 1);
    gpio_setup_up_down_resistor(PIN_SIF_SCL, PM_PIN_PULLUP_10K);
    gpio_write(PIN_SIF_SCL, 1);

    gpio_set_input_en(PIN_SIF_SDA, 1);
    gpio_set_output_en(PIN_SIF_SDA, 0);
    gpio_setup_up_down_resistor(PIN_SIF_SDA, PM_PIN_PULLUP_10K);


    gpio_set_input_en(PIN_SIF_MOTION, 1);
    gpio_set_output_en(PIN_SIF_MOTION, 0);
#if(BOARD_TYPE==QFN24_BOARD)
    gpio_setup_up_down_resistor(PIN_SIF_MOTION, PM_PIN_PULLUP_10K);

#elif(BOARD_TYPE==QFN32_BOARD)
    gpio_setup_up_down_resistor(PIN_SIF_MOTION, PM_PIN_PULLUP_1M);
#endif

    //sensor_set_wakeup_level_suspend();
#if SENSOR_CS_ENABLE
    gpio_set_input_en(PIN_SENSOR_CS, 0);
    gpio_set_output_en(PIN_SENSOR_CS, 1);
    gpio_setup_up_down_resistor(PIN_SENSOR_CS, PM_PIN_PULLUP_1M);
    gpio_write(PIN_SENSOR_CS, 1);
#endif
#else*/
/*#if 0
    if (deep_flag)
    {
        gpio_setup_up_down_resistor(PIN_SIF_SCL, PM_PIN_PULLUP_1M);
        gpio_setup_up_down_resistor(PIN_SIF_SDA, PM_PIN_PULLUP_10K);
        gpio_setup_up_down_resistor(PIN_SIF_MOTION, PM_PIN_PULLUP_1M);
    }
#endif*/
//#endif
}

/**
 * @brief       This function config motion sda gpio in chip power down
 * @return      
 * @note        
 */
void sensor_gpio_powerDownConfig()
{
    gpio_setup_up_down_resistor(PIN_SIF_SCL, PM_PIN_PULLUP_1M);//scl pull up
    if (gpio_read(PIN_SIF_SDA))//sda high
    {
        gpio_setup_up_down_resistor(PIN_SIF_SDA, PM_PIN_PULLUP_1M);//sda pull up
    }
    else
    {
        gpio_setup_up_down_resistor(PIN_SIF_SDA, PM_PIN_PULLDOWN_100K);//sda pull down
    }

    if (!gpio_read(PIN_SIF_MOTION))//read sif motion
    {
        gpio_setup_up_down_resistor(PIN_SIF_MOTION, PM_PIN_PULLDOWN_100K);//sif motion  pulldown
    }
}

/**
 * @brief       This function resyn sif
 * @return      
 * @note        
 */
void  sif_resyn(void)
{
    gpio_write(PIN_SIF_SCL, 1);//scl 1
    WaitUs(3);//3 us
    gpio_write(PIN_SIF_SCL, 0);//scl 0
    WaitUs(2);//2us
    gpio_write(PIN_SIF_SCL, 1);//scl 1
    WaitUs(2000);//2ms
}

/**
 * @brief       This function send byte data to sensor
 * @param[in]   data	- the data need send
 * @return      
 * @note        
 */
_attribute_ram_code_ void sif_SendByte(u8 data)
{
    u8 i = 0;
    u8 buf = data;

    for (i = 0; i < 8; i++)
    {
        sif_spi_clk_low;//clk low
        gpio_write(PIN_SIF_SDA, buf & 0x80);//write buf
        DLY_100NS;//100ns


        sif_spi_clk_high;//clk high
        buf <<= 1;
        DLY_100NS;//100ns
    }
}

/**
 * @brief       This function read byte data from sensor
 * @return      
 * @note        
 */
_attribute_ram_code_ u8 sif_ReadByte()
{
    u8 i = 0;//i 0
    u8 dat = 0;//dat 0

    for (i = 0; i < 8; i++)
    {
        sif_spi_clk_low;//clk low
        dat <<= 1;
        DLY_100NS;//100ns

        sif_spi_clk_high;//clk high
        DLY_100NS;//100ns
        if (get_spi_sda_vaule)//sda high
        {
            dat |= 1;
        }
    }
    return dat;
}


/**
 * @brief       This function read pan3204 register
 * @param[in]   cAddr	-  the addr need read
 * @return      
 * @note        
 */
_attribute_ram_code_ u8 I2C_PAN3204LL_ReadRegister(u8 cAddr)
{

    u8 dat = 0;


    //u8 r = irq_disable();
    sif_spi_sda_output_enable;//output en
    sif_spi_sda_input_disable;//dis input
    sif_spi_cs_low;//cs low

    CS_DELAY;//cs daley

    sif_SendByte(cAddr & 0x7f);//send addr

    sif_spi_sda_output_disable;// delay 5us
    sif_spi_sda_input_enable;  // delay 15us


    //WaitUs(5);       //this delay is necessary!


    dat = sif_ReadByte();//read data


    CS_DELAY;
    sif_spi_cs_high;//cs high

    //irq_restore(r);

    return dat;
}

/**
 * @brief       This function write data to sensor register
 * @param[in]   cAddr	- the addr need write
 * @param[in]   cData	- the data need write
 * @return      
 * @note        
 */
_attribute_ram_code_  void I2C_PAN3204LL_WriteRegister(u8 cAddr, u8 cData)
{
   // u8 r = irq_disable();
    gpio_set_output_en(PIN_SIF_SDA, 1);//output en
    gpio_set_input_en(PIN_SIF_SDA, 0);//disable input
    sif_spi_cs_low;
    CS_DELAY;
    sif_SendByte((cAddr | 0x80));//send addr
    //WaitUs(1);
    sif_SendByte(cData);//send data

    CS_DELAY;
    sif_spi_cs_high;

    gpio_set_output_en(PIN_SIF_SDA, 0);//output disable
    gpio_set_input_en(PIN_SIF_SDA, 1);//input en


   // irq_restore(r);

}


//----------------------------------------------------------------------
// FUNCTION NAME: Sensor3204LL_Optimization_Setting
//
// DESCRIPTION:
//      Do the optimization of the sensor PAN3204LL to consume less current.
//      Also, if need, set a better tracking performance on Critical surfaces.
//
//----------------------------------------------------------------------

/**
 * @brief       This function download config table
 * @param[in]   len	-  the write len
 * @param[in]   ptbl	- the congfig table
 * @return      
 * @note        
 */
void DownloadConfigTable(const unsigned char *ptbl, unsigned int len)
{
    unsigned int i, addr;

    for (i = 0; i < len; i += 2)
    {
        addr = ptbl[0];
        I2C_PAN3204LL_WriteRegister(addr, ptbl[1]);//write ptbl to reg
        ptbl += 2;//ptbl +2
    }
}

const unsigned char Config_3204[] =
{
    0x09, 0x5A, //
    0x0A, 0xF2, //
    0x0B, 0x00, //
    0x0D, 0x14, //
    0x12, 0x1D, //
    0x27, 0x4B, //
    0x42, 0x85, //
    0x28, 0xEF, //
    0x2A, 0xAA, //
    0x2B, 0xA0, //
    0x2C, 0x8C, //
    0x2D, 0x82, //
    0x09, 0x00, //
};


const unsigned char Config_3204LL[] =
{
    0x09, 0x5A,
    0x0A, 0x40,
    0x0B, 0x10,
    0x0C, 0xE0,
    0x24, 0x1F,
    0x64, 0x4E,
    0x50, 0x07,
    0x4D, 0x81,
    0x0D, 0x0A,
    0x09, 0x00,
};

const unsigned char Config_3204UL[] =
{
    0x09, 0x5A, //
    0x0A, 0xf0, //
    0x0D, 0x0F, //
    0x1D, 0xE3, //
    0x28, 0xB4, //
    0x29, 0x46, //
    0x2A, 0x96, //
    0x2B, 0x8C, //
    0x2C, 0x6E, //
    0x2D, 0x64, //
    0x38, 0x5F, //
    0x39, 0x0F, //
    0x3A, 0x32, //
    0x3B, 0x47, //
    0x42, 0x10, //
    0x54, 0x2E, // 
    0x55, 0xF2, //
    0x61, 0xF4,
    0x63, 0x70,
    0x75, 0x52,
    0x76, 0x41,
    0x77, 0xED,
    0x78, 0x23,
    0x79, 0x46,
    0x7A, 0xE5,
    0x7C, 0x48,
    0x7D, 0xD2,
    0x7E, 0x77,
    0x1B, 0x35,
    0x7F, 0x01,
    0x0B, 0x00,
    0x7F, 0x00,

#if SENSOR_QB_READ
    0x15, 0x84,
    0x19, 0x1c,
    0x12, 0x5d,  //ini value:0x12 = 0x1d
#endif

    0x09, 0x00,
};

const unsigned char Config_3205[] =
{
/*#if 0
    // E ÔªËØmouse settting
    0x09, 0x5a,
    0x0b, 0x12,
    0x0c, 0x20,
    0x28, 0xb4,
    0x29, 0x46,
    0x2a, 0x96,
    0x2b, 0x8c,
    0x2c, 0x6e,

    0x2d, 0x64,
    0x38, 0x5f,
    0x39, 0x0f,
    0x3a, 0x32,
    0x3b, 0x47,
    0x42, 0x10,
    0x4b, 0x13,
    0x54, 0x2e,
    0x55, 0xf2,
    0x61, 0xf4,
    0x63, 0x70,
    0x75, 0x52,
    0x76, 0x41,
    0x77, 0xed,
    0x78, 0x23,
    0x79, 0x46,
    0x7a, 0xe5,
    0x7c, 0x48,
    0x7d, 0x80,
    0x7e, 0x77,
    0x7f, 0x01,
    0x0b, 0x00,
    0x7f, 0x00,
    0x09, 0x00,
#elif 0
    //Below is the setting from YiHaiXing which cannot improve the current significantly.
    //But this setting can improve the code size about 52 bytes.
    0x09, 0x5A,
    0x0A, 0x70,
    0x0B, 0x10,
    0x0C, 0x70,
    0x0D, 0x0A,
    0x0E, 0xE5,
    0x09, 0x00,
#else*/
    0x09, 0x5A,
    0x0D, 0x0A,
    0x1B, 0x35,
    0x1D, 0xDB,

    0x28, 0xB4,
    0x29, 0x46,
    0x2A, 0x96,
    0x2B, 0x8C,
    0x2C, 0x6E,
    0x2D, 0x64,
    0x38, 0x5F,
    0x39, 0x0F,
    0x3A, 0x32,
    0x3B, 0x47,
    0x42, 0x10,

    0x43, 0x09,
    0x54, 0x2E,
    0x55, 0xF2,
    0x61, 0xF4,
    0x63, 0x70,
    0x75, 0x52,
    0x76, 0x41,
    0x77, 0xED,
    0x78, 0x23,
    0x79, 0x46,
    0x7A, 0xE5,
    0x7C, 0x48,

    0x7D, 0x80,
    0x7E, 0x77,

    0x7F, 0x01,
    0x0B, 0x00,

    0x7F, 0x00,
    0x09, 0x00,
//#endif
};

const unsigned char Config_AN3205[] =
{
    0x09, 0xA5,
    0x40, 0x1B,
    0x41, 0x04,
    0x46, 0x43,
    0x48, 0x77,
    0x4D, 0x08,
    0x4E, 0x80,
    0x09, 0x00,
};

//3207 default setting is the same
/*#if 0
const unsigned char Config_3207[] =
{
    0x09,  0x5A,
    0x0D,  0x0A,
    0x0E,  0xC5,
    0x20,  0x2B,
    0x24,  0x2D,
    0x28,  0xB4,
    0x29,  0x46,
    0x2A,  0x96,
    0x2B,  0x8C,
    0x2C,  0x6E,
    0x2D,  0x64,
    0x38,  0x58,
    0x42,  0x22,
    0x43,  0x09,
    0x4C,  0xC2,
    0x4D,  0x81,
    0x4F,  0xD0,
    0x56,  0xA1,
    0x61,  0xF4,
    0x62,  0x10,
    0x63,  0x07,
    0x67,  0x00,
    0x75,  0x52,
    0x76,  0x41,
    0x77,  0x6D,
    0x78,  0x23,
    0x79,  0x46,
    0x09,  0x00,
};
#endif*/

const unsigned char Config_8630[] =
{
    //	0x7f , 0xa5,
    //	0x1c , 0x14,
    //	0x1f , 0x3a,
    //	0x7f , 0x00,
};

const unsigned char Config_3207[] =
{

};

const unsigned char Config_VT108[] =  		//KA8
{
    0x09,  0x5a,
    0x0d,  0x12,
    0x0e,  0xc5,
    0x09,  0x00,
};

const unsigned char Config_M8589[] =
{

};

const unsigned char Config_KA9[] =
{
    0x09,  0x5a,
    0x0d,  0x14,
    0x1b,  0x35,
    0x1d,  0xdb,
    0x28,  0xb4,
    0x29,  0x46,
    0x2a,  0x96,
    0x2b,  0x8c,
    0x2c,  0x6e,
    0x2d,  0x64,
    0x38,  0x5f,
    0x39,  0x0f,
    0x3a,  0x32,
    0x3b,  0x47,
    0x42,  0x10,
    0x54,  0x2e,
    0x55,  0xf2,
    0x61,  0xf4,
    0x63,  0x70,
    0x75,  0x52,
    0x76,  0x41,
    0x77,  0xed,
    0x78,  0x23,
    0x79,  0x46,
    0x7a,  0xe5,
    0x7c,  0x48,
    0x7d,  0x80,
    0x7e,  0x77,
    0x1b,  0x35,
    0x7f,  0x01,
    0x0b,  0x00,
    0x7f,  0x00,
    0x09,  0x00,
};

const unsigned char ShutDown_Config_KA9[] =
{

    0x09,  0x5a,
    0x4b,  0x13,
    0x09,  0x00,
};

const unsigned char WakeUp_Config_KA9[] =
{
    0x09,  0x5a,
    0x4b,  0x1b,
    0x09,  0x00,
};

/*#if 0
const unsigned char Config_3212[] =
{
    0x09,  0x5a,
    0x26,  0x04,
    0x09,  0x00,
};
#else*/
const unsigned char Config_3212[] =
{
    0x09,  0x5a,
    0x06,  0xa0,
    0x26,  0x34,
    0x09,  0x00,
};

//#endif

const unsigned char Config_YS8008[] =
{
    0x06,  0x03,
    0x09,  0x5a,
    0x0b,  0x90,
    0x09,  0x69,
    0x0d,  0x48,
    0x0e,  0x9f,
    0x0f,  0xba,
    0x16,  0xbd,
    0x17,  0x08,
    0x22,  0x11,
    0x09,  0x00,
};
typedef struct
{
    const unsigned char *ptbl;
    unsigned int len;
} SENSOR_OPT;

const SENSOR_OPT Pan_opt[] =
{
    {Config_3204, sizeof(Config_3204)}, //3204
    {Config_3204LL, sizeof(Config_3204LL)},//3204 ll
    {Config_3204UL, sizeof(Config_3204UL)}, //3204ul
    {Config_3205, sizeof(Config_3205)},//3205
    {Config_AN3205, sizeof(Config_AN3205)},//an3205
    {Config_8630, sizeof(Config_8630)},//8630
    {Config_3207, sizeof(Config_3207)},//3207
    {Config_VT108, sizeof(Config_VT108)},//vt108
    {Config_M8589, sizeof(Config_M8589)},//m8589
    {Config_KA9, sizeof(Config_KA9)},//ka9
};
#define OPTIM_SENSOR_NUM    ( sizeof(Pan_opt) / sizeof(SENSOR_OPT) )

/*#if 0
void Sensor3204LL_Optimization_Setting(void)
{
    DownloadConfigTable(Pan_opt[sensor_type].ptbl, Pan_opt[sensor_type].len);

}
#else*/

#define Sensor3204LL_Optimization_Setting()    DownloadConfigTable(Pan_opt[sensor_type].ptbl, Pan_opt[sensor_type].len) //3204
#define Sensor3212_Optimization_Setting()	   DownloadConfigTable(Config_3212, sizeof(Config_3212)) //3212
#define SensorKA9_Shutdown_Setting()		   DownloadConfigTable(ShutDown_Config_KA9, sizeof(ShutDown_Config_KA9)) //ka9
#define SensorKA9_WakeUp_Setting()             DownloadConfigTable(WakeUp_Config_KA9, sizeof(WakeUp_Config_KA9))//ka9
#define SensorYS8008_Optimization_Setting()    DownloadConfigTable(Config_YS8008, sizeof(Config_YS8008)) //ys8008
//#endif


/*#if 0
void OPTSensor_re_init()
{
    sif_init();
    sif_resyn();
}
#endif*/



/**
 * @brief       This function resync opt sensor
 * @param[in]   retry	-  retry times
 * @return      
 * @note        
 */
_attribute_ram_code_ int OPTSensor_resync(u32 retry)
{
    check_spi_counter = 0;
    while (((product_id1 = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_PRODUCT_ID1)) != PAN3204LL_PRODUCT_ID1)\
            && (product_id1 != PAN3204LL_PRODUCT_ID1_8204)\
            && (product_id1 != PAN3204LL_PRODUCT_ID1_3065)\
            &&(product_id1 != 0x58)\
          )
    {
#if (MODULE_WATCHDOG_ENABLE)//wd en

        wd_clear();//clear wd
#endif
        gpio_write(PIN_SIF_SCL, 1);//scl 1
        WaitUs(3);//3us
        gpio_write(PIN_SIF_SCL, 0);//scl 0

        if (mouse_cust_fct3065xy)
        {
            WaitUs(10000);				//FCT3065-XY SCL Low 10ms, then high 512ms
            gpio_write(PIN_SIF_SCL, 1);//scl 1
            WaitUs(40000);//4ms
        }
        else
        {
            WaitUs(3);//3 us
            gpio_write(PIN_SIF_SCL, 1);//scl 1
        }


        if (++check_spi_counter >= retry)
        {
            check_resync_counter++;
            return SENSOR_RECOVER_FAIL;
        }
    }
    return 0;
}


/**
 * @brief       This function identify product id
 * @param[in]   product_id2	- 
 * @return      
 * @note        
 */
static inline int sensor_type_identify(unsigned char product_id2)
{
    int sensor_type = 0;
#if (SENSOR_PAW3212_ENABLE)//3212 en
    if ((product_id2 & 0xff) == 0x02)
    {
        sensor_type = SENSOR_PAW3212;//paw3212
        return sensor_type;
    }
#endif
	if((product_id1==0x38)&&(product_id2==0x51))
		{
			sensor_type = SENSOR_SW3851;
			return sensor_type;
		}
	if((product_id1==0x58)&&(product_id2==0x59))
		{
			sensor_type = SENSOR_KA8G2;
			return sensor_type;
		}
    if ((product_id1 == PAN3805EK_PRODUCT_ID1) && (product_id2 == PAN3805EK_PRODUCT_ID2))//3805
    {
        sensor_type = SENSOR_PAN3805EK;//3805

        return sensor_type;
    }
    else if ((product_id2 == FCT3065_PRODUCT_NEW_ID2)  || (product_id2 == FCT3065_PRODUCT_OLD_ID2))//fct3065
    {
        if (product_id1 == FCT3065_PRODUCT_ID1)//3605 pid 1
        {
            sensor_type = SENSOR_3205;//3205
            mouse_cust_fct3065xy = 1;
            return sensor_type;
        }
    }
    else if ((product_id1 == PAN3204LL_PRODUCT_ID1_8204) && (product_id2 == S8321_PRODUCT_ID2))//8204
    {
        sensor_type = SENSOR_VT108;//vt108
        return sensor_type;
    }

    switch (product_id2 & 0xF0)//
    {
        case PAN3204_PRODUCT_ID2://0x50
            if (product_id2 == 0x54)//0x54
            {
                sensor_type = SENSOR_VT108;//vt108
                break;
            }
            else if (product_id2 == 0x52)//0x52
            {
                sensor_type = SENSOR_OM16;//om16
                break;
            }
            else if (product_id2 == 0x50)//0x50
            {
                u8 reg_17 = I2C_PAN3204LL_ReadRegister(0x17);

                if (reg_17 == 0x5c)
                    sensor_type = SENSOR_YS8006;//ys8006
                break;
            }
            else
            {
                sensor_type = SENSOR_3204;//3204
                unsigned  int reg_old, reg_new;
#if SUPPORT_M8589 //sup m8589
                reg_old = I2C_PAN3204LL_ReadRegister(0x9);
                reg_new = 0;
                do
                {
                    I2C_PAN3204LL_WriteRegister(0x09, 0xa5);        //first reg 06 write 0 to BIT2
                    if (++reg_new > 100000)
                        break;
#if (MODULE_WATCHDOG_ENABLE) //wd en
					wd_clear(); //clear watch dog
#endif
                }
                while (0xa5 != I2C_PAN3204LL_ReadRegister(0x9));
                reg_new = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_PRODUCT_ID2);
                I2C_PAN3204LL_WriteRegister(0x09, reg_old);         //first reg 06 write 0 to BIT2
                if (reg_new == 0x49)
                {
                    sensor_type = SENSOR_M8589;//m8589
                    break;
                }
#endif
#if SENSOR_8640_ENABLE //sensor 8640 en
                reg_old = I2C_PAN3204LL_ReadRegister(0x06);
                I2C_PAN3204LL_WriteRegister(0x06, 0);       //first reg 06 write 0 to BIT2
                reg_new = I2C_PAN3204LL_ReadRegister(0x06);
                I2C_PAN3204LL_WriteRegister(0x06, reg_old);
                if ((BIT(2) & reg_new) == BIT(2))           //Sigma 8630 reg 06 is always 1, 3205/3205 reg 06 is 0
                {
                    sensor_type = SENSOR_SIGMA_8630;//8630
                    break;
                }
#endif
                break;
            }
        case PAN3204LL_PRODUCT_ID2://0xC0
            sensor_type = SENSOR_3204LL;//3204ll
            break;
        case PAN3204UL_PRODUCT_ID2://0xD0
            if (I2C_PAN3204LL_ReadRegister(0x1E) & 0x01)
            {
                sensor_type = SENSOR_3204UL;//3204ul
            }
            else if (I2C_PAN3204LL_ReadRegister(0x1B) == 0x1D)
            {
                sensor_type = SENSOR_YS8008;//ys8008
            }

            else
            {
                if (I2C_PAN3204LL_ReadRegister(0x10) == 0x13 && I2C_PAN3204LL_ReadRegister(0x11) == 0x20) 			//add KA9
                {
                    sensor_type = SENSOR_3205;//3205
                }
                else
                {
                    sensor_type = SENSOR_KA9;//ka9
                    u8 reg_20 = I2C_PAN3204LL_ReadRegister(0x20);
                    u8 reg_21 = I2C_PAN3204LL_ReadRegister(0x21);
                    if ((reg_20 == 0x58) && ((reg_21 & 0xf0) == 0x20))
                    {
                        sensor_type = SENSOR_AN3205;//an3205
                    }
                }
            }
            break;
        case PAW3207_PRODUCT_ID2://0xE0
            sensor_type = SENSOR_PAW3207;//paw3207
            break;
        default:
            break;
    }
    return sensor_type;
}
/*#if 0
const u8 pan3805_op_reg[] =
{
    0x7F, 0x00,		// switch to bank0, not allowed to perform hal_pixart_writeRegister
    0x09, 0x5A,					// disable write protect
    0x51, 0x0B	,				// set LED current source to 11
    0x0A, 0x17,
    0x2E, 0x40,
    0x32, 0x40,
    0x33, 0x02,
    0x34, 0x00,
    0x36, 0xE0,
    0x3E, 0x14,
    0x52, 0x04,
    0x57, 0x03,
    0x59, 0x03,

    0x7F, 0x01,			// switch to bank1, not allowed to perform hal_pixart_writeRegister
    0x08, 0x1C,
    0x0A, 0x02,
    0x19, 0x40,
    0x1B, 0x10,
    0x1D, 0x30,
    0x1F, 0x24,
    0x20, 0x00,
    0x23, 0x60,
    0x25, 0x64,
    0x27, 0x64,
    0x2B, 0x78,
    0x2F, 0x78,
    0x39, 0x78,
    0x3B, 0x78,
    0x3D, 0x78,
    0x3F, 0x78,
    0x44, 0x7E,
    0x45, 0xF4,
    0x46, 0x01,
    0x47, 0x2C,
    0x49, 0x90,
    0x4A, 0x05,
    0x4B, 0xDC,
    0x4C, 0x07,
    0x4D, 0x08,

    0x7F, 0x02,			// switch to bank2, not allowed to perform hal_pixart_writeRegister
    0x07, 0x1B,
    0x08, 0x1F,
    0x09, 0x23,

    0x7F, 0x03,			// switch to bank3, not allowed to perform hal_pixart_writeRegister
    0x07, 0x07,
    0x08, 0x06,
    0x36, 0x21,
    0x37, 0x34,

    0x7F, 0x04,			// switch to bank4, not allowed to perform hal_pixart_writeRegister
    0x05, 0x01,
    0x2C, 0x06,
    0x2E, 0x0C,
    0x30, 0x0C,
    0x32, 0x06,
    0x34, 0x03,
    0x38, 0x17,
    0x39, 0x71,
    0x3A, 0x18,
    0x3B, 0x4D,
    0x3C, 0x18,
    0x3D, 0x4D,
    0x3E, 0x14,
    0x3F, 0xD1,
    0x40, 0x14,
    0x41, 0xDD,
    0x42, 0x0A,
    0x43, 0x6C,
    0x44, 0x08,
    0x45, 0xAD,
    0x46, 0x06,
    0x47, 0xF2,
    0x48, 0x06,
    0x49, 0xEC,
    0x4A, 0x06,
    0x4B, 0xEC,


    0x7F, 0x05,			// switch to bank5, not allowed to perform hal_pixart_writeRegister
    0x03, 0x00,
    0x09, 0x01,
    0x0B, 0xFF,
    0x0D, 0xFF,
    0x0F, 0xFF,
    0x11, 0xFF,
    0x12, 0xD2,
    0x13, 0xD2,
    0x19, 0xFF,
    0x1B, 0xFF,
    0x1D, 0xFF,
    0x1F, 0xFF,
    0x20, 0xD2,
    0x21, 0xD2,
    0x30, 0x05,
    0x41, 0x02,
    0x53, 0xFF,
    0x5F, 0x02,


    0x7F, 0x06,			// switch to bank6, not allowed to perform hal_pixart_writeRegister
    0x2A, 0x05,			// Register Address 0x2A of Bank6 is WRITE ONLY, not allowed to perform hal_pixart_writeRegister
    0x35, 0x19,

    0x7F, 0x07,			// switch to bank7, not allowed to perform hal_pixart_writeRegister
    0x06, 0x04,
    0x02, 0x6C,
    0x03, 0x07,
    0x47, 0xAF,
    0x4B, 0xB7,
    0x35, 0x01,
    0x36, 0x00,

    0x7F, 0x00,			// switch to bank0, not allowed to perform hal_pixart_writeRegister
    0x09, 0x00,

};
void pan3805_Optimization_Setting()
{
    for (u16 i = 0; i < sizeof(pan3805_op_reg); i += 2)
    {
        I2C_PAN3204LL_WriteRegister(pan3805_op_reg[i], pan3805_op_reg[i + 1]);
    }
}
#else*/

/**
 * @brief       This function opt pan3805
 * @return      
 * @note        
 */
void pan3805_Optimization_Setting()
{

    I2C_PAN3204LL_WriteRegister(0x7F, 0x00);		// switch to bank0, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x09, 0x5A);					// disable write protect
    I2C_PAN3204LL_WriteRegister(0x51, 0x0B);					// set LED current source to 11
    I2C_PAN3204LL_WriteRegister(0x0A, 0x17);
    I2C_PAN3204LL_WriteRegister(0x2E, 0x40);
    I2C_PAN3204LL_WriteRegister(0x32, 0x40);
    I2C_PAN3204LL_WriteRegister(0x33, 0x02);
    I2C_PAN3204LL_WriteRegister(0x34, 0x00);
    I2C_PAN3204LL_WriteRegister(0x36, 0xE0);
    I2C_PAN3204LL_WriteRegister(0x3E, 0x14);
    I2C_PAN3204LL_WriteRegister(0x52, 0x04);
    I2C_PAN3204LL_WriteRegister(0x57, 0x03);
    I2C_PAN3204LL_WriteRegister(0x59, 0x03);

    I2C_PAN3204LL_WriteRegister(0x7F, 0x01);		// switch to bank1, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x08, 0x1C);
    I2C_PAN3204LL_WriteRegister(0x0A, 0x02);
    I2C_PAN3204LL_WriteRegister(0x19, 0x40);
    I2C_PAN3204LL_WriteRegister(0x1B, 0x10);
    I2C_PAN3204LL_WriteRegister(0x1D, 0x30);
    I2C_PAN3204LL_WriteRegister(0x1F, 0x24);
    I2C_PAN3204LL_WriteRegister(0x20, 0x00);
    I2C_PAN3204LL_WriteRegister(0x23, 0x60);
    I2C_PAN3204LL_WriteRegister(0x25, 0x64);
    I2C_PAN3204LL_WriteRegister(0x27, 0x64);
    I2C_PAN3204LL_WriteRegister(0x2B, 0x78);
    I2C_PAN3204LL_WriteRegister(0x2F, 0x78);
    I2C_PAN3204LL_WriteRegister(0x39, 0x78);
    I2C_PAN3204LL_WriteRegister(0x3B, 0x78);
    I2C_PAN3204LL_WriteRegister(0x3D, 0x78);
    I2C_PAN3204LL_WriteRegister(0x3F, 0x78);
    I2C_PAN3204LL_WriteRegister(0x44, 0x7E);
    I2C_PAN3204LL_WriteRegister(0x45, 0xF4);
    I2C_PAN3204LL_WriteRegister(0x46, 0x01);
    I2C_PAN3204LL_WriteRegister(0x47, 0x2C);
    I2C_PAN3204LL_WriteRegister(0x49, 0x90);
    I2C_PAN3204LL_WriteRegister(0x4A, 0x05);
    I2C_PAN3204LL_WriteRegister(0x4B, 0xDC);
    I2C_PAN3204LL_WriteRegister(0x4C, 0x07);
    I2C_PAN3204LL_WriteRegister(0x4D, 0x08);

    I2C_PAN3204LL_WriteRegister(0x7F, 0x02);		// switch to bank2, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x07, 0x1B);
    I2C_PAN3204LL_WriteRegister(0x08, 0x1F);
    I2C_PAN3204LL_WriteRegister(0x09, 0x23);

    I2C_PAN3204LL_WriteRegister(0x7F, 0x03);		// switch to bank3, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x07, 0x07);
    I2C_PAN3204LL_WriteRegister(0x08, 0x06);
    I2C_PAN3204LL_WriteRegister(0x36, 0x21);
    I2C_PAN3204LL_WriteRegister(0x37, 0x34);

    I2C_PAN3204LL_WriteRegister(0x7F, 0x04);		// switch to bank4, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x05, 0x01);
    I2C_PAN3204LL_WriteRegister(0x2C, 0x06);
    I2C_PAN3204LL_WriteRegister(0x2E, 0x0C);
    I2C_PAN3204LL_WriteRegister(0x30, 0x0C);
    I2C_PAN3204LL_WriteRegister(0x32, 0x06);
    I2C_PAN3204LL_WriteRegister(0x34, 0x03);
    I2C_PAN3204LL_WriteRegister(0x38, 0x17);
    I2C_PAN3204LL_WriteRegister(0x39, 0x71);
    I2C_PAN3204LL_WriteRegister(0x3A, 0x18);
    I2C_PAN3204LL_WriteRegister(0x3B, 0x4D);
    I2C_PAN3204LL_WriteRegister(0x3C, 0x18);
    I2C_PAN3204LL_WriteRegister(0x3D, 0x4D);
    I2C_PAN3204LL_WriteRegister(0x3E, 0x14);
    I2C_PAN3204LL_WriteRegister(0x3F, 0xD1);
    I2C_PAN3204LL_WriteRegister(0x40, 0x14);
    I2C_PAN3204LL_WriteRegister(0x41, 0xDD);
    I2C_PAN3204LL_WriteRegister(0x42, 0x0A);
    I2C_PAN3204LL_WriteRegister(0x43, 0x6C);
    I2C_PAN3204LL_WriteRegister(0x44, 0x08);
    I2C_PAN3204LL_WriteRegister(0x45, 0xAD);
    I2C_PAN3204LL_WriteRegister(0x46, 0x06);
    I2C_PAN3204LL_WriteRegister(0x47, 0xF2);
    I2C_PAN3204LL_WriteRegister(0x48, 0x06);
    I2C_PAN3204LL_WriteRegister(0x49, 0xEC);
    I2C_PAN3204LL_WriteRegister(0x4A, 0x06);
    I2C_PAN3204LL_WriteRegister(0x4B, 0xEC);


    I2C_PAN3204LL_WriteRegister(0x7F, 0x05);		// switch to bank5, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x03, 0x00);
    I2C_PAN3204LL_WriteRegister(0x09, 0x01);
    I2C_PAN3204LL_WriteRegister(0x0B, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x0D, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x0F, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x11, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x12, 0xD2);
    I2C_PAN3204LL_WriteRegister(0x13, 0xD2);
    I2C_PAN3204LL_WriteRegister(0x19, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x1B, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x1D, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x1F, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x20, 0xD2);
    I2C_PAN3204LL_WriteRegister(0x21, 0xD2);
    I2C_PAN3204LL_WriteRegister(0x30, 0x05);
    I2C_PAN3204LL_WriteRegister(0x41, 0x02);
    I2C_PAN3204LL_WriteRegister(0x53, 0xFF);
    I2C_PAN3204LL_WriteRegister(0x5F, 0x02);


    I2C_PAN3204LL_WriteRegister(0x7F, 0x06);		// switch to bank6, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x2A, 0x05);		// Register Address 0x2A of Bank6 is WRITE ONLY, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x35, 0x19);

    I2C_PAN3204LL_WriteRegister(0x7F, 0x07);		// switch to bank7, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x06, 0x04);
    I2C_PAN3204LL_WriteRegister(0x02, 0x6C);
    I2C_PAN3204LL_WriteRegister(0x03, 0x07);
    I2C_PAN3204LL_WriteRegister(0x47, 0xAF);
    I2C_PAN3204LL_WriteRegister(0x4B, 0xB7);
    I2C_PAN3204LL_WriteRegister(0x35, 0x01);
    I2C_PAN3204LL_WriteRegister(0x36, 0x00);

    I2C_PAN3204LL_WriteRegister(0x7F, 0x00);		// switch to bank0, not allowed to perform hal_pixart_writeRegister
    I2C_PAN3204LL_WriteRegister(0x09, 0x00);

}

//#endif
void KA8G2_Optimization_Setting()
{
	I2C_PAN3204LL_WriteRegister(0x09,0xA5);
	I2C_PAN3204LL_WriteRegister(0x46,0x34); // ???? spi ??????
	I2C_PAN3204LL_WriteRegister(0x19,0x00);
	I2C_PAN3204LL_WriteRegister(0x60,0x07);
	I2C_PAN3204LL_WriteRegister(0x69,0x04);
	I2C_PAN3204LL_WriteRegister(0x7D,0x20);
	I2C_PAN3204LL_WriteRegister(0x7E,0x00);
	I2C_PAN3204LL_WriteRegister(0x09,0x00);
	I2C_PAN3204LL_WriteRegister(0x0D,0x1B);
	I2C_PAN3204LL_WriteRegister(0x0E,0x1B);

	I2C_PAN3204LL_WriteRegister(0x6E,0x1c);  //disable 
	//I2C_PAN3204LL_WriteRegister(0x6E,0x14);// eanble inter Internal resistance

	I2C_PAN3204LL_WriteRegister(0x09,0x00);
}
//called when power on, or mouse waked up from deep-sleep

/**
 * @brief       This function init opt sensor
 * @param[in]   poweron	- 
 * @return      
 * @note        
 */
unsigned int OPTSensor_Init(unsigned int poweron)
{

    // Do the full chip reset.
    // u8 ret=0;
    sensor_type = analog_read(DEEP_ANA_REG6);
#if PM_DEEPSLEEP_RETENTION_ENABLE
    if (deepRetWakeUp)
    {
        return 1;
    }
#endif
    sif_init();
    if (deep_flag == DEEP_SLEEP_ANA_AAA)
    {
#if SENSOR_SHUT_DOWN_ENABLE

        Sensor3204_Wakeup(0);
#endif
        //return 1;

    }
	
	#if(1)
    if (OPTSensor_resync(1024))
    {
        printf("resync fail\n");
        return SENSOR_MODE_A3000;
    }
	#endif
    if (poweron)
    {
        //reset sensor would clear all optimization

        I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_CONFIGURATION, CONFIGURATION_RESET);
        WaitUs(1000);
    }

    product_id1 = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_PRODUCT_ID1);    //power-on must re-read product_id1, or it would make mistake
    product_id2 = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_PRODUCT_ID2);
    product_id3 = I2C_PAN3204LL_ReadRegister(0x60);



    sensor_type = sensor_type_identify(product_id2);
	printf("the product_id1 is %d, the product_id2 is %d, the sensor_type is %d\n",product_id1,product_id2,sensor_type);
    analog_write(DEEP_ANA_REG6, sensor_type);
/*#if 0 // test sensor
    if (sensor_type != SENSOR_PAW3212)
    {
        while (1)
        {
            led_bat_lvd();
            device_led_process();
        }

    }
#endif*/
    if (sensor_type == SENSOR_PAW3212)//3212
    {
        Sensor3212_Optimization_Setting();		//PAW3212 do special optimization
#if (MOUSE_DATA_LEN_AAA==6)
        u8 ww = 0;
        for (u8 i = 0; i < 8; i++)
        {
            I2C_PAN3204LL_WriteRegister(0x09, 0x5a);
            I2C_PAN3204LL_WriteRegister(0x19, 0x04);
            I2C_PAN3204LL_WriteRegister(0x09, 0x00);
            ww = I2C_PAN3204LL_ReadRegister(0x19);

            if (ww == 0x04)
            {
                break;
            }
        }
#endif
    }
    else if (sensor_type == SENSOR_PAN3805EK)//3805
    {
        pan3805_Optimization_Setting();
#if (MOUSE_DATA_LEN_AAA==4)//ms data 4
        u8 ww = 0;
        for (u8 i = 0; i < 8; i++)
        {
            I2C_PAN3204LL_WriteRegister(0x09, 0x5a);
            I2C_PAN3204LL_WriteRegister(0x19, 0x10);
            I2C_PAN3204LL_WriteRegister(0x09, 0x00);
            ww = I2C_PAN3204LL_ReadRegister(0x19);

            if (ww == 0x10)
            {
                break;
            }
        }


#endif
    }
	else if(sensor_type==SENSOR_KA8G2)
	{
			KA8G2_Optimization_Setting();
	}
    else
    {
        if ((sensor_type < OPTIM_SENSOR_NUM) || ((sensor_type == SENSOR_SIGMA_8630) &&
                (I2C_PAN3204LL_ReadRegister(0x1f) != 0x3a)))
        {
            if (product_id1 == PAN3204LL_PRODUCT_ID1)       //FCT donnot do optimizataion
            {
#if (SENSOR_PAW3212_ENABLE)
                if (sensor_type == SENSOR_PAW3212)
                {



                }
                else
#endif
                {
                    if (!mouse_cust_fct3065xy)
                        Sensor3204LL_Optimization_Setting();
                }

            }
        }
        else if (sensor_type == SENSOR_YS8008)
        {
            SensorYS8008_Optimization_Setting();
        }
    }
    //reg_0d = I2C_PAN3204LL_ReadRegister(0x0d);
    return sensor_type;
}


/**
 * @brief       This function opt 3207
 * @return      
 * @note        
 */
static inline void OPTSensor_surface_optimiz_3207(void)
{
    _attribute_data_retention_user static u8  surface_bak;
    u8  surface;
    surface = I2C_PAN3204LL_ReadRegister(0x33) & 0x1F;
    if ((surface < 4) && (surface_bak >= 4))
    {
        I2C_PAN3204LL_WriteRegister(0x09, 0x5A);
        I2C_PAN3204LL_WriteRegister(0x0D, 0x1B);
        I2C_PAN3204LL_WriteRegister(0x09, 0x00);
    }
    else if ((surface >= 4) && (surface_bak < 4))
    {
        I2C_PAN3204LL_WriteRegister(0x09, 0x5A);
        I2C_PAN3204LL_WriteRegister(0x0D, 0x16);
        I2C_PAN3204LL_WriteRegister(0x09, 0x00);
    }
    surface_bak = surface;
}


/**
 * @brief       This function report sensor data
 * @param[in]   no_overflow	
 * @return      
 * @note        
 */
_attribute_ram_code_ unsigned int OPTSensor_motion_report(u32 no_overflow)
{
    unsigned int optical_status = 0;
    int reg_x, reg_y;
#if TEST_MCU_CURRENT_DEBUG
    return  0;
    //sensor_set_wakeup_level_suspend(1);
#endif

    //sensor ph5205 no overflow read

    if ((product_id1 == 0x28) || (((product_id3 & 0x10) == 0x10 || ((product_id3 & 0x80) == 0x80)) &&
                                  sensor_type == SENSOR_3205)
            || (sensor_type == SENSOR_PAN3805EK) || (sensor_type == SENSOR_PAW3212) ||
            (sensor_type == SENSOR_OM16)|| (sensor_type==SENSOR_KA8G2)||(sensor_type==SENSOR_SW3851))	//damn 5205 has the exact same config with 3205 even its own IDs!!!

        no_overflow = 1;

    _attribute_data_retention_user static u8 resync_cnt = 0;

    if (mouse_cust_fct3065xy)
    {
       //printf("mouse cust fct3065\n");
        if (OPTSensor_resync(33))
        {
        	printf("mouse cust fct3065\n");
            return 0;
        }
    }
    else
    {
        if ((resync_cnt++ & 0x1f) == 0)
        {
            if (OPTSensor_resync(33))
            {
                return 0;
            }
            if (sensor_type == SENSOR_PAW3207)
            {
                OPTSensor_surface_optimiz_3207();
            }
        }
    }
    //---------------------------------------------------------
    // Read MOTION_STATUS regisgter, and then DELTA_X register, and
    // third DELTA_Y regisgter. The sequence is not suggested to change.
    //---------------------------------------------------------
#if SENSOR_MOTION_ENABLE
    if (gpio_read(PIN_SIF_MOTION))
    {
        return 0;
    }
#endif
    optical_status = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_MOTION_STATUS);

    if ((optical_status & MOTION_STATUS_MOT)  \
            || (no_overflow && ((optical_status & MOTION_STATUS_DXOVF) || (optical_status & MOTION_STATUS_DYOVF)))\
       )
    {

        reg_x = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_DELTA_X);	// DIRECTION_CLOCK_12 X
        reg_y = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_DELTA_Y);	// DIRECTION_CLOCK_12 Y
        if (sensor_type == SENSOR_PAN3805EK)
        {
#if (MOUSE_DATA_LEN_AAA==6)
            reg_x |= I2C_PAN3204LL_ReadRegister(0x11) << 8;
            reg_y |= I2C_PAN3204LL_ReadRegister(0x12) << 8;
#endif
            ms_data.x = reg_x;
            ms_data.y = reg_y;
            return 1;
        }

        else if (sensor_type == SENSOR_PAW3212)
        {
#if (MOUSE_DATA_LEN_AAA==6)
            u8 delta_xy_high = I2C_PAN3204LL_ReadRegister(0x12);
            s16 x, y;
            x = reg_x | ((delta_xy_high & 0xF0) << 4);
            y = reg_y | ((delta_xy_high & 0x0F) << 8);

            x = x * 16;
            y = y * 16;
            ms_data.x = x / 16;
            ms_data.y = y / 16;

#else
            ms_data.x = reg_x;
            ms_data.y = reg_y;
#endif
            return 1;
        }

        if (reg_x == 0x80)
            reg_x = 0x81;
        if (reg_y == 0x80)
            reg_y = 0x81;

        if (!no_overflow)
        {
            if (optical_status & MOTION_STATUS_DXOVF)//x over
                reg_x = ((signed char)reg_x >= 0) ? 0x81 : 0x7f; //x overflow, current<0,0x7f, current>0, 0xff

            if (optical_status & MOTION_STATUS_DYOVF)//y over
                reg_y = ((signed char)reg_y >= 0) ? 0x81 : 0x7f; //y overflow, current<0,0x7f, current>0, 0xff
        }

        /*
        		if(sensor_type == SENSOR_YS8006){
        			pBuf[0] = (mouse_status.cpi==2)  ? (signed char)(reg_x*3/4) : reg_x;
        			pBuf[1] = (mouse_status.cpi==2)  ? (signed char)(reg_y*3/4) : reg_y;
        		}
        */
        if (sensor_type == SENSOR_YS8008)//ys8008
        {
/*#if 0
            if ((reg_x == 0) && (reg_y == 0) && (loop_cnt >= 100)\\( mouse_sleep.mode == SLEEP_MODE_LONG_SUSPEND))
            {
                reg_x = 1;
                reg_y = 0;

            }
#endif*/
            //read YS8008 operation state, if it is not normal state, set reg_x=0,reg_y=0;
            if ((I2C_PAN3204LL_ReadRegister(0x08) & 0xFF) || (optical_status & 0x60))
            {
                reg_x = 0;//0
                reg_y = 0;//0
            }

            if (((abs((signed char)reg_x) > 0x01) || (abs((signed char)reg_y) > 0x01))\
                    && (abs((signed char)reg_x) < 0x5f) && (abs((signed char)reg_y) < 0x5f))
            {
#if (MOUSE_DATA_LEN_AAA==6)
                ms_data.x = ((signed char)reg_x >> 1) * 256;
                ms_data.y = ((signed char)reg_y >> 1) * 256;
#else
                ms_data.x = ((signed char)reg_x >> 1);
                ms_data.y = ((signed char)reg_y >> 1);
#endif
            }
            else
            {
#if (MOUSE_DATA_LEN_AAA==6)
                ms_data.x = reg_x * 256;
                ms_data.y = reg_y * 256;
#else
                ms_data.x = reg_x;
                ms_data.y = reg_y;
#endif
            }
#if (MOUSE_DATA_LEN_AAA==6)
            ms_data.x = ms_data.x / 256;///256
            ms_data.y = ms_data.y / 256;///256
#endif
        }
        else

        {
#if (MOUSE_DATA_LEN_AAA==6)

            ms_data.x = reg_x * 256;//regx*256
            ms_data.y = reg_y * 256;//regy*256
            ms_data.x = ms_data.x / 256;///256
            ms_data.y = ms_data.y / 256;///256
#else
            ms_data.x = reg_x;//x
            ms_data.y = reg_y;//y
#endif
        }


        return 1;
    }
    else
    {
        return 0;
    }

}


// Must clear SLP_ENH bit in the OPERATION_MODE register before using Power-Down mode.

/**
 * @brief       This function shut down opt sensor
 * @return      
 * @note        
 */
void OPTSensor_Shutdown(void)
{
#if SENSOR_SHUT_DOWN_ENABLE
    unsigned int i;
    for (i = 0; i < 16; i++)
    {
#if SENSOR_8640_ENABLE
        if (sensor_type == SENSOR_SIGMA_8630)
        {
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_OPERATION_MODE, SIGMA_8630_SLEEP_2);  //must set sleep2
            WaitUs(5001); 		//must wait 2000us in sleep2 mode before set power down
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_CONFIGURATION, CONFIG_POWERDOWN_3204);
            return;
        }
        else
#endif
        {
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_OPERATION_MODE, SLEEP_DISABLE_3204);
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_CONFIGURATION, CONFIG_POWERDOWN_3204);


            if (sensor_type == SENSOR_YS8008)//ys 8008
            {
                return;
            }
            else if (sensor_type == SENSOR_KA9)//ka9
            {
                SensorKA9_Shutdown_Setting();
            }


        }

        if (I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_CONFIGURATION) != CONFIG_POWERDOWN_3204)
        {
            OPTSensor_resync(33);
        }
        else
        {
            i = 16;
        }
    }
    sensor_gpio_powerDownConfig();
#endif
}


//unsigned char PAN3204LL_dpi_ctrl_table[] = {CPI800_3204LL, CPI1200_3204LL, CPI1600_3204LL, 0};

const unsigned char DPI_convert_table[LAST_SUPPORT_SENSOR] = {3, 0, 1, 1, 1, 4, 0, 3, 3, 0, 0, 3, 0, 0};	//paw3212 = 0x09

//const unsigned char DPI_convert_table[LAST_SUPPORT_SENSOR] = {3, 0, 1, 1, 1, 4, 0, 3, 3, 0, 3, 0};	//paw3212 = 0x09

const u8 paw3212_dpi_tbl[] =
{
#if(0)
#if (0)//(PRJ_NAME==WANG_HONG_TAI_BEI_PRJ)

    16,				// cpi_600 =  16*38 = 608, step = 38
    22,
    27,				// cpi_1000 = 27*38 = 1026
    42,				// cpi_1600 = 42*38 = 1596
    63,				// cpi_2400 = 63*38 = 2394
    // cpi_3600 Counterfeiting
#else
    22, 			// cpi_800 =  22*38 = 836, step = 38
    27,
    32, 			// cpi_1200 = 32*38 = 1216
    42, 			// cpi_1600 = 42*38 = 1596
    63, 			// cpi_2400 = 63*38 = 2394
    // cpi_3600 Counterfeiting
#endif
#endif
    22, 			// cpi_800 =  22*38 = 836, step = 38
    32, 			// cpi_1200 = 32*38 = 1216
    42, 			// cpi_1600 = 42*38 = 1596
    63, 

};


/**
 * @brief       This function update opt sensor dpi
 * @param[in]   cpi_ctrl	- cpi ctrl
 * @return      
 * @note        
 */
unsigned int OPTSensor_dpi_update(unsigned int cpi_ctrl)
{
    unsigned int dpi_tbl, dpi_reg;


/*#if 0
    if (product_id1 != PAN3204LL_PRODUCT_ID1)
        if (product_id2 != 0x70)	//sendor 3065
        {
            dpi_tbl = cpi_ctrl + 1;  //sensor 3065(old) and 3205-TJDN convert value is 1
        }
        else		//sensor 3065 new
        {
            dpi_tbl = cpi_ctrl ;
        }
#else*/
    if (product_id1 == PAN3204LL_PRODUCT_ID1_3065) 		//FCT3065 sensor
    {
        if (product_id2 != 0x70)	//sendor 3065
        {
            dpi_tbl = cpi_ctrl + 1;  //sensor 3065(old) and 3205-TJDN convert value is 1
        }
        else		//sensor 3065 new
        {
            dpi_tbl = cpi_ctrl ;
        }
    }
//#endif
    else if (product_id1 == 0x28)

        dpi_tbl = cpi_ctrl + 3;  //sensor ph5205

    else if (product_id2 == 0x02) //3212
    {
        // if(sensor_type == SENSOR_PAW3212)
        // {
        dpi_tbl = paw3212_dpi_tbl[cpi_ctrl];
        // }
        //else
        //{
        //dpi_tbl =cpi_ctrl;
        //}
    }
    else
    {

        dpi_tbl = cpi_ctrl + DPI_convert_table[sensor_type];
    }
    int timeout = 0;
#if (SENSOR_PAW3212_ENABLE)
    if (sensor_type == SENSOR_PAW3212)
    {
        unsigned int dpi_reg_1, dpi_reg_2;
        do
        {
            I2C_PAN3204LL_WriteRegister(REG_PAW3212_CPI_X,	dpi_tbl);//cpi x
            I2C_PAN3204LL_WriteRegister(REG_PAW3212_CPI_Y,  dpi_tbl);//cpi y
            dpi_reg_1 = I2C_PAN3204LL_ReadRegister(REG_PAW3212_CPI_X);//x
            dpi_reg_2 = I2C_PAN3204LL_ReadRegister(REG_PAW3212_CPI_Y);//y
#if (MODULE_WATCHDOG_ENABLE)
			wd_clear(); //clear watch dog
#endif

        }
        while ((dpi_reg_1 == dpi_reg_2) && (dpi_reg_1 != dpi_tbl) && (++timeout < 32));
        dbg_sensor_cpi = dpi_reg_1;
        return (dpi_reg_1 == dpi_tbl);
    }
    else
#endif
    {
        do
        {
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_CONFIGURATION, dpi_tbl);
            dpi_reg = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_CONFIGURATION);
#if (MODULE_WATCHDOG_ENABLE)
			wd_clear(); //clear watch dog
#endif
        }
        while ((dpi_reg != dpi_tbl) && (++timeout < 32));
        dbg_sensor_cpi = dpi_reg;
        return (dpi_reg == dpi_tbl);
    }
}

const u8 pan3805_dpi_tbl[] =
{
    0x1f, 0x22, //800
    0x26, 0x2a, //1000
    0x3c, 0x43, //1600
    0x5b, 0x64, //2400
};

_attribute_data_retention_user  u8 xy_multiple_flag = 0;
const u8 KA8G2_dpi_tbl[] =
{
	17, //1000
	40, //2400
	67, //4000
};

void KA8G2_CPI_set(u8 dpi_val)
{
	for (u8 i = 0; i < 16; i++)
	{
		u8	reg_1 = 0, reg_2 = 0;
		I2C_PAN3204LL_WriteRegister(0x09,0xA5);
		I2C_PAN3204LL_WriteRegister(0x0d, dpi_val);
		I2C_PAN3204LL_WriteRegister(0x0e, dpi_val);
		reg_1 = I2C_PAN3204LL_ReadRegister(0x0d);
		reg_2 = I2C_PAN3204LL_ReadRegister(0x0e);
		I2C_PAN3204LL_WriteRegister(0x09,0x00);
		if ((reg_1 == reg_2)&&(reg_2 == dpi_val))
		{
		    return;
		}
	}
}


/**
 * @brief       This function set sensor dpi
 * @param[in]   dpi	- 
 * @return      
 * @note        
 */
void sensor_dpi_set11(u8 dpi)
{
    xy_multiple_flag = 0;
    u8	reg_1, reg_2;

    if (sensor_type == SENSOR_PAN3805EK)
    {
        for (u8 i = 0; i < 8; i++)
        {
            I2C_PAN3204LL_WriteRegister(0x0d, pan3805_dpi_tbl[(dpi) * 2]);
            I2C_PAN3204LL_WriteRegister(0x0e, pan3805_dpi_tbl[(dpi) * 2 + 1]);
            reg_1 = I2C_PAN3204LL_ReadRegister(0x0d);
            reg_2 = I2C_PAN3204LL_ReadRegister(0x0e);
            if ((reg_1 == pan3805_dpi_tbl[(dpi) * 2]) && (reg_2 == pan3805_dpi_tbl[(dpi) * 2 + 1]))
            {
                break;
            }
        }

    }
    else if ((sensor_type == SENSOR_PAW3212) && (dpi == 4))//3212 and dpi 4
    {
        xy_multiple_flag = MULTIPIPE_1_DOT_5;//1.5
        OPTSensor_dpi_update(3);//update dpi
    }
    else
    {
        OPTSensor_dpi_update(dpi);//update dpi
    }
}
const u8 dpi_table[LAST_SUPPORT_SENSOR] = {3, 0, 1, 1, 1, 0, 0, 3, 3, 1, 0, 3, 1, 1};//dpi table


/**
 * @brief       This function set sensor dpi
 * @param[in]   dpi	- dpi value
 * @return      
 * @note        
 */
void sensor_dpi_set(u8 dpi)
{

    u8 cnt = 0;
#if MUTI_SENSOR_ENABLE //multi sensor
    xy_multiple_flag = 0; //0
#endif
	if(sensor_type==SENSOR_KA8G2)
		{
			KA8G2_CPI_set(KA8G2_dpi_tbl[dpi]);
		}
	else{

    for (u8 i = 0; i < 8; i++)
    {
        u8	reg_1 = 0, reg_2 = 0;

        if (sensor_type == SENSOR_PAW3212)//3212
        {
#if 1//S_3212_ENABLE
            if (dpi < 4)
            {
                cnt = dpi;
            }
            else
            {
#if MUTI_SENSOR_ENABLE//sensor en
                xy_multiple_flag = MULTIPIPE_1_DOT_5;//1.5
#endif
                cnt = 3;
            }
			I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_WRITE_PROTECT,0x5A);
            I2C_PAN3204LL_WriteRegister(REG_PAW3212_CPI_X, paw3212_dpi_tbl[cnt]);
            I2C_PAN3204LL_WriteRegister(REG_PAW3212_CPI_Y, paw3212_dpi_tbl[cnt]);
            reg_1 = I2C_PAN3204LL_ReadRegister(REG_PAW3212_CPI_X);
            reg_2 = I2C_PAN3204LL_ReadRegister(REG_PAW3212_CPI_Y);
            if ((reg_1 == reg_2) && (reg_2 == paw3212_dpi_tbl[cnt]))
            {
            	I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_WRITE_PROTECT,0x00);
				printf("set paw 3212 dpi ok\n");
                return;
            }
#endif
        }
#if 1//S_3805_ENABLE
        else if (sensor_type == SENSOR_PAN3805EK)
        {

            I2C_PAN3204LL_WriteRegister(0x0d, pan3805_dpi_tbl[(dpi) * 2]);
            I2C_PAN3204LL_WriteRegister(0x0e, pan3805_dpi_tbl[(dpi) * 2 + 1]);
            reg_1 = I2C_PAN3204LL_ReadRegister(0x0d);
            reg_2 = I2C_PAN3204LL_ReadRegister(0x0e);
            if ((reg_1 == pan3805_dpi_tbl[(dpi) * 2]) && (reg_2 == pan3805_dpi_tbl[(dpi) * 2 + 1]))
            {
                return;
            }
        }
#endif
        else
        {
            cnt = dpi_table[sensor_type];
            if (dpi != 0)//dpi not 0
            {
                cnt = cnt + 1 + dpi;//update cnt
            }

            if (product_id1 == PAN3204LL_PRODUCT_ID1_3065) //FCT3065 sensor
            {
                if (product_id2 == 0x70)//sendor 3065xyd
                {
                    //cnt = cnt - 1;//cnt--
                    cnt = dpi;
                }
				else
				{
					cnt = dpi;
				}
				printf("3065 set dpi %d\n",cnt);
            }
            reg_2 = (I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_CONFIGURATION) & 0xf8) | (cnt & 0x07);
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_CONFIGURATION, reg_2);
            reg_1 = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_CONFIGURATION);
            if (reg_1 == reg_2)
            {
                return;
            }
        }
    }
 }
}

/**
 * @brief       This function set btn cpi
 * @param[in]   dpi_val	- cpi value
 * @return      
 * @note        
 */
void btn_dpi_set()
{
    u8 dpi = 0; //init 0
    u8 max_cnt = 3;//init max 3


    if (sensor_type == SENSOR_PAN3805EK)//3805
    {
        max_cnt = 4;//4
    }
    else if (sensor_type == SENSOR_PAW3212)//3212
    {
        max_cnt = 4;//max 5
    }
	else if(sensor_type == SENSOR_3205)
	{
		if(product_id2 == 0x70)
			{
				max_cnt = 4;
			}
		else
			{
				max_cnt = 5;
			}
	}
	
    if (fun_mode == RF_1M_BLE_MODE)//ble
    {
        flash_dpi_info.bt_dpi++;//dpi ++
        if (flash_dpi_info.bt_dpi >= max_cnt)//>max
        {
            flash_dpi_info.bt_dpi = 0;//0
        }
        dpi = flash_dpi_info.bt_dpi;//dpi update
    }
    else if(fun_mode == RF_2M_2P4G_MODE)//24g
    {
        flash_dpi_info.d24g_dpi++;//dpi ++
        if (flash_dpi_info.d24g_dpi >= max_cnt)//>max
        {
            flash_dpi_info.d24g_dpi = 0;//0
        }
        dpi = flash_dpi_info.d24g_dpi;//dpi update
    }
    else if(fun_mode == USB_MODE)//usb
    {
        flash_dpi_info.usb_dpi++;//dpi ++
        if (flash_dpi_info.usb_dpi >= max_cnt)//>max
        {
            flash_dpi_info.usb_dpi = 0;//0
        }
        dpi = flash_dpi_info.usb_dpi;//set dpi
    }   	

    sensor_dpi_set(dpi);//set sensor dpi
 

#if BLT_APP_LED_ENABLE//led en
    dpi_led_set(dpi + 1);//show dpi led
#endif

    save_data_to_flash(MOUSE_DPI_ADDR, 8, (u8 *)&flash_dpi_info.bt_dpi, (int *)&flash_dpi_info.idx);//save dpi value to flash

}
void dpi_set(u8 dpi_value)
{
    u8 dpi = 0; //init 0
    u8 max_cnt = 3;//init max 3


    if (sensor_type == SENSOR_PAN3805EK)//3805
    {
        max_cnt = 4;//4
    }
    else if (sensor_type == SENSOR_PAW3212)//3212
    {
        max_cnt = 5;//max 5
    }
	else if(sensor_type == SENSOR_3205)
	{
		if(product_id2 == 0x70)
			{
				max_cnt = 4;
			}
		else
			{
				max_cnt = 5;
			}
	}
	
    if (fun_mode == RF_1M_BLE_MODE)//ble
    {
        flash_dpi_info.bt_dpi = dpi_value;//dpi ++
        if (flash_dpi_info.bt_dpi >= max_cnt)//>max
        {
            flash_dpi_info.bt_dpi = 0;//0
        }
        dpi = flash_dpi_info.bt_dpi;//dpi update
    }
    else if(fun_mode == RF_2M_2P4G_MODE)//24g
    {
        flash_dpi_info.d24g_dpi = dpi_value;//dpi ++
        if (flash_dpi_info.d24g_dpi >= max_cnt)//>max
        {
            flash_dpi_info.d24g_dpi = 0;//0
        }
        dpi = flash_dpi_info.d24g_dpi;//dpi update
    }
    else if(fun_mode == USB_MODE)//usb
    {
        flash_dpi_info.usb_dpi = dpi_value;//dpi ++
        if (flash_dpi_info.usb_dpi >= max_cnt)//>max
        {
            flash_dpi_info.usb_dpi = 0;//0
        }
        dpi = flash_dpi_info.usb_dpi;//set dpi
    }   	

    sensor_dpi_set(dpi);//set sensor dpi

#if BLT_APP_LED_ENABLE//led en
    dpi_led_set(dpi + 1);//show dpi led
#endif

    save_data_to_flash(MOUSE_DPI_ADDR, 8, (u8 *)&flash_dpi_info.bt_dpi, (int *)&flash_dpi_info.idx);//save dpi value to flash

}

/**
 * @brief       This function wakeup sensor 3204
 * @param[in]   sensor	- sensor
 * @return      
 * @note        
 */
int Sensor3204_Wakeup(u32 sensor)
{
#if SENSOR_SHUT_DOWN_ENABLE

    int timeout = 0;
    u8 debug_reg5;
    I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_CONFIGURATION,
                                2);    //clear BIT(3) to exit Power down mode, deault cpi-rate-2

    if (SENSOR_YS8008 == sensor_type)
    {
        WaitUs(5001);
    }

    if (sensor_type == SENSOR_KA9)
    {
        SensorKA9_WakeUp_Setting();
    }
    else
    {

        do
        {
            I2C_PAN3204LL_WriteRegister(REG_PAN3204LL_OPERATION_MODE, WAKEUP_3204);			//sleep enable and wakeup

            debug_reg5 = I2C_PAN3204LL_ReadRegister(REG_PAN3204LL_OPERATION_MODE);
            OPTSensor_resync(33);
#if (MODULE_WATCHDOG_ENABLE)
			wd_clear(); //clear watch dog
#endif
        }
        while ((debug_reg5 != WAKEUP_3204) && (++timeout < 32));
    }
#endif
    return 1;
}


/**
 * @brief       This function check sensor dir
 * @param[in]   sensor_dir	- 
 * @return      
 * @note        
 */
void check_sensor_dircet(u8 sensor_dir)
{
    s16 temp;
    if (sensor_type == SENSOR_PAN3805EK)//3805
    {
        if (sensor_dir == SENSOR_DIRECTION_CLOCK_3)//3
        {
            temp = ms_data.y;//temp y
            ms_data.y = ms_data.x;//y=x
            ms_data.x = -temp;//x=-y
        }
        else if (sensor_dir == SENSOR_DIRECTION_CLOCK_6)//6
        {
            ms_data.y = -ms_data.y;//y=-y
            ms_data.x = -ms_data.x;//x =-x

        }
        else if (sensor_dir == SENSOR_DIRECTION_CLOCK_9)//9
        {
            temp = ms_data.y;//temp y
            ms_data.y = -ms_data.x;//y =-x
            ms_data.x = temp;//x = y
        }

    }
    else
    {
        if (sensor_dir == SENSOR_DIRECTION_CLOCK_3)//3
        {
            temp = ms_data.y;//tmp y
            ms_data.y = ms_data.x;//y=x
            ms_data.x = temp;//x =y
        }
        else if (sensor_dir == SENSOR_DIRECTION_CLOCK_6)//6
        {
            ms_data.x = -ms_data.x;//x = -x
        }
        else if (sensor_dir == SENSOR_DIRECTION_CLOCK_9)//9
        {
            temp = ms_data.y;//tmp y
            ms_data.y = -ms_data.x;//y=-x
            ms_data.x = -temp;//x =-y
        }

        else if (sensor_dir == SENSOR_DIRECTION_CLOCK_12)//12
        {
            ms_data.y = -ms_data.y;//y=-y
        }
    }

}

_attribute_data_retention_user int sm_sum_x, sm_pre_x, sm_sum_y, sm_pre_y;
/**
 * @brief       This function smoother x,y data
 * @return      
 * @note        
 */
void iir_smoother()
{
    sm_sum_x = sm_sum_x - sm_pre_x + ms_data.x;//sum -pre +now
    sm_pre_x = sm_sum_x / 2;//pre = sum/2
    ms_data.x = sm_pre_x; //pkt is now pre

    sm_sum_y = sm_sum_y - sm_pre_y + ms_data.y;//sum -pre +now
    sm_pre_y = sm_sum_y / 2;//pre = sum/2
    ms_data.y = sm_pre_y;//pkt is now pre
}


/**
 * @brief       This function smoother clear
 * @return      
 * @note        
 */
static inline void iir_smoother_clear(void)
{
    sm_sum_x = 0; //0
    sm_pre_x = 0; //0

    sm_sum_y = 0; //0
    sm_pre_y = 0; //0
}
#define sm_dyn_pth1  6
#define  sm_dyn_pth2 4
/**
 * @brief       This function adaptive smoother
 * @return      
 * @note        
 */
u8 adaptive_smoother()
{
    _attribute_data_retention_user static u8 asm_flg = 0;
    _attribute_data_retention_user static u32 sm_last_smoother_tick = 0;

    //auto clear asm sum when no data for a long time
    if (asm_flg && clock_time_exceed(sm_last_smoother_tick, 100000))
    {
        asm_flg = 0;//reset asm_flg 0
        iir_smoother_clear();//clear smoother
    }
    if (!asm_flg)
    {
        if ((abs(ms_data.x) > sm_dyn_pth1) || (abs(ms_data.y) > sm_dyn_pth1))
        {
            asm_flg = 1;
            iir_smoother();//smooth
        }
        else
        {
            asm_flg = 0;
            iir_smoother_clear();//smooth clear
        }
    }
    else
    {
        if ((abs(ms_data.x) < sm_dyn_pth2) && (abs(ms_data.y) < sm_dyn_pth2))
        {
            asm_flg = 0;
            iir_smoother_clear();//smoth clear
        }
        else
        {
            asm_flg = 1;
            iir_smoother();//smooth
        }
    }

    if (asm_flg)
    {
        sm_last_smoother_tick = clock_time();//update last smooth tick
    }

    return asm_flg;
}

void sensor_test()
{
		sif_spi_cs_high;
		 sif_spi_sda_high;
		 sif_spi_clk_high;
		 
		 sleep_ms(1);
		 sif_spi_cs_low;
		 sif_spi_sda_low;
		 sif_spi_clk_low;
		 sleep_ms(1);
}
#endif







