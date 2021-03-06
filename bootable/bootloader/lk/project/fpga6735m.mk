LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := fpga6735m

MODULES += app/mt_boot \
           dev/lcm


MTK_EMMC_SUPPORT = yes
MACH_FPGA_LED_SUPPORT = yes
ifeq ($(MACH_FPGA_LED_SUPPORT),yes)
DEFINES += MACH_FPGA_LED_SUPPORT 
endif
DEFINES += MTK_NEW_COMBO_EMMC_SUPPORT
#MTK_KERNEL_POWER_OFF_CHARGING = yes
MTK_LCM_PHYSICAL_ROTATION = 0
CUSTOM_LK_LCM="nt35590_hd720_dsi_cmd_auo"
#nt35590_hd720_dsi_cmd_auo = yes

#FASTBOOT_USE_G_ORIGINAL_PROTOCOL = yes
MTK_SECURITY_SW_SUPPORT = no
MTK_VERIFIED_BOOT_SUPPORT = yes
MTK_SEC_FASTBOOT_UNLOCK_SUPPORT = yes

DEBUG := 2
BOOT_LOGO := hd720

#DEFINES += WITH_DEBUG_DCC=1
DEFINES += WITH_DEBUG_UART=1
#DEFINES += WITH_DEBUG_FBCON=1
DEFINES += MACH_FPGA=y
DEFINES += MACH_FPGA_NO_DISPLAY=y
DEFINES += MTK_GPT_SCHEME_SUPPORT
