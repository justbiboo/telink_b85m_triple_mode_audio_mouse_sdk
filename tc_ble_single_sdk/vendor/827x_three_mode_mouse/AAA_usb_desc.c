#include "AAA_public_config.h"
#if USB_DESCRIPTOR_MY_SELF

typedef struct
{
	u8 Size;
	u8 Type;
	wchar_t str[22];
}KM_USB_Descriptor_String_t;

static const u8 mouse_report_desc[] = 
{
/*******************mouse report=66************************/
	0x05, 0x01,  // Usage Page (Generic Desktop)
	0x09, 0x02,  // Usage (Mouse)
	0xA1, 0x01,  // Collection (Application)
	0x85, 0X01,  // Report Id  
	0x09, 0x01,  //   Usage (Pointer)
	0xA1, 0x00,  //   Collection (Physical)
	0x05, 0x09,  //  Usage Page (Buttons)
	0x19, 0x01,  //  Usage Minimum (01) - Button 1
	0x29, 0x05,  //  Usage Maximum (05) - Button 5
	0x15, 0x00,  //  Logical Minimum (0)
	0x25, 0x01,  //  Logical Maximum (1)
	0x75, 0x01,  //  Report Size (1)
	0x95, 0x05,  //  Report Count (3)
	0x81, 0x02,  //  Input (Data, Variable, Absolute) - Button states
	0x75, 0x03,  //  Report Size (5)
	0x95, 0x01,  //  Report Count (1)
	0x81, 0x01,  //  Input (Constant) - Padding or Reserved bits

	0x05, 0x01, 	 //  Usage Page (Generic Desktop Control)
	0x09, 0x30,  // Usage (X)
	0x09, 0x31,  // Usage (Y)

	0x16, 0x01, 0x80, //  LOGICAL_MINIMUM(0)
	0x26, 0xff, 0x7f,
	0x75, 0x10, //	Report Size (16)
	0x95, 0x02, //	Report Count (2)
	0x81, 0x06, //	Input (Data, Variable, Relative)

	//0x05,0x01,			 //  Usage Page (Generic Desktop Control)
	0x09, 0x38, 		 //  Usage (Wheel)
	0x15, 0x81, 		 //  Logical Minimum (-4)
	0x25, 0x7F, 		 //  Logical Maximum (3)
	0x75, 0x08, 		 //  Report Size (3)
	0x95, 0x01, 		 //  Report Count (1)
	0x81, 0x06, 		 //  Input (Data, Variable, Relative)

	0xC0,		  //   End Collection
	0xC0,		  // End Collection

/*******************consume report=25************************/
	0x05, 0x0C,   // Usage Page (Consumer)
	0x09, 0x01,   // Usage (Consumer Control)
	0xA1, 0x01,   // Collection (Application)
	0x85, 0x02,   //	 Report Id
	0x75, 0x10, 	//global, report size 16 bits
	0x95, 0x01, 	//global, report count 1
	0x15, 0x01, 	//global, min  0x01
	0x26, 0x8c, 0x02, //global, max  0x28c
	0x19, 0x01, 	//local, min   0x01
	0x2a, 0x8c, 0x02, //local, max	  0x28c
	0x81, 0x00, 	//main,  input data varible, absolute
	0xc0,		  //main, end collection

/*******************system power report=33************************/
	0x05, 0x01, 	//gobal,  USAGE_PAGE 1 (Generic Desktop)
	0x09, 0x80, 	//local, usage ID 0x80 system control
	0xa1, 0x01, 	//main conllection
	0x85, 0x03, 	//global report ID 0x4
	
	0x09, 0x82, 	//local usage ID 0x82 system sleep
	0x09, 0x81, 	//local usage ID 0x81 system power down
	0x09, 0x83, 	//local usage ID 0x83 system wakeup

	0x15, 0x00, 	//global min 01
	0x25, 0x01, 	//gobal, max 3
	0x19, 0x01,  //  Usage Minimum (01) - Button 1
	0x29, 0x03,  //  Usage Maximum (03) - Button 3
	
	0x75,0x01,	   //global, report size 2
	0x95,0x03,	   //report count  1	
	0x81,0x02,	   //main, input data, var, abs, No Prefer, NULL state
	//0x75,0x03,  //	 Report Size (5)
	0x95,0x05,	//	 Report Count (1)
	0x81,0x01,	//	 Input (Constant) - Padding or Reserved bits

	0xc0,		 //end of collection

};

static const u8 kb_report_desc[] = 
{
//keyboard report in
	0x05, 0x01,	 // Usage Pg (Generic Desktop)
	0x09, 0x06,	 // Usage (Keyboard)
	0xA1, 0x01,	 // Collection: (Application)
	//0x85, REPORT_ID_KEYBOARD_INPUT_AAA,	 // Report Id (keyboard)
			   //
	0x05, 0x07,	 // Usage Pg (Key Codes)
	0x19, 0xE0,	 // Usage Min (224)  VK_CTRL:0xe0
	0x29, 0xE7,	 // Usage Max (231)  VK_RWIN:0xe7
	0x15, 0x00,	 // Log Min (0)
	0x25, 0x01,	 // Log Max (1)
			   //
			   // Modifier byte
	0x75, 0x01,	 // Report Size (1)   1 bit * 8
	0x95, 0x08,	 // Report Count (8)
	0x81, 0x02,	 // Input: (Data, Variable, Absolute)
			   //
			   // Reserved byte
	0x95, 0x01,	 // Report Count (1)
	0x75, 0x08,	 // Report Size (8)
	0x81, 0x01,	 // Input: (Constant)

//keyboard output
	//5 bit led ctrl: NumLock CapsLock ScrollLock Compose kana
	0x95, 0x05,	//Report Count (5)
	0x75, 0x01,	//Report Size (1)
	0x05, 0x08,	//Usage Pg (LEDs )
	0x19, 0x01,	//Usage Min
	0x29, 0x05,	//Usage Max
	0x91, 0x02,	//Output (Data, Variable, Absolute)
	//3 bit reserved
	0x95, 0x01,	//Report Count (1)
	0x75, 0x03,	//Report Size (3)
	0x91, 0x01,	//Output (Constant)

	// Key arrays (6 bytes)
	0x95, 0x06,	 // Report Count (6)
	0x75, 0x08,	 // Report Size (8)
	0x15, 0x00,	 // Log Min (0)
	0x26, 0xF1,0x00,	 // Log Max (241)
	0x05, 0x07,	 // Usage Pg (Key Codes)
	0x19, 0x00,	 // Usage Min (0)
	0x2a, 0xf1,0x00,	 // Usage Max (241)
	0x81, 0x00,	 // Input: (Data, Array)

	0xC0,			  // End Collection
};
 
static const u8 spp_report_desc[] = 
{
/*******************ota page************************/
	0x06,0xEF,0xff,  //global usage page
	0x09,0x00,	   //usage undefined
	0xa1,0x01,	   //main collection
	0x85,0x05,	   //global report ID 0x5		//luyong report id 05 for usb upgrate read and write

	0x15, 0x00, 	//LOGICAL_MINIMUM (0)
	0x26, 0xff,0x00, //LOGICAL_MAXIMUM (255)
	0x95, (USB_OTA_LENGTH-1),    	//Report Count (32)	//MTU_SIZE=23
	0x75, 0x08,    	//Report Size (8)
	0x09,0x01,
	0x81, 0x02, 	//INPUT (Data,Var,Abs)
	0x09,0x02,
	0x91, 0x02,		//Output (Data, Variable, Absolute)
	0x09,0x03,
	0xB1, 0x02,		//Output (Data, Variable, Absolute)
	0xc0,			//main, end collection
};


u8 device_desc_km[18] = 
{
    0x12,  		// sizeof(USB_Descriptor_Device_t)
    0x01,  		// Type = DTYPE_Device
    0x10,0x01,	// 0x0110 : USBSpecification, USB 1.1
    0x00,       // Class : USB_CSCP_NoDeviceClass
    0x00, 		// SubClass : USB_CSCP_NoDeviceSubclass
    0x00, 		// Protocol : USB_CSCP_NoDeviceProtocol
    0x08,       // Endpoint0Size, Maximum Packet Size for Zero Endpoint. Valid Sizes are 8, 16, 32, 64
    0x8A,0x24, 	// VendorID 0x248a   8  9
    0x49,0x5B, 	// ProductID defult value   mouse only:2781  mouse_keyboard_kit:2782
    0x00,0x01,	// 0x0100 : ReleaseNumber
    0x01, 		// ManufacturerStrIndex = USB_STRING_VENDOR   @@@ pay attention
    0x02,		// ProductStrIndex = USB_STRING_PRODUCT		  @@@ pay attention
    0x00, 		// SerialNumStrIndex=USB_STRING_SERIAL
    0x01         // NumberOfConfigurations
};

u8 km_cfg_desc[9+9+9+7+9+9+7]=
{
//USB_Descriptor_Configuration_Header_t
	0x09,			//sizeof(USB_Descriptor_Configuration_Header_t)
	0x02,  			//type= DTYPE_Configuration
	sizeof(km_cfg_desc),0x00,     //TotalConfigurationSize,  @@@@ will change @@@@	//0x5b  //0x42
	0x02,           //TotalInterfaces          @@@@ will change @@@@
	0x01,      		//Configuration index
	0x00,      		//ConfigurationStrIndex = NO_DESCRIPTOR
	0xa0,           //ConfigAttributes = USB_CONFIG_ATTR_RESERVED | USB_CONFIG_ATTR_REMOTEWAKEUP
	0x32,           //MaxPowerConsumption = USB_CONFIG_POWER_MA(50)  MaxPower = 100mA
	
//--------------0 Interface_t Descriptor--------------------------------------
	0x09, 		//size = sizeof(USB_Descriptor_Interface_t)
	0x04, 	  	 //type = DTYPE_Interface
	0x00,  		//InterfaceNumber          @@@@ will change @@@@
	0x00,  		//AlternateSetting = 0
	0x01,  		//TotalEndpoints = 1@@@
	0x03, 		//Class = HID_CSCP_HIDClass						//determine hid 
	0x01, 		//SubClass = HID_CSCP_BootSubclass,
	0x02,		//Protocol = HID_CSCP_MouseBootProtocol
	0,          //InterfaceStrIndex = NO_DESCRIPTOR
//----------------HID_Descriptor---------------------------------------------
	0x09,			 //size = sizeof(USB_HID_Descriptor_HID_t)
	0x21,			 //type = HID_DTYPE_HID
	0x11,0x01,		 //HIDSpec = 0x0111
	0x21,			 //CountryCode = USB_HID_COUNTRY_US
	0x01,				 //TotalReportDescriptors
	0x22,			 //HIDReportType = HID_DTYPE_Report
	sizeof(mouse_report_desc), 0x00,	  //HIDReportLength[2] = {sizeof(mouse_report_desc), 0x00}
//------------------Endpoint Description-----------------
	0x07,			//size = sizeof(USB_Descriptor_Endpoint_t)
	0x05,			//type = DTYPE_Endpoint
	0x80| USB_EDP_MOUSE, //EndpointAddress = ENDPOINT_DIR_IN | USB_EDP_MOUSE,
	0x03,			//Attributes = EP_TYPE_INTERRUPT
	0x08,0x00,		//EndpointSize = 0x0008
	1,				//PollingIntervalMS = USB_MOUSE_POLL_INTERVAL

//--------------1 Interface_t Descriptor--------------------------------------
	0x09,			 //size = sizeof(USB_Descriptor_Interface_t)
	0x04,			 //type = DTYPE_Interface
	0x01,			 //InterfaceNumber			@@@@ will change @@@@
	0,				 //AlternateSetting = 0
	1,				 //TotalEndpoints = 1
	0x03,			 //Class = HID_CSCP_HIDClass
	0x00,			 //SubClass = HID_CSCP_BootSubclass,
	0x00,			 //Protocol = HID_CSCP_KeyboardBootProtocol,
	0,				 //InterfaceStrIndex = NO_DESCRIPTOR
//----------------HID_Descriptor---------------------------------------------
	0x09,			 //size = sizeof(USB_HID_Descriptor_HID_t)
	0x21,			 //type = HID_DTYPE_HID
	0x11, 0x01, 	 // HIDSpec = 0x0111
	0x21, 			 //CountryCode = USB_HID_COUNTRY_US
	1,				 //TotalReportDescriptors
	0x22,			 //HIDReportType = HID_DTYPE_Report
	sizeof(spp_report_desc), 0x00,		//Report size
//------------------Endpoint Description-----------------
	0x07,			 //size = sizeof(USB_Descriptor_Endpoint_t)
	0x05,			 //type = DTYPE_Endpoint
	0x80 | USB_EDP_SPP_IN, //EndpointAddress = ENDPOINT_DIR_IN | USB_EDP_KEYBOARD_IN,
	0x03,			 //Attributes = EP_TYPE_INTERRUPT
	0x40,0x00,		 //EndpointSize = 0x0008
	0x01,			 //PollingIntervalMS = USB_KEYBOARD_POLL_INTERVAL = 10
};


KM_USB_Descriptor_String_t  km_vendor_desc = {
			14,               //sizeof(L"Telink")=14
			0x03,			  //DTYPE_String=0x03
			L"Telink"
};

KM_USB_Descriptor_String_t  km_product_desc = {
			36,               //sizeof(L"Wireless Receiver")=36
			0x03,			  //DTYPE_String=0x03
			L"Wireless Receiver"
};

KM_USB_Descriptor_String_t  km_serial_desc = {
			18,               //sizeof(L"TLSR8566")=18
			0x03,			  //DTYPE_String=0x03
			L"TLSR8278"
};
const USB_Descriptor_String_t km_language_desc = { {
		sizeof(USB_Descriptor_Hdr_t) + 2, DTYPE_String },
		{ LANGUAGE_ID_ENG } };


void set_string_from_addr(KM_USB_Descriptor_String_t *ptr, u32 addr)
{
	u8 *p;



	int j = 0;
	if(*p != 0xff){
		while(*p != 0xff){
			ptr->str[j++] = *p++;
		}
		ptr->str[j] = 0;
		ptr->Size = (j+1)<<1;
	}

	/*
	u8 len;
	u8 *p = (u8 *)(addr);
	if(*p != U8_MAX){
		len = (*p++)%MAX_STRING_LEN;
		ptr->Size = (len+1)<<1;
		for(int i=0;i<len;i++){
			ptr->str[i] = *p++;
		}
		ptr->str[len] = 0;
	}
	*/
}



unsigned char *usbdesc_get_device(void) {
	return (unsigned char *)(&device_desc_km);
}

unsigned char *usbdesc_get_configuration(void) {
	return (unsigned char *)(km_cfg_desc);
}
unsigned short usb_desc_get_configuration_size(){
	return km_cfg_desc[2];//len_total_km_cfg_desc;
}
u8 * usb_get_HID_DTYPE_Report(u8 index,u16 *g_response_len)
{
	if(index==0)
	{
		g_response_len[0]=sizeof(mouse_report_desc);
		return (u8*)&mouse_report_desc[0];
	}
	else //if(index==2)
	{
		g_response_len[0]=sizeof(spp_report_desc);
		return (u8*)&spp_report_desc[0];
	}
}
u8 * usb_get_HID_DTYPE_HID(u8 index,u16 *g_response_len)
{
	#if 1
	if(index==0)
	{
		g_response_len[0]=9+7;
		return (unsigned char*) (&km_cfg_desc[9+9]);
	}
	else //if(index==1)
	{
		g_response_len[0]=9+7;
		return (unsigned char*) (&km_cfg_desc[9+9+9+7+9]);
	}
	#else
	g_response_len[0]=9+7;
		return (unsigned char*) (&km_cfg_desc[9+9]);
	#endif
}


unsigned char *usbdesc_get_language(void) {
	return (unsigned char *)(&km_language_desc);
}
unsigned char *usbdesc_get_vendor(void) {
	return (unsigned char *)(&km_vendor_desc);
}
unsigned short usb_desc_get_vendor_size(void) {
	return km_vendor_desc.Size;
}
unsigned char *usbdesc_get_product(void) {
	return (unsigned char *)(&km_product_desc);
}
unsigned short usb_desc_get_product_size(void) {
	return km_product_desc.Size;
}
unsigned char *usbdesc_get_serial(void) {
	return (unsigned char *)(&km_serial_desc);
}
unsigned short usb_desc_get_serial_size(void) {
	return km_serial_desc.Size;
}
#endif
int usb_keyboard_hid_report_aaa(unsigned char *data)
{
 	if(reg_irq_src & FLD_IRQ_USB_PWDN_EN)
    {
		//user_resume_host();
		reg_usb_ep_ctrl(USB_EDP_KEYBOARD_IN) = 0;
		return 1;
    }

	if (!reg_usb_host_conn)
    {
    	reg_usb_ep_ctrl(USB_EDP_KEYBOARD_IN) = 0;
    	return 1;
    }
	 
	if(usbhw_is_ep_busy(USB_EDP_KEYBOARD_IN))
		return 0;

    //printf("\n KeyBoard HID report:");

	reg_usb_ep_ptr(USB_EDP_KEYBOARD_IN) = 0;

	for(int i = 0; i < 8; ++i){
    	reg_usb_ep_dat(USB_EDP_KEYBOARD_IN) = data[i];
    	//printf("0x%1x ",data[i]);
    }
	//	reg_usb_ep_ctrl(USB_EDP_KEYBOARD_IN) = FLD_EP_DAT_ACK;		// ACK

		usbhw_data_ep_ack(USB_EDP_KEYBOARD_IN);

	return 1;
}


/**
 * @brief       This function send mouse data to host
 * @param[in]   len	- data len
 * @param[in]   p	- data
 * @param[in]   report_id	- report id
 * @return      
 * @note        
 */
 int usb_mouse_hid_report_aaa(u8 report_id,unsigned char * p,u8 len)
{
	if(reg_irq_src & FLD_IRQ_USB_PWDN_EN)
	{
		//user_resume_host();
		reg_usb_ep_ctrl(USB_EDP_MOUSE) = 0;
		return 1;
	}

	if (!reg_usb_host_conn)
	{
		reg_usb_ep_ctrl(USB_EDP_MOUSE) = 0;
		return 1;
	}
	
	if(usbhw_is_ep_busy(USB_EDP_MOUSE))
	{
		return 0;
	}

	reg_usb_ep_ptr(USB_EDP_MOUSE) = 0;

	//p[0]: btn |p[1]锟�?xl8 |p[2]锟�?xh8 |p[3]锟�?yl8 |p[4]锟�?yh8 |p[5]锟�?wheel

	if(usb_mouse_report_proto== 0)
	{
		//reg_usb_ep_dat(USB_EDP_MOUSE) = x & 0xff;
		//reg_usb_ep_dat(USB_EDP_MOUSE) = y & 0xff;

		reg_usb_ep_dat(USB_EDP_MOUSE) = p[0];//btn
		reg_usb_ep_dat(USB_EDP_MOUSE) = p[1];// xl8
		#if 0  //8bit sensor  ykq change
		reg_usb_ep_dat(USB_EDP_MOUSE) = data[2];
		#else //16bit sensor
		  reg_usb_ep_dat(USB_EDP_MOUSE) = p[3]; //yl8 
		#endif
	}
	else
	{
		reg_usb_ep_dat(USB_EDP_MOUSE) = report_id;
		
		for(u8 i=0;i<len;i++)
		{
			reg_usb_ep_dat(USB_EDP_MOUSE) = p[i];
		}
	}
	//	reg_usb_ep_ctrl(USB_EDP_MOUSE) = FLD_EP_DAT_ACK;		// ACK

	usbhw_data_ep_ack(USB_EDP_MOUSE);

	return 1;
}
int usb_app_hid_report(u8 report_id,u8 *data, u16 length)
{
	if (usbhw_is_ep_busy(USB_EDP_SPP_IN))
	{

		return 0;
	}
	reg_usb_ep_ptr(USB_EDP_SPP_IN) = 0;
	if(report_id!=0)
	{
		reg_usb_ep_dat(USB_EDP_SPP_IN) = report_id;
	}
	//printf("tx=0x");
	for (u8 i=0; i<length; i++)
	{
		//printf("%01x",data[i]);
		reg_usb_ep_dat(USB_EDP_SPP_IN) = data[i];
	}
	//printf("\r\n");	
	
	usbhw_data_ep_ack(USB_EDP_SPP_IN);

	return 1;
}



/**
 * @brief       This function resume usb host
 * @return      
 * @note        
 */
void usb_resume_host(void)
{
	if ((reg_usb_mdev & BIT(2)) && (reg_irq_src & FLD_IRQ_USB_PWDN_EN) ){   // (reg_usb_mdev & BIT(2))
		reg_wakeup_en = FLD_WAKEUP_SRC_USB_RESM;
		reg_wakeup_en = FLD_WAKEUP_SRC_USB;
	}
}

