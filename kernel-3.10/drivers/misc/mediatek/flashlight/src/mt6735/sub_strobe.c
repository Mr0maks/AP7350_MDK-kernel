
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include "kd_flashlight.h"
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_hw.h"
#include <cust_gpio_usage.h>
#include <cust_i2c.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/xlog.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
#include <linux/mutex.h>
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#endif

#include <linux/i2c.h>
#include <linux/leds.h>



/******************************************************************************
 * Debug configuration
******************************************************************************/
// availible parameter
// ANDROID_LOG_ASSERT
// ANDROID_LOG_ERROR
// ANDROID_LOG_WARNING
// ANDROID_LOG_INFO
// ANDROID_LOG_DEBUG
// ANDROID_LOG_VERBOSE
#define TAG_NAME "[sub_strobe.c]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    pr_debug(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_WARN(fmt, arg...)        pr_warning(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_NOTICE(fmt, arg...)      pr_notice(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_INFO(fmt, arg...)        pr_info(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_TRC_FUNC(f)              pr_debug(TAG_NAME "<%s>\n", __FUNCTION__)
#define PK_TRC_VERBOSE(fmt, arg...) pr_debug(TAG_NAME fmt, ##arg)
#define PK_ERROR(fmt, arg...)       pr_err(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)

#define DEBUG_LEDS_STROBE
#ifdef  DEBUG_LEDS_STROBE
	#define PK_DBG PK_DBG_FUNC
	#define PK_VER PK_TRC_VERBOSE
	#define PK_ERR PK_ERROR
#else
	#define PK_DBG(a,...)
	#define PK_VER(a,...)
	#define PK_ERR(a,...)
#endif



#ifndef LEDSSTROBE_LM3642_ENABLE
//control by BB gpio
#define GPIO_CAMERA_FLASH_MODE GPIO_CAMERA_FLASH_MODE_PIN //GPIO12
#define GPIO_CAMERA_FLASH_MODE_M_GPIO  GPIO_MODE_00
        //CAMERA-FLASH-T/F
            //H:flash mode
            //L:torch mode
#define GPIO_CAMERA_FLASH_EN GPIO_CAMERA_FLASH_EN_PIN//GPIO13
#define GPIO_CAMERA_FLASH_EN_M_GPIO  GPIO_MODE_00

#ifdef GPIO_CAMERA_FLASH_LEVEL_PIN//114
#define GPIO_FLASH_LEVEL GPIO_CAMERA_FLASH_LEVEL_PIN//114
#define GPIO_FLASH_LEVEL_M_GPIO  GPIO_CAMERA_FLASH_LEVEL_PIN_M_GPIO
#endif

#if 0
#define GPIO_CAMERA_FLASH_FRONT_EN GPIO_CAMERA_FLASH_EXT2_PIN//119 for front flashliht
#define GPIO_CAMERA_FLASH_FRONT_EN_M_GPIO GPIO_CAMERA_FLASH_EXT2_PIN_M_GPIO

//#define GPIO_CAMERA_FLASH_BACK_EN GPIO_CAMERA_FLASH_EXT1_PIN//118 for back flashlight
//#define GPIO_CAMERA_FLASH_BACK_EN_M_GPIO GPIO_CAMERA_FLASH_EXT1_PIN_M_GPIO
#else
#undef GPIO_CAMERA_FLASH_FRONT_EN
#undef GPIO_CAMERA_FLASH_BACK_EN
#endif
        //CAMERA-FLASH-EN
#define TORCH_LIGHT_LEVEL 3 //just 1st level modified by tyrael
#define PRE_LIGHT_LEVEL 6
#endif

/******************************************************************************
 * local variables
******************************************************************************/

static DEFINE_SPINLOCK(g_strobeSMPLock); /* cotta-- SMP proection */


static u32 strobe_Res = 0;
static u32 strobe_Timeus = 0;
static BOOL g_strobe_On = 0;

static int g_duty=-1;
static int g_timeOutTimeMs=0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
static DEFINE_MUTEX(g_strobeSem);
#else
static DECLARE_MUTEX(g_strobeSem);
#endif


#define STROBE_DEVICE_ID 0xC6


static struct work_struct workTimeOut;

//#define FLASH_GPIO_ENF GPIO12
//#define FLASH_GPIO_ENT GPIO13

static int g_bLtVersion=0;

/*****************************************************************************
Functions
*****************************************************************************/
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);
extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
static void work_timeOutFunc(struct work_struct *data);

#ifdef LEDSSTROBE_LM3642_ENABLE

static struct i2c_client *LM3642_i2c_client = NULL;




struct Sub_LM3642_platform_data {
	u8 torch_pin_enable;    // 1:  TX1/TORCH pin isa hardware TORCH enable
	u8 pam_sync_pin_enable; // 1:  TX2 Mode The ENVM/TX2 is a PAM Sync. on input
	u8 thermal_comp_mode_enable;// 1: LEDI/NTC pin in Thermal Comparator Mode
	u8 strobe_pin_disable;  // 1 : STROBE Input disabled
	u8 vout_mode_enable;  // 1 : Voltage Out Mode enable
};

struct Sub_LM3642_chip_data {
	struct i2c_client *client;

	//struct led_classdev cdev_flash;
	//struct led_classdev cdev_torch;
	//struct led_classdev cdev_indicator;

	struct Sub_LM3642_platform_data *pdata;
	struct mutex lock;

	u8 last_flag;
	u8 no_pdata;
};

/* i2c access*/
/*
static int LM3642_read_reg(struct i2c_client *client, u8 reg,u8 *val)
{
	int ret;
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);

	if (ret < 0) {
		PK_ERR("failed reading at 0x%02x error %d\n",reg, ret);
		return ret;
	}
	*val = ret&0xff;

	return 0;
}*/

static int LM3642_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret=0;
	struct Sub_LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret =  i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		PK_ERR("failed writting at 0x%02x\n", reg);
	return ret;
}

static int LM3642_read_reg(struct i2c_client *client, u8 reg)
{
	int val=0;
	struct Sub_LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	val =  i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);


	return val;
}




static int LM3642_chip_init(struct Sub_LM3642_chip_data *chip)
{


	return 0;
}

static int LM3642_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct Sub_LM3642_chip_data *chip;
	struct Sub_LM3642_platform_data *pdata = client->dev.platform_data;

	int err = -1;

	PK_DBG("LM3642_probe start--->.\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		printk(KERN_ERR  "LM3642 i2c functionality check fail.\n");
		return err;
	}

	chip = kzalloc(sizeof(struct Sub_LM3642_chip_data), GFP_KERNEL);
	chip->client = client;

	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	if(pdata == NULL){ //values are set to Zero.
		PK_ERR("LM3642 Platform data does not exist\n");
		pdata = kzalloc(sizeof(struct Sub_LM3642_platform_data),GFP_KERNEL);
		chip->pdata  = pdata;
		chip->no_pdata = 1;
	}

	chip->pdata  = pdata;
	if(LM3642_chip_init(chip)<0)
		goto err_chip_init;

	LM3642_i2c_client = client;
	PK_DBG("LM3642 Initializing is done \n");

	return 0;

err_chip_init:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	PK_ERR("LM3642 probe is failed \n");
	return -ENODEV;
}

static int LM3642_remove(struct i2c_client *client)
{
	struct Sub_LM3642_chip_data *chip = i2c_get_clientdata(client);

    if(chip->no_pdata)
		kfree(chip->pdata);
	kfree(chip);
	return 0;
}


#define LM3642_NAME "leds-LM3642"
static const struct i2c_device_id LM3642_id[] = {
	{LM3642_NAME, 0},
	{}
};

static struct i2c_driver LM3642_i2c_driver = {
	.driver = {
		.name  = LM3642_NAME,
	},
	.probe	= LM3642_probe,
	.remove   = LM3642_remove,
	.id_table = LM3642_id,
};

struct Sub_LM3642_platform_data LM3642_pdata = {0, 0, 0, 0, 0};
static struct i2c_board_info __initdata i2c_LM3642={ I2C_BOARD_INFO(LM3642_NAME, I2C_STROBE_MAIN_SLAVE_7_BIT_ADDR), \
													.platform_data = &LM3642_pdata,};

static int __init LM3642_init(void)
{
	printk("LM3642_init\n");
	//i2c_register_board_info(2, &i2c_LM3642, 1);
	i2c_register_board_info(I2C_STROBE_MAIN_CHANNEL, &i2c_LM3642, 1);


	return i2c_add_driver(&LM3642_i2c_driver);
}

static void __exit LM3642_exit(void)
{
	i2c_del_driver(&LM3642_i2c_driver);
}


module_init(LM3642_init);
module_exit(LM3642_exit);

MODULE_DESCRIPTION("Flash driver for LM3642");
MODULE_AUTHOR("pw <pengwei@mediatek.com>");
MODULE_LICENSE("GPL v2");

int sub_readReg(int reg)
{

    int val;
    val = LM3642_read_reg(LM3642_i2c_client, reg);
    return (int)val;
}
#endif

int Sub_FL_Enable(void)
{
#ifdef LEDSSTROBE_LM3642_ENABLE
	char buf[2];
//	char bufR[2];
    if(g_duty<0)
        g_duty=0;
    else if(g_duty>16)
        g_duty=16;
  if(g_duty<=2)
  {
        int val;
        if(g_bLtVersion==1)
        {
            if(g_duty==0)
                val=3;
            else if(g_duty==1)
                val=5;
            else //if(g_duty==2)
                val=7;
        }
        else
        {
            if(g_duty==0)
                val=1;
            else if(g_duty==1)
                val=2;
            else //if(g_duty==2)
                val=3;
        }
        buf[0]=9;
        buf[1]=val<<4;
        //iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
        LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

        buf[0]=10;
        buf[1]=0x02;
        //iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
        LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);
  }
  else
  {
    int val;
    val = (g_duty-1);
    buf[0]=9;
	  buf[1]=val;
	  //iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
	  LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

	  buf[0]=10;
	  buf[1]=0x03;
	  //iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
	  LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);
  }
	PK_DBG(" Sub_FL_Enable line=%d\n",__LINE__);

    sub_readReg(0);
	sub_readReg(1);
	sub_readReg(6);
	sub_readReg(8);
	sub_readReg(9);
	sub_readReg(0xa);
	sub_readReg(0xb);
#else
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
    if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
    if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	PK_DBG("[constant_flashlight] set gpio %d %s \n",GPIO_CAMERA_FLASH_EN,GPIO_OUT_ONE?"HIGH":"LOW");

     #ifdef GPIO_CAMERA_FLASH_FRONT_EN
     if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_FRONT_EN, GPIO_CAMERA_FLASH_FRONT_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
     if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
     if(mt_set_gpio_out(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}	
     #endif
#endif
    return 0;
}



int Sub_FL_Disable(void)
{
#ifdef LEDSSTROBE_LM3642_ENABLE
		char buf[2];

///////////////////////
	buf[0]=10;
	buf[1]=0x00;
	//iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);
	PK_DBG(" Sub_FL_Disable line=%d\n",__LINE__);
#else
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
    if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
    if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	PK_DBG("[constant_flashlight] set gpio %d %s \n",GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO?"HIGH":"LOW");

     #ifdef GPIO_CAMERA_FLASH_FRONT_EN
     if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_FRONT_EN, GPIO_CAMERA_FLASH_FRONT_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
     if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
     if(mt_set_gpio_out(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}	
     #endif
#endif
    return 0;
}

int Sub_FL_dim_duty(unsigned long duty)
{
#ifdef LEDSSTROBE_LM3642_ENABLE
	PK_DBG(" Sub_FL_dim_duty line=%d\n",__LINE__);
	g_duty = duty;
#else
	int i;
	switch (duty){
	case 0://in torch mode
		PK_DBG("set torch mode\n");
        #ifdef GPIO_CAMERA_FLASH_LEVEL_PIN
		if(mt_set_gpio_mode(GPIO_FLASH_LEVEL, GPIO_FLASH_LEVEL_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_FLASH_LEVEL,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_FLASH_LEVEL,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
        #endif
		/*set torch mode*/
		if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		
		break;
	case 1://in pre flash & af flash mode
		PK_DBG("set pre flash & af flash mdoe\n");
		for(i=0;i<PRE_LIGHT_LEVEL;i++)
		{
			if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN,GPIO_CAMERA_FLASH_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
			if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		}
		PK_DBG("[constant_flashlight] set gpio %d %s \n",GPIO_CAMERA_FLASH_EN,GPIO_OUT_ONE?"HIGH":"LOW");
	
		#ifdef GPIO_CAMERA_FLASH_LEVEL_PIN
		if(mt_set_gpio_mode(GPIO_FLASH_LEVEL, GPIO_FLASH_LEVEL_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_FLASH_LEVEL,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_FLASH_LEVEL,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
        #endif
					 
		if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		break;
	case 2://in main flash mode
		PK_DBG("set main flash mode\n");
        #ifdef GPIO_CAMERA_FLASH_LEVEL_PIN
		if(mt_set_gpio_mode(GPIO_FLASH_LEVEL, GPIO_FLASH_LEVEL_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_FLASH_LEVEL,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_FLASH_LEVEL,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
        #endif
					 
		if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		break;
	default:
		//PK_DBG("error duty=%d value !!!%d\n",(int)duty);
		break;
	}
#endif
    return 0;
}




int Sub_FL_Init(void)
{
#ifdef LEDSSTROBE_LM3642_ENABLE
   int regVal0;
  char buf[2];

  buf[0]=0xa;
	buf[1]=0x0;
	//iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

	buf[0]=0x8;
	buf[1]=0x47;
	//iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);

	buf[0]=9;
	buf[1]=0x35;
	//iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
	LM3642_write_reg(LM3642_i2c_client, buf[0], buf[1]);




	//static int LM3642_read_reg(struct i2c_client *client, u8 reg)
	//regVal0 = readReg(0);
	regVal0 = LM3642_read_reg(LM3642_i2c_client, 0);

	if(regVal0==1)
	    g_bLtVersion=1;
	else
	    g_bLtVersion=0;


    PK_DBG(" FL_Init regVal0=%d isLtVer=%d\n",regVal0, g_bLtVersion);


/*
	if(mt_set_gpio_mode(FLASH_GPIO_ENT,GPIO_MODE_00)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
    if(mt_set_gpio_dir(FLASH_GPIO_ENT,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
    if(mt_set_gpio_out(FLASH_GPIO_ENT,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}

    	if(mt_set_gpio_mode(FLASH_GPIO_ENF,GPIO_MODE_00)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
    if(mt_set_gpio_dir(FLASH_GPIO_ENF,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
    if(mt_set_gpio_out(FLASH_GPIO_ENF,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
    */


#else
	PK_DBG("start\n");
    /*set torch mode*/
    if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
    if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
    if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
    /*Init. to disable*/
    if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN,GPIO_CAMERA_FLASH_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
    if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
    if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
  	//disable front flashlight en pin
 /* 	#ifdef GPIO_CAMERA_FLASH_FRONT_EN
     if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_FRONT_EN, GPIO_CAMERA_FLASH_FRONT_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
     if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
     if(mt_set_gpio_out(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}	
	 #endif*/
	INIT_WORK(&workTimeOut, work_timeOutFunc);
	PK_DBG("done\n");
#endif
    PK_DBG(" FL_Init line=%d\n",__LINE__);
    return 0;
}


int Sub_FL_Uninit(void)
{
	Sub_FL_Disable();
    return 0;
}

/*****************************************************************************
User interface
*****************************************************************************/

static void work_timeOutFunc(struct work_struct *data)
{
    Sub_FL_Disable();
    PK_DBG("sub_ledTimeOut_callback\n");
    //printk(KERN_ALERT "work handler function./n");
}



enum hrtimer_restart sub_ledTimeOutCallback(struct hrtimer *timer)
{
    schedule_work(&workTimeOut);
    return HRTIMER_NORESTART;
}
static struct hrtimer g_timeOutTimer;
void Sub_timerInit(void)
{
  INIT_WORK(&workTimeOut, work_timeOutFunc);
	g_timeOutTimeMs=1000; //1s
	hrtimer_init( &g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	g_timeOutTimer.function=sub_ledTimeOutCallback;

}

#ifndef LEDSSTROBE_LM3642_ENABLE
static int set_flashlight_state(unsigned long state)
{

    if(state==1){
    /*Enable*/
     PK_DBG("in flash light test mode so open so enable back and front flash light at same time \n");
	#ifdef GPIO_CAMERA_FLASH_BACK_EN
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_BACK_EN, GPIO_CAMERA_FLASH_BACK_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
	if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_BACK_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
	if(mt_set_gpio_out(GPIO_CAMERA_FLASH_BACK_EN,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	#endif

	#ifdef GPIO_CAMERA_FLASH_FRONT_EN
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_FRONT_EN, GPIO_CAMERA_FLASH_FRONT_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
	if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
	if(mt_set_gpio_out(GPIO_CAMERA_FLASH_FRONT_EN,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	#endif
#if 0
    switch(state){
	case 1:
		if(mt_set_gpio_mode(GPIO_FLASH_LEVEL, GPIO_FLASH_LEVEL_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
             if(mt_set_gpio_dir(GPIO_FLASH_LEVEL,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
             if(mt_set_gpio_out(GPIO_FLASH_LEVEL,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}

		    /*set torch mode*/
		if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		break;
	case 2:
			if(mt_set_gpio_mode(GPIO_FLASH_LEVEL, GPIO_FLASH_LEVEL_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
                   if(mt_set_gpio_dir(GPIO_FLASH_LEVEL,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
                   if(mt_set_gpio_out(GPIO_FLASH_LEVEL,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		 
                   if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
                   if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
                   if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		break;
	case 3:
		 if(mt_set_gpio_mode(GPIO_FLASH_LEVEL, GPIO_FLASH_LEVEL_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
              if(mt_set_gpio_dir(GPIO_FLASH_LEVEL,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
              if(mt_set_gpio_out(GPIO_FLASH_LEVEL,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
			 
              if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
              if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
              if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
		break;
	default :
    		PK_DBG(" No such command \n");	
	}
#endif
    		return 0;
	}
	else{
		PK_DBG("There must be something wrong !!!\n");
	}
}
#endif
static int sub_strobe_ioctl(unsigned int cmd, unsigned long arg)
{
	int i4RetValue = 0;
	int ior_shift;
	int iow_shift;
	int iowr_shift;
	ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC,0, int));
	iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC,0, int));
	iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC,0, int));
	PK_DBG("LM3642 constant_flashlight_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%d\n",__LINE__, ior_shift, iow_shift, iowr_shift,(int)arg);
    switch(cmd)
    {
#ifndef LEDSSTROBE_LM3642_ENABLE
		case FLASHLIGHTIOC_T_STATE:
			PK_DBG("FLASHLIGHTIOC_T_STATE: %d\n",(int)arg);
			set_flashlight_state(arg);
		break;
#endif

		case FLASH_IOC_SET_TIME_OUT_TIME_MS:
			PK_DBG("FLASH_IOC_SET_TIME_OUT_TIME_MS: %d\n",(int)arg);
			g_timeOutTimeMs=arg;
		break;


    	case FLASH_IOC_SET_DUTY :
    		PK_DBG("FLASHLIGHT_DUTY: %d\n",(int)arg);
    		Sub_FL_dim_duty(arg);
    		break;


    	case FLASH_IOC_SET_STEP:
    		PK_DBG("FLASH_IOC_SET_STEP: %d\n",(int)arg);

    		break;

    	case FLASH_IOC_SET_ONOFF :
    		PK_DBG("FLASHLIGHT_ONOFF: %d\n",(int)arg);
    		if(arg==1)
    		{

    		    int s;
    		    int ms;
    		    if(g_timeOutTimeMs>1000)
            	{
            		s = g_timeOutTimeMs/1000;
            		ms = g_timeOutTimeMs - s*1000;
            	}
            	else
            	{
            		s = 0;
            		ms = g_timeOutTimeMs;
            	}

				if(g_timeOutTimeMs!=0)
	            {
	            	ktime_t ktime;
					ktime = ktime_set( s, ms*1000000 );
					hrtimer_start( &g_timeOutTimer, ktime, HRTIMER_MODE_REL );
	            }
    			Sub_FL_Enable();
    		}
    		else
    		{
    			Sub_FL_Disable();
				hrtimer_cancel( &g_timeOutTimer );
    		}
    		break;
		default :
    		PK_DBG(" No such command \n");
    		i4RetValue = -EPERM;
    		break;
    }
    return i4RetValue;
}




static int sub_strobe_open(void *pArg)
{
    int i4RetValue = 0;
    PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

	if (0 == strobe_Res)
	{
	    Sub_FL_Init();
		Sub_timerInit();
	}
	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);
	spin_lock_irq(&g_strobeSMPLock);


    if(strobe_Res)
    {
        PK_ERR(" busy!\n");
        i4RetValue = -EBUSY;
    }
    else
    {
        strobe_Res += 1;
    }


    spin_unlock_irq(&g_strobeSMPLock);
    PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

    return i4RetValue;

}


static int sub_strobe_release(void *pArg)
{
    PK_DBG(" constant_flashlight_release\n");

    if (strobe_Res)
    {
        spin_lock_irq(&g_strobeSMPLock);

        strobe_Res = 0;
        strobe_Timeus = 0;

        /* LED On Status */
        g_strobe_On = FALSE;

        spin_unlock_irq(&g_strobeSMPLock);

    	Sub_FL_Uninit();
    }

    PK_DBG(" Done\n");

    return 0;

}


FLASHLIGHT_FUNCTION_STRUCT	subStrobeFunc=
{
	sub_strobe_open,
	sub_strobe_release,
	sub_strobe_ioctl
};


MUINT32 subStrobeInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
    if (pfFunc != NULL)
    {
        *pfFunc = &subStrobeFunc;
    }
    return 0;
}





