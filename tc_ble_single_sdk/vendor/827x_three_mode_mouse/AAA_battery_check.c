/********************************************************************************************************
 * @file     AAA_battery_check.c
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

#define DBG_ADC_ON_RF_PKT			0
#define DBG_ADC_SAMPLE_DAT			0

#define DBG_ADC_BLE_NOTIFY_YKQ      0


//_attribute_data_retention_	u8		adc_first_flg = 1;
_attribute_data_retention_	u8 		lowBattDet_enable = 1; //low batt en
u8      adc_hw_initialized = 0;   //note: can not be retention variable
_attribute_data_retention_  u16     batt_vol_mv; //batt vol
_attribute_data_retention_  u16     alarm_vol_mv; //alarm vol
_attribute_data_retention_  u16     max_vol_mv; //max vol
_attribute_data_retention_  u16     min_vol_mv; //min_vol
_attribute_data_retention_ u8 need_batt_data_notify = 0; //need batt data notify
//_attribute_data_retention_ u8 batt_data_change = 0;
u8 battery_led_repeat_time =0;


_attribute_data_retention_user	u32	lowBattDet_tick   = 0; //low batt tick init 0

_attribute_data_retention_user	u8	low_Batt_flag   = 0; //low batt flag init 0




#define ADC_SAMPLE_NUM		8    //sample num 8

#if (DBG_ADC_ON_RF_PKT || DBG_ADC_SAMPLE_DAT)
    _attribute_data_retention_	u16	adc_dat_min = 0xffff;
    _attribute_data_retention_	u16	adc_dat_max = 0;
#endif

#if (DBG_ADC_SAMPLE_DAT)
_attribute_data_retention_	volatile int *adc_dat_buf;
_attribute_data_retention_	volatile signed int adc_dat_raw[ADC_SAMPLE_NUM * 128];

_attribute_data_retention_	u8	adc_index = 0;

_attribute_data_retention_	u16 avg_convert_raw;
_attribute_data_retention_	u16 avg_convert_oct;

_attribute_data_retention_	u16 adc_average;

_attribute_data_retention_	u16 voltage_mv_oct;

_attribute_data_retention_	u16 adc_sample[ADC_SAMPLE_NUM] = {0};

_attribute_data_retention_	u32 adc_result;
#else

_attribute_data_retention_	volatile unsigned int adc_dat_buf[ADC_SAMPLE_NUM];  //size must 16 byte aligned(16/32/64...)

#endif





/**
 * @brief       This function set battery check enable 
 * @param[in]   en	- 
 * @return      
 * @note        
 */
void battery_set_detect_enable(int en)
{
    lowBattDet_enable = en;//lowbattdet en

    if (!en)//no en
    {
        adc_hw_initialized = 0;   //need initialized again
    }

}



/**
 * @brief       This function get low batt check enable or not
 * @return      
 * @note        
 */
int battery_get_detect_enable(void)
{
    return lowBattDet_enable;
}




/**
 * @brief       This function init adc vbat detect
 * @param[in]   chn	- adc chn
 * @return      
 * @note        
 */
_attribute_ram_code_ void adc_vbat_detect_init(u8 chn)
{
    /******power off sar adc********/
    adc_power_on_sar_adc(0);

    //telink advice: you must choose one gpio with adc function to output high level(voltage will equal to vbat), then use adc to measure high level voltage

   // gpio_set_output_en(GPIO_VBAT_DETECT, 1);
  //  gpio_write(GPIO_VBAT_DETECT, 1);

    /******set adc sample clk as 4MHz******/
    adc_set_sample_clk(5); //adc sample clk= 24M/(1+5)=4M

    /******set adc L R channel Gain Stage bias current trimming******/
   /////adc_set_left_right_gain_bias(GAIN_STAGE_BIAS_PER100, GAIN_STAGE_BIAS_PER100);

    //set misc channel en,  and adc state machine state cnt 2( "set" stage and "capture" state for misc channel)
    adc_set_chn_enable_and_max_state_cnt(ADC_MISC_CHN, 2);  	//set total length for sampling state machine and channel

    //set "capture state" length for misc channel: 240
    //set "set state" length for misc channel: 10
    //adc state machine  period  = 24M/250 = 96K, T = 10.4 uS
    adc_set_state_length(240, 10);  	//set R_max_mc,R_max_c,R_max_s

#if 1  //optimize, for saving time
    //set misc channel use differential_mode,
    //set misc channel resolution 14 bit,  misc channel differential mode
    //notice that: in differential_mode MSB is sign bit, rest are data,  here BIT(13) is sign bit
    analog_write(anareg_adc_res_m, RES14 | FLD_ADC_EN_DIFF_CHN_M);
    adc_set_ain_chn_misc(chn, GND);
#else
    ////set misc channel use differential_mode,
    adc_set_ain_channel_differential_mode(ADC_MISC_CHN, ADC_INPUT_PCHN, GND);

    //set misc channel resolution 14 bit
    //notice that: in differential_mode MSB is sign bit, rest are data,  here BIT(13) is sign bit
    adc_set_resolution(ADC_MISC_CHN, RES14);
#endif

    //set misc channel vref 1.2V
    adc_set_ref_voltage(ADC_VREF_1P2V);//1.2v

    //set misc t_sample 6 cycle of adc clock:  6 * 1/4M
#if 1   //optimize, for saving time
    adc_set_tsample_cycle_chn_misc(SAMPLING_CYCLES_6);  	//Number of ADC clock cycles in sampling phase
#else
    adc_set_tsample_cycle(ADC_MISC_CHN, SAMPLING_CYCLES_6);   	//Number of ADC clock cycles in sampling phase
#endif

    if (user_cfg.bat_type == 0x01) //dual battery
    {
        alarm_vol_mv = 2200;//2.2v
	  max_vol_mv = 3000;//3v
        min_vol_mv = 2100;//2.1v
#if       BATT_CHECK_ENABLE //batt check en
        gpio_set_output_en(GPIO_VBAT_DETECT, 1);//gpio vbat en
        gpio_write(GPIO_VBAT_DETECT, 1);//write vbat 1
#endif
    }
    else if (user_cfg.bat_type == 0x02)	//li battery
    {
        alarm_vol_mv = 1650; 			//3700/2 DIV
        max_vol_mv = 2075;              //4150/2 DIV
        min_vol_mv = 1500;              //3000/3 DIV
    }
    else
    {
        //alarm_vol_mv = 1600; 			//3700/2 DIV
        //max_vol_mv = 2075;              //4150/2 DIV
        //min_vol_mv = 1500;				//3000/3 DIV
        alarm_vol_mv = 1200; 			//3600/3 DIV
        max_vol_mv = 1350;              //4150/3 DIV
        min_vol_mv = 1100;				//3300/3 DIV
#if       BATT_CHECK_ENABLE //batt check en
        gpio_set_output_en(GPIO_VBAT_DETECT, 1);//gpio vbat en
        gpio_write(GPIO_VBAT_DETECT, 1);//write vbat 1
#endif
    }

	//set Analog input pre-scal.ing 1/4
    adc_set_ain_pre_scaler(ADC_PRESCALER_1F8);//ADC pre_scaling default value is ADC_PRESCALER_1F8, it can change after adc_base_init().
	
    /******power on sar adc********/
    //note: this setting must be set after all other settings
    adc_power_on_sar_adc(1);  //adc power on
}



/**
 * @brief       This function check app adc
 * @param[in]   chn	-  adc chn
 * @return      
 * @note        
 */
_attribute_ram_code_ int app_adc_check(u8 chn)
{
    u16 temp;
    int i, j;

    //when MCU powered up or wakeup from deep/deep with retention, adc need be initialized
    if (!adc_hw_initialized)
    {
        adc_hw_initialized = 1;
        adc_vbat_detect_init(chn);
    }
    else
    {
		adc_vbat_detect_init(chn);
    }

    adc_reset_adc_module();
    u32 t0 = clock_time();

#if (DBG_ADC_SAMPLE_DAT)
    adc_dat_buf = (int *)&adc_dat_raw[ADC_SAMPLE_NUM * adc_index];
#else
    u16 adc_sample[ADC_SAMPLE_NUM] = {0};
    u32 adc_result;
#endif
    for (i = 0; i < ADC_SAMPLE_NUM; i++)    	//dfifo data clear
    {
        adc_dat_buf[i] = 0;
    }
    while (!clock_time_exceed(t0, 25)); //wait at least 2 sample cycle(f = 96K, T = 10.4us)

    //dfifo setting will lose in suspend/deep, so we need config it every time
    adc_config_misc_channel_buf((u16 *)adc_dat_buf, ADC_SAMPLE_NUM << 2); //size: ADC_SAMPLE_NUM*4
    dfifo_enable_dfifo2();
	
    // get adc sample data and sort these data
    for (i = 0; i < ADC_SAMPLE_NUM; i++)
    {
#if (MODULE_WATCHDOG_ENABLE)
		wd_clear(); //clear watch dog
#endif
        while (!adc_dat_buf[i]);

        if (adc_dat_buf[i] & BIT(13))  //14 bit resolution, BIT(13) is sign bit, 1 means negative voltage in differential_mode
        {
            adc_sample[i] = 0;
        }
        else
        {
            adc_sample[i] = ((u16)adc_dat_buf[i] & 0x1FFF);  //BIT(12..0) is valid adc result
        }

#if (DBG_ADC_SAMPLE_DAT) //debug
        if (adc_sample[i] < adc_dat_min)
        {
            adc_dat_min = adc_sample[i];
        }
        if (adc_sample[i] > adc_dat_max)
        {
            adc_dat_max = adc_sample[i];
        }
#endif
        //insert sort
        if (i)
        {
            if (adc_sample[i] < adc_sample[i - 1])
            {
                temp = adc_sample[i];
                adc_sample[i] = adc_sample[i - 1];
                for (j = i - 1; j >= 0 && adc_sample[j] > temp; j--)
                {
                    adc_sample[j + 1] = adc_sample[j];
                }
                adc_sample[j + 1] = temp;
            }
        }
    }

    dfifo_disable_dfifo2();   //misc channel data dfifo disable

    // get average value from raw data(abandon some small and big data ), then filter with history data //////
#if (ADC_SAMPLE_NUM == 4)  	//use middle 2 data (index: 1,2)
    u32 adc_average = (adc_sample[1] + adc_sample[2]) / 2;
#elif(ADC_SAMPLE_NUM == 8) 	//use middle 4 data (index: 2,3,4,5)
    u32 adc_average = (adc_sample[2] + adc_sample[3] + adc_sample[4] + adc_sample[5]) / 4;
#endif

#if 1
    adc_result = adc_average;
#else  	//history data filter
    if (adc_first_flg)
    {
        adc_result = adc_average;
        adc_first_flg = 0;
    }
    else
    {
        adc_result = ((adc_result * 3) + adc_average + 2) >> 2; //filter
    }
#endif

   // adc sample data convert to voltage(mv) 
#if 0
    //                         (1200mV Vref, 1/4 scaler)   (BIT<12~0> valid data)       calibration rate
    //			 =  adc_result       * 4800              /        0x2000           *       (100/103)
    //           =  adc_result * 4800 * 100 / 0x2000 / 103
    //           =  adc_result * (480000/103) /0x2000
    //           =  adc_result * 4660 >>13
    //           =  adc_result * 1165 >>11
    batt_vol_mv  = (adc_result * 1165) >> 11;
#else

    //                         (1180mV Vref, 1/4 scaler)   (BIT<12~0> valid data)
    //			 =  adc_result   *   1180     * 4        /        0x2000
    //           =  adc_result * 4720 >>13
    //           =  adc_result * 295 >>9
   batt_vol_mv  = (adc_result * 590) >> 9;
   //batt_vol_mv  = (adc_result * adc_vref_cfg.adc_vref)>>10;
#endif

#if DBG_ADC_BLE_NOTIFY_YKQ
    if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) //conn status
    {
        bls_att_pushNotifyData(BATT_LEVEL_INPUT_DP_H, (u8*)&batt_vol_mv, 2);//push batt vol
    }

#endif

#if (DBG_ADC_ON_RF_PKT) //debug

    //debug
#if (!DBG_ADC_SAMPLE_DAT)
    u16 avg_convert_raw;
    u16 avg_convert_oct;
    u16 voltage_mv_oct;
#endif

    avg_convert_raw = (adc_average * 4800) >> 13;

    voltage_mv_oct = (batt_vol_mv / 1000) << 12 | ((batt_vol_mv / 100) % 10) << 8 \
                     | ((batt_vol_mv % 100) / 10) << 4  | (batt_vol_mv % 10);


    avg_convert_oct = (avg_convert_raw / 1000) << 12 | ((avg_convert_raw / 100) % 10) << 8 \
                      | ((avg_convert_raw % 100) / 10) << 4  | (avg_convert_raw % 10);

    u8	tbl_advData[4 + ADC_SAMPLE_NUM * 2 ] = {0};

    tbl_advData[0] = avg_convert_oct >> 8; //avg
    tbl_advData[1] = avg_convert_oct & 0xff;
    tbl_advData[2] = voltage_mv_oct >> 8; //vol
    tbl_advData[3] = voltage_mv_oct & 0xff;
    tbl_advData[4] = adc_dat_min >> 8; //min
    tbl_advData[5] = adc_dat_min & 0xff;
    tbl_advData[6] = adc_dat_max >> 8; //max
    tbl_advData[7] = adc_dat_max & 0xff;

    for (i = 0; i < ADC_SAMPLE_NUM; i++)
    {
        tbl_advData[8 + i * 2] = adc_sample[i] >> 8;
        tbl_advData[9 + i * 2] = adc_sample[i] & 0xff;
    }

    if (blc_ll_getCurrentState() == BLS_LINK_STATE_ADV) //adv 
    {
        bls_ll_setAdvData((u8 *)tbl_advData, sizeof(tbl_advData)); //set sdv data
    }
    else if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) //con
    {
        bls_att_pushNotifyData(BATT_LEVEL_INPUT_DP_H, tbl_advData, 4 + ADC_SAMPLE_NUM * 2); //notify data
    }
#endif

#if (DBG_ADC_SAMPLE_DAT) //debug
    adc_index ++;
    if (adc_index >= 128)
    {
        adc_index = 0;
    }
#endif
    return 1;

}


#if (BATT_CHECK_ENABLE)

/**
 * @brief       This function check battery power
 * @return      
 * @note        
 */
void user_battery_power_check()
{
    app_adc_check(ADC_INPUT_PCHN);//adc input chn check
    adc_power_on_sar_adc(0);//power off adc sar module

    u8 Baifen = 0;//init baifen
	my_printf_aaa("batt %d\n", batt_vol_mv);
    if (batt_vol_mv <= min_vol_mv) //less than min vol
    {
    		Baifen = 0;//baifen 0
		u32 led_pin=0; //led pin

		if (fun_mode == RF_1M_BLE_MODE)//ble mode
		{
			led_pin=PIN_BLE_LED;//ble led
		}
		else
		{
			led_pin=PIN_24G_LED;//24g led
		}

	#if (0 && BLT_APP_LED_ENABLE)  		//led indicate
		gpio_set_output_en(led_pin, 1);  	//output enable
		for(int k=0;k<10;k++)
		{
		#if (MODULE_WATCHDOG_ENABLE)  //wd en
			wd_clear(); //clear watch dog
		#endif
			gpio_write(led_pin, 1);//on
			sleep_us(150000);//150ms
		#if (MODULE_WATCHDOG_ENABLE)//wd en
			wd_clear(); //clear watch dog
		#endif
			gpio_write(led_pin, 0);//off
			sleep_us(150000);//150ms
		}
		gpio_set_output_en(led_pin, 0);//output dis
	
		enter_deep_aaa();//enter deep
	#endif
		
    }
    else if (batt_vol_mv < alarm_vol_mv) //less than alarm mv
    {
		//led_bat_lvd();//led bat show
    }
    else
   // {
      // ;// low_batt_flag = 0;
    //}


#if (DBG_ADC_BLE_NOTIFY_YKQ==0)

    if (batt_vol_mv >= max_vol_mv)
    {
        Baifen = 100;
    }
    else if (batt_vol_mv <= min_vol_mv)
    {
        Baifen = 0;
	  //ble_status_aaa = DEEP_SLEEPE_STATUS_AAA;
	  //enter_deep_aaa();
    }
    else
    {
        Baifen = (100 * (batt_vol_mv - min_vol_mv)) / (max_vol_mv - min_vol_mv);//set baifen
    }
    // Baifen = (Baifen / 25) * 25;
    if(Baifen>70)
	{
		mic_duration = 60;
	}
	else if(Baifen>=40)
	{
		mic_duration = 30;
	}
	else
	{
		mic_duration = 10;
	}

  #if 1
    if (my_batVal[0] != Baifen)//new batt value
    {
    	//batt_data_change =1;
        need_batt_data_notify = 1;//need notify new batt value
        my_batVal[0] = Baifen; //update my_batval
    }
  #else//debug
    need_batt_data_notify = 1;
    my_batVal[0]--;
  #endif
  
#endif
}

/**
 * @brief       This function init batt check
 * @return      
 * @note        
 */
void user_batt_check_init()
{
//#if 1
/*#if 0
    if (analog_read(DEEP_ANA_REG2) ==  LOW_BATT_FLG)
    {
        app_battery_power_check(VBAT_ALRAM_THRES_MV + 200);  //2.2 V
    }
#else*/
    user_battery_power_check();//power check

    if (low_Batt_flag) //low batt falg 1
    {
	#if (BLT_APP_LED_ENABLE)  //led indicate
        if (connect_ok) //connct ok
        {
            led_bat_lvd();//bat led show
        }

	#endif
    }
	
//#endif
//#endif
}



/**
 * @brief       This function pro battery check
 * @return      
 * @note        
 */
void user_batt_check_proc()
{

    if(battery_get_detect_enable() && clock_time_exceed(lowBattDet_tick, 5*1000000))//every 10s
    {
        lowBattDet_tick = clock_time();//update lowbattdet tick
        user_battery_power_check();//check battery
        //qingjing_battery_led_set(app_data.battery_percent);
        //printf("the battery valum is ")
    }
}
#endif

#if BUTTON_FUN_ENABLE_AAA

/**
 * @brief       This function btn adc check
 * @param[in]   chn	- channel
 * @return      
 * @note        
 */
u16  user_btn_adc_check_proc(u8 chn)
{
//#if 1

    app_adc_check(chn);//app check adc

    return batt_vol_mv;//return batt_vol

//#endif
}

#endif
