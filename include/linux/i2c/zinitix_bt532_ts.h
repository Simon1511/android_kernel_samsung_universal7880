/*
 *
 * Zinitix bt532 touch driver
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */


#ifndef _LINUX_BT532_TS_H
#define _LINUX_BT532_TS_H

#define TS_DRVIER_VERSION	"1.0.18_1"

#define BT532_TS_DEVICE		"bt532_ts_device"

#ifdef CONFIG_SEC_DEBUG_TSP_LOG
#define zinitix_debug_msg(fmt, args...) \
	do { \
		if (m_ts_debug_mode){ \
			printk(KERN_INFO "bt532_ts[%-18s:%5d] " fmt, \
					__func__, __LINE__, ## args); \
		}	\
		sec_debug_tsp_log(fmt, ## args); \
	} while (0);

#define zinitix_printk(fmt, args...) \
	do { \
		printk(KERN_INFO "bt532_ts[%-18s:%5d] " fmt, \
				__func__, __LINE__, ## args); \
		sec_debug_tsp_log(fmt, ## args); \
	} while (0);

#else	//CONFIG_SEC_DEBUG_TSP_LOG
#define zinitix_debug_msg(fmt, args...) \
	do { \
		if (m_ts_debug_mode){ \
			printk(KERN_INFO "bt532_ts[%-18s:%5d] " fmt, \
					__func__, __LINE__, ## args); \
		}	\
	} while (0);

#define zinitix_printk(fmt, args...) \
	do { \
		printk(KERN_INFO "bt532_ts[%-18s:%5d] " fmt, \
				__func__, __LINE__, ## args); \
	} while (0);
#define bt532_err(fmt) \
	do { \
		pr_err("bt532_ts : %s " fmt, __func__); \
	} while (0);
#endif	//CONFIG_SEC_DEBUG_TSP_LOG

struct bt532_ts_platform_data {
	u32		irq_gpio;
	u32		gpio_int;
	u32		gpio_scl;
	u32		gpio_sda;
	u32		gpio_ldo_en;
	int 		(*tsp_power)(void *data, bool on);
	u16		x_resolution;
	u16		y_resolution;
	u16		page_size;
	u8		orientation;
	bool		support_touchkey;
	bool		support_spay;
	bool		support_aod;
	bool		support_lpm_mode;
	bool		bringup;
	bool		mis_cal_check;
	u16 		pat_function;
	u16 		afe_base;	
	const char *project_name;
	void (*register_cb)(void *);

	const char *regulator_dvdd;
	const char *regulator_avdd;
	const char *regulator_tkled;
	const char *firmware_name;
	const char *chip_name;
	struct pinctrl *pinctrl;
};

extern struct class *sec_class;

void tsp_charger_infom(bool en);

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
extern void trustedui_mode_on(void);
extern void trustedui_mode_off(void);
#endif

#endif /* LINUX_BT532_TS_H */