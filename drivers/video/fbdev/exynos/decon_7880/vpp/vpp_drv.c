/* linux/drivers/video/exynos/decon/vpp/vpp_drv.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS5 SoC series VPP driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/exynos_iovmm.h>
#include <linux/smc.h>
#include <linux/export.h>
#include <linux/videodev2_exynos_media.h>

#include "../../../../../soc/samsung/pwrcal/pwrcal.h"
#include "../../../../../soc/samsung/pwrcal/S5E7880/S5E7880-vclk.h"
#include "vpp.h"
#include "vpp_common.h"
#include "../decon_helper.h"

/*
 * Gscaler constraints
 * This is base of without rotation.
 */

#define check_align(width, height, align_w, align_h)\
	(IS_ALIGNED(width, align_w) && IS_ALIGNED(height, align_h))
#define is_err_irq(irq) ((irq == VG_IRQ_DEADLOCK_STATUS) ||\
			(irq == VG_IRQ_READ_SLAVE_ERROR))

#define MIF_LV1			(2912000/2)
#define INT_LV7			(400000)
#define FRAMEDONE_TIMEOUT	msecs_to_jiffies(30)

#define MEM_FAULT_VPP_MASTER            0
#define MEM_FAULT_VPP_CFW               1
#define MEM_FAULT_PROT_EXCEPT_0         2
#define MEM_FAULT_PROT_EXCEPT_1         3
#define MEM_FAULT_PROT_EXCEPT_2         4
#define MEM_FAULT_PROT_EXCEPT_3         5

struct vpp_dev *vpp0_for_decon;
EXPORT_SYMBOL(vpp0_for_decon);
struct vpp_dev *vpp1_for_decon;
EXPORT_SYMBOL(vpp1_for_decon);
struct vpp_dev *vpp2_for_decon;
EXPORT_SYMBOL(vpp2_for_decon);
struct vpp_dev *vpp3_for_decon;
EXPORT_SYMBOL(vpp3_for_decon);

/* VPP CFW offsets for G0, G1, G2 and VG0.
 * G2 doesn't support secure window. So assign error no -99.
 */
int vpp_cfw_id[] = {0, 1, -99, 2};

static void vpp_dump_cfw_register(void)
{
	u32 smc_val;
	/* FIXME */
	return;
	smc_val = exynos_smc(0x810000DE, MEM_FAULT_VPP_MASTER, 0, 0);
	vpp_err("=== vpp_master:0x%x\n", smc_val);
	smc_val = exynos_smc(0x810000DE, MEM_FAULT_VPP_CFW, 0, 0);
	vpp_err("=== vpp_cfw:0x%x\n", smc_val);
	smc_val = exynos_smc(0x810000DE, MEM_FAULT_PROT_EXCEPT_0, 0, 0);
	vpp_err("=== vpp_except_0:0x%x\n", smc_val);
	smc_val = exynos_smc(0x810000DE, MEM_FAULT_PROT_EXCEPT_1, 0, 0);
	vpp_err("=== vpp_except_1:0x%x\n", smc_val);
	smc_val = exynos_smc(0x810000DE, MEM_FAULT_PROT_EXCEPT_2, 0, 0);
	vpp_err("=== vpp_except_2:0x%x\n", smc_val);
	smc_val = exynos_smc(0x810000DE, MEM_FAULT_PROT_EXCEPT_3, 0, 0);
	vpp_err("=== vpp_except_3:0x%x\n", smc_val);
}

static void vpp_dump_registers(struct vpp_dev *vpp)
{
	vpp_dump_cfw_register();
	dev_info(DEV, "=== VPP%d SFR DUMP ===\n", vpp->id);
	dev_info(DEV, "start count : %d, done count : %d\n",
			vpp->start_count, vpp->done_count);

	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			vpp->regs, 0x90, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			vpp->regs + 0xA00, 0x8, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			vpp->regs + 0xA48, 0x10, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			vpp->regs + 0xB00, 0x100, false);
}

void vpp_op_timer_handler(unsigned long arg)
{
	struct vpp_dev *vpp = (struct vpp_dev *)arg;

	vpp_dump_registers(vpp);

	dev_info(DEV, "VPP[%d] irq hasn't been occured", vpp->id);
}

static int vpp_wait_for_framedone(struct vpp_dev *vpp)
{
	int done_cnt;
	int ret;

	if (test_bit(VPP_POWER_ON, &vpp->state)) {
		done_cnt = vpp->done_count;
		dev_dbg(DEV, "%s (%d, %d)\n", __func__,
				done_cnt, vpp->done_count);
		ret = wait_event_interruptible_timeout(vpp->framedone_wq,
				(done_cnt != vpp->done_count),
				FRAMEDONE_TIMEOUT);
		if (ret == 0) {
			dev_dbg(DEV, "timeout of frame done(st:%d, %d, do:%d)\n",
				vpp->start_count, done_cnt, vpp->done_count);
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static int vpp_check_size(struct vpp_dev *vpp, struct vpp_img_format *vi)
{
	struct decon_win_config *config = vpp->config;
	struct decon_frame *src = &config->src;
	struct decon_frame *dst = &config->dst;
	struct vpp_size_constraints vc;

	vpp_constraints_params(&vc, vi);

	if ((!check_align(src->x, src->y, vc.src_mul_x, vc.src_mul_y)) ||
	   (!check_align(src->f_w, src->f_h, vc.src_mul_w, vc.src_mul_h)) ||
	   (!check_align(src->w, src->h, vc.img_mul_w, vc.img_mul_h)) ||
	   (!check_align(dst->w, dst->h, vc.sca_mul_w, vc.sca_mul_h))) {
		dev_err(DEV, "Alignment error\n");
		goto err;
	}

	if (src->w > vc.src_w_max || src->w < vc.src_w_min ||
		src->h > vc.src_h_max || src->h < vc.src_h_min) {
		dev_err(DEV, "Unsupported source size\n");
		goto err;
	}

	if (dst->w > vc.sca_w_max || dst->w < vc.sca_w_min ||
		dst->h > vc.sca_h_max || dst->h < vc.sca_h_min) {
		dev_err(DEV, "Unsupported dest size\n");
		goto err;
	}

	return 0;
err:
	dev_err(DEV, "offset x : %d, offset y: %d\n", src->x, src->y);
	dev_err(DEV, "src_mul_x : %d, src_mul_y : %d\n", vc.src_mul_x, vc.src_mul_y);
	dev_err(DEV, "src f_w : %d, src f_h: %d\n", src->f_w, src->f_h);
	dev_err(DEV, "src_mul_w : %d, src_mul_h : %d\n", vc.src_mul_w, vc.src_mul_h);
	dev_err(DEV, "src w : %d, src h: %d\n", src->w, src->h);
	dev_err(DEV, "img_mul_w : %d, img_mul_h : %d\n", vc.img_mul_w, vc.img_mul_h);
	dev_err(DEV, "dst w : %d, dst h: %d\n", dst->w, dst->h);
	dev_err(DEV, "sca_mul_w : %d, sca_mul_h : %d\n", vc.sca_mul_w, vc.sca_mul_h);
	dev_err(DEV, "rotation : %d, color_format : %d\n",
				config->vpp_parm.rot, config->format);

	return -EINVAL;
}

static int vpp_init(struct vpp_dev *vpp)
{
	int ret = 0;
	if (vpp->id == 0) {
		ret = exynos_smc(MC_FC_SET_CFW_PROT,
				MC_FC_DRM_SET_CFW_PROT, PROT_G0, 0);
		if (ret != 2) {
			vpp_err("smc call fail for vpp0: %d %d\n", ret,__LINE__);
			return ret;
		}
	}

	if (vpp->id == 1) {
		ret = exynos_smc(MC_FC_SET_CFW_PROT,
				MC_FC_DRM_SET_CFW_PROT, PROT_G1, 0);
		if (ret != 2) {
			vpp_err("smc call fail for vpp1: %d %d)\n", ret,__LINE__);
			return ret;
		}
	}

	if (vpp->id == 3) {
		ret = exynos_smc(MC_FC_SET_CFW_PROT,
				MC_FC_DRM_SET_CFW_PROT, PROT_VG0, 0);
		if (ret != 2) {
			vpp_err("smc call fail for vpp3: %d %d)\n", ret,__LINE__);
			return ret;
		}
	}

	vpp_reg_init(vpp->id);
	vpp->h_ratio = vpp->v_ratio = 0;

	vpp->start_count = 0;
	vpp->done_count = 0;

	set_bit(VPP_POWER_ON, &vpp->state);

	return 0;
}

static int vpp_deinit(struct vpp_dev *vpp, bool do_sw_reset)
{
	clear_bit(VPP_POWER_ON, &vpp->state);

	vpp_reg_deinit(vpp->id, do_sw_reset);

	return 0;
}

static bool vpp_check_block_mode(struct vpp_dev *vpp)
{
	struct decon_win_config *config = vpp->config;
	u32 b_w = config->block_area.w;
	u32 b_h = config->block_area.h;

	if (config->vpp_parm.rot != VPP_ROT_NORMAL)
		return false;
	if (is_scaling(vpp))
		return false;
	if (!is_rgb(config))
		return false;
	if (b_w < BLK_WIDTH_MIN || b_h < BLK_HEIGHT_MIN)
		return false;

	return true;
}

void vpp_split_single_plane(struct decon_win_config *config, struct vpp_size_param *p)
{
	switch(config->format) {
	case DECON_PIXEL_FORMAT_NV12N:
		p->addr1 = NV12N_CBCR_BASE(p->addr0, p->src_fw, p->src_fh);
		break;
	default:
		break;
	}
}

void vpp_set_cfw(struct vpp_dev *vpp, struct decon_win_config *config)
{
	int ret;
	int i, plane;

	plane = decon_get_plane_cnt(config->format);
	for (i = 0; i < plane; i++) {
		ret = exynos_smc(SMC_DRM_SECBUF_CFW_PROT, vpp->phys_addr->phy_addr[i],
				vpp->phys_addr->phy_addr_len[i], vpp_cfw_id[vpp->id] + VPP_CFW_OFFSET);
		if (ret) {
			vpp_err("failed to secbuf cfw protection(%d) vpp(%d) addr[0]\n", ret, vpp->id);
			vpp_info("VPP:0 CFW_PROT. addr:%#lx, size:%d. ip:%d\n", vpp->phys_addr->phy_addr[i],
					vpp->phys_addr->phy_addr_len[i], vpp_cfw_id[vpp->id] + VPP_CFW_OFFSET);
		}
	}
}

void vpp_set_deadlock_time(struct vpp_dev *vpp, int msec)
{
	int deadlock_num;
	int disp;

	disp = cal_dfs_get_rate(dvfs_disp);
	deadlock_num = msec * disp;
	vpp_reg_set_deadlock_num(vpp->id, deadlock_num);
}

static int vpp_set_config(struct vpp_dev *vpp)
{
	struct decon_win_config *config = vpp->config;
	struct vpp_size_param p;
	struct vpp_img_format vi;
	int ret = -EINVAL;
	unsigned long flags;

	if (test_bit(VPP_STOPPING, &vpp->state)) {
		dev_warn(DEV, "vpp is ongoing stop(%d)\n", vpp->id);
		return 0;
	}

	if (!test_bit(VPP_RUNNING, &vpp->state)) {
		dev_dbg(DEV, "vpp start(%d)\n", vpp->id);
		ret = pm_runtime_get_sync(DEV);
		if (ret < 0) {
			dev_err(DEV, "Failed runtime_get(), %d\n", ret);
			return ret;
		}
		spin_lock_irqsave(&vpp->slock, flags);
		ret = vpp_init(vpp);
		if (ret < 0) {
			dev_err(DEV, "Failed to initiailze clk\n");
			spin_unlock_irqrestore(&vpp->slock, flags);
			pm_runtime_put_sync(DEV);
			return ret;
		}
		/* The state need to be set here to handle err case */
		set_bit(VPP_RUNNING, &vpp->state);
		spin_unlock_irqrestore(&vpp->slock, flags);
		enable_irq(vpp->irq);
	}

	vpp_reg_wait_pingpong_clear(vpp->id);

	vpp_set_deadlock_time(vpp, 20);

	vpp_to_scale_params(vpp, &p);
	vpp->h_ratio = p.vpp_h_ratio;
	vpp->v_ratio = p.vpp_v_ratio;

	vpp_select_format(vpp, &vi);
	if (vpp->id == 3)
		vpp_reg_set_rgb_type(vpp->id, config->vpp_parm.eq_mode);

	ret = vpp_reg_set_in_format(vpp->id, &vi);
	if (ret)
		goto err;

	ret = vpp_check_size(vpp, &vi);
	if (ret)
		goto err;

	vpp_reg_set_in_size(vpp->id, &p);

	config->src.w = p.src_w;
	config->src.h = p.src_h;

	vpp_split_single_plane(config, &p);
	if (config->protection)
		vpp_set_cfw(vpp, config);
	vpp_reg_set_in_buf_addr(vpp->id, &p, &vi);

	if (vpp_check_block_mode(vpp))
		vpp_reg_set_in_block_size(vpp->id, true, &p);
	else
		vpp_reg_set_in_block_size(vpp->id, false, &p);

	vpp->op_timer.expires = (jiffies + 1 * HZ);
	mod_timer(&vpp->op_timer, vpp->op_timer.expires);

	vpp->start_count++;

	DISP_SS_EVENT_LOG(DISP_EVT_VPP_WINCON, vpp->sd, ktime_set(0, 0));
	return 0;
err:
	dev_err(DEV, "failed to set config\n");
	return ret;
}

static int vpp_clk_enable(struct vpp_dev *vpp)
{
	int ret;

	ret = clk_enable(vpp->res.gate);
	if (ret) {
		dev_err(DEV, "Failed res.gate clk enable\n");
		return ret;
	}

	return ret;
}

static void vpp_clk_disable(struct vpp_dev *vpp)
{
	clk_disable(vpp->res.gate);
}

static int vpp_tui_protection(struct v4l2_subdev *sd, int enable)
{
	struct vpp_dev *vpp = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (test_bit(VPP_POWER_ON, &vpp->state)) {
		dev_err(DEV, "VPP is not ready for TUI (%ld)\n", vpp->state);
		return -EBUSY;
	}

	if (enable) {
		ret = vpp_clk_enable(vpp);
		disable_irq(vpp->irq);
	} else {
		vpp_clk_disable(vpp);
		enable_irq(vpp->irq);
	}
	return ret;
}

static long vpp_subdev_ioctl(struct v4l2_subdev *sd,
				unsigned int cmd, void *arg)
{
	struct vpp_dev *vpp = v4l2_get_subdevdata(sd);
	int ret = 0;
	unsigned long flags;
	unsigned long state = (unsigned long)arg;
	bool need_reset;
	BUG_ON(!vpp);

	mutex_lock(&vpp->mlock);
	switch (cmd) {
	case VPP_WIN_CONFIG:
		vpp->config = (struct decon_win_config *)arg;
		vpp->protection = vpp->config->protection;
		ret = vpp_set_config(vpp);
		if (ret)
			dev_err(DEV, "Failed vpp-%d configuration\n",
					vpp->id);
		break;

	case VPP_STOP:
		if (!test_bit(VPP_RUNNING, &vpp->state)) {
			dev_warn(DEV, "vpp-%d is already stopped\n",
					vpp->id);
			goto err;
		}
		set_bit(VPP_STOPPING, &vpp->state);
		if (state != VPP_STOP_ERR) {
			ret = vpp_reg_wait_op_status(vpp->id);
			if (ret < 0) {
				dev_err(DEV, "%s : vpp-%d is working\n",
						__func__, vpp->id);
				goto err;
			}
		}
		need_reset = (state > 0) ? true : false;
		DISP_SS_EVENT_LOG(DISP_EVT_VPP_STOP, vpp->sd, ktime_set(0, 0));
		clear_bit(VPP_RUNNING, &vpp->state);
		disable_irq(vpp->irq);
		spin_lock_irqsave(&vpp->slock, flags);
		del_timer(&vpp->op_timer);
		vpp_deinit(vpp, need_reset);
		spin_unlock_irqrestore(&vpp->slock, flags);
#if defined (CONFIG_EXYNOS8890_BTS_OPTIMIZATION_TYPE1)
		call_bts_ops(vpp, bts_set_zero_bw, vpp);
#endif

		pm_runtime_put_sync(DEV);
		dev_dbg(DEV, "vpp stop(%d)\n", vpp->id);
		clear_bit(VPP_STOPPING, &vpp->state);
		break;

	case VPP_TUI_PROTECT:
		ret = vpp_tui_protection(sd, state);
		break;

	case VPP_GET_BTS_VAL:
		vpp->config = (struct decon_win_config *)arg;
#if defined (CONFIG_EXYNOS8890_BTS_OPTIMIZATION_TYPE1)
		call_bts_ops(vpp, bts_get_bw, vpp);
#endif
		break;

	case VPP_SET_BW:
		vpp->config = (struct decon_win_config *)arg;
#if defined (CONFIG_EXYNOS8890_BTS_OPTIMIZATION_TYPE1)
		call_bts_ops(vpp, bts_set_calc_bw, vpp);
#endif
		break;

	case VPP_SET_ROT_MIF:
		vpp->config = (struct decon_win_config *)arg;
#if defined (CONFIG_EXYNOS8890_BTS_OPTIMIZATION_TYPE1)
		call_bts_ops(vpp, bts_set_rot_mif, vpp);
#endif
		break;

	case VPP_DUMP:
		vpp_dump_registers(vpp);
		break;

	case VPP_WAIT_IDLE:
		if (test_bit(VPP_RUNNING, &vpp->state))
			vpp_reg_wait_idle(vpp->id);
		break;

	case VPP_WAIT_FOR_FRAMEDONE:
		ret = vpp_wait_for_framedone(vpp);
		break;

	case VPP_CFW_CONFIG:
		vpp->phys_addr = (struct vpp_phys_addr *)arg;
		break;

	default:
		break;
	}

err:
	mutex_unlock(&vpp->mlock);

	return ret;
}

static int vpp_sd_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct vpp_dev *vpp = v4l2_get_subdevdata(sd);

	dev_dbg(DEV, "vpp%d is opened\n", vpp->id);

	return 0;
}

static int vpp_sd_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct vpp_dev *vpp = v4l2_get_subdevdata(sd);

	dev_dbg(DEV, "vpp%d is closed\n", vpp->id);

	return 0;
}

static int vpp_link_setup(struct media_entity *entity,
			const struct media_pad *local,
			const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations vpp_media_ops = {
	.link_setup = vpp_link_setup,
};

static const struct v4l2_subdev_internal_ops vpp_internal_ops = {
	.open = vpp_sd_open,
	.close = vpp_sd_close,
};

static const struct v4l2_subdev_core_ops vpp_subdev_core_ops = {
	.ioctl = vpp_subdev_ioctl,
};

static struct v4l2_subdev_ops vpp_subdev_ops = {
	.core = &vpp_subdev_core_ops,
};

static int vpp_find_media_device(struct vpp_dev *vpp)
{
	struct exynos_md *md;

	md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);
	if (!md) {
		decon_err("failed to get output media device\n");
		return -ENODEV;
	}
	vpp->mdev = md;

	return 0;
}

static int vpp_create_subdev(struct vpp_dev *vpp)
{
	struct v4l2_subdev *sd;
	int ret;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	v4l2_subdev_init(sd, &vpp_subdev_ops);

	vpp->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s.%d", "vpp-sd", vpp->id);
	sd->grp_id = vpp->id;
	ret = media_entity_init(&sd->entity, VPP_PADS_NUM,
				&vpp->pad, 0);
	if (ret) {
		dev_err(DEV, "Failed to initialize VPP media entity");
		goto error;
	}

	sd->entity.ops = &vpp_media_ops;
	sd->internal_ops = &vpp_internal_ops;

	ret = v4l2_device_register_subdev(&vpp->mdev->v4l2_dev, sd);
	if (ret) {
		media_entity_cleanup(&sd->entity);
		goto error;
	}

	vpp->mdev->vpp_sd[vpp->id] = sd;
	vpp->mdev->vpp_dev[vpp->id] = &vpp->pdev->dev;
	dev_info(DEV, "vpp_sd[%d] = %08lx\n", vpp->id,
			(ulong)vpp->mdev->vpp_sd[vpp->id]);

	vpp->sd = sd;
	v4l2_set_subdevdata(sd, vpp);

	return 0;
error:
	kfree(sd);
	return ret;
}

static int vpp_resume(struct device *dev)
{
	return 0;
}

static int vpp_suspend(struct device *dev)
{
	return 0;
}

static irqreturn_t vpp_irq_handler(int irq, void *priv)
{
	struct vpp_dev *vpp = priv;
	int vpp_irq = 0;

	DISP_SS_EVENT_START();
	spin_lock(&vpp->slock);
	if (test_bit(VPP_POWER_ON, &vpp->state)) {
		vpp_irq = vpp_reg_get_irq_status(vpp->id);
		vpp_reg_set_clear_irq(vpp->id, vpp_irq);

		if (is_err_irq(vpp_irq)) {
			dev_err(DEV, "Error interrupt (0x%x)\n", vpp_irq);
			vpp_dump_registers(vpp);
			exynos_sysmmu_show_status(&vpp->pdev->dev);
			goto err;
		}
	}

	if (vpp_irq & VG_IRQ_FRAMEDONE) {
		vpp->done_count++;
		wake_up_interruptible_all(&vpp->framedone_wq);
		DISP_SS_EVENT_LOG(DISP_EVT_VPP_FRAMEDONE, vpp->sd, start);
	}

	dev_dbg(DEV, "irq status : 0x%x\n", vpp_irq);
err:
	del_timer(&vpp->op_timer);
	spin_unlock(&vpp->slock);

	return IRQ_HANDLED;
}

int vpp_sysmmu_fault_handler(struct iommu_domain *domain,
	struct device *dev, unsigned long iova, int flags, void *token)
{
	struct vpp_dev *vpp = dev_get_drvdata(dev);

	if (test_bit(VPP_POWER_ON, &vpp->state)) {
		dev_info(DEV, "vpp%d sysmmu fault handler\n", vpp->id);
		vpp_dump_registers(vpp);
	}

	return 0;
}

static void vpp_config_id(struct vpp_dev *vpp)
{
	switch (vpp->id) {
	case 0:
		vpp0_for_decon = vpp;
		break;
	case 1:
		vpp1_for_decon = vpp;
		break;
	case 2:
		vpp2_for_decon = vpp;
		break;
	case 3:
		vpp3_for_decon = vpp;
		break;
	default:
		dev_err(DEV, "Failed to find vpp id(%d)\n", vpp->id);
	}
}

static int vpp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpp_dev *vpp;
	struct resource *res;
	int irq;
	int vpp_irq = 0;
	int ret = 0;

	vpp = devm_kzalloc(dev, sizeof(*vpp), GFP_KERNEL);
	if (!vpp) {
		dev_err(dev, "Failed to allocate local vpp mem\n");
		return -ENOMEM;
	}

	vpp->id = of_alias_get_id(dev->of_node, "vpp");

	pr_info("###%s:VPP%d probe : start\n", __func__, vpp->id);
	of_property_read_u32(dev->of_node, "#ar-id-num", &vpp->pbuf_num);

	vpp->pdev = pdev;

	vpp_config_id(vpp);

	if (vpp->id == 0) {
		ret = exynos_smc(MC_FC_SET_CFW_PROT,
				MC_FC_DRM_SET_CFW_PROT, PROT_G0, 0);
		if (ret != 2) {
			vpp_err("smc call fail for vpp0: %d\n", ret);
			return ret;
		}
	}

	if (vpp->id == 1) {
		ret = exynos_smc(MC_FC_SET_CFW_PROT,
				MC_FC_DRM_SET_CFW_PROT, PROT_G1, 0);
		if (ret != 2) {
			vpp_err("smc call fail for vpp1: %d)\n", ret);
			return ret;
		}
	}

	if (vpp->id == 3) {
		ret = exynos_smc(MC_FC_SET_CFW_PROT,
				MC_FC_DRM_SET_CFW_PROT, PROT_VG0, 0);
		if (ret != 2) {
			vpp_err("smc call fail for vpp3: %d)\n", ret);
			return ret;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vpp->regs = devm_ioremap_resource(dev, res);
	if (!vpp->regs) {
		dev_err(DEV, "Failed to map registers\n");
		ret = -EADDRNOTAVAIL;
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(DEV, "Failed to get IRQ resource\n");
		return irq;
	}

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(DEV);
	if (ret < 0) {
		dev_err(DEV, "Failed runtime_get(), %d\n", ret);
		return ret;
	}

	vpp_irq = vpp_reg_get_irq_status(vpp->id);
	vpp_reg_set_clear_irq(vpp->id, vpp_irq);

	ret = vpp_reg_wait_op_status(vpp->id);
	if (ret < 0) {
		dev_err(dev, "%s : vpp-%d is working\n",
				__func__, vpp->id);
		return ret;
	}

	pm_runtime_put_sync(DEV);

	spin_lock_init(&vpp->slock);

	ret = devm_request_irq(dev, irq, vpp_irq_handler,
				0, pdev->name, vpp);
	if (ret) {
		dev_err(DEV, "Failed to install irq\n");
		return ret;
	}

	vpp->irq = irq;
	disable_irq(vpp->irq);

	ret = vpp_find_media_device(vpp);
	if (ret) {
		dev_err(DEV, "Failed find media device\n");
		return ret;
	}

	ret = vpp_create_subdev(vpp);
	if (ret) {
		dev_err(DEV, "Failed create sub-device\n");
		return ret;
	}

	init_waitqueue_head(&vpp->stop_queue);
	init_waitqueue_head(&vpp->framedone_wq);

	platform_set_drvdata(pdev, vpp);

	ret = iovmm_activate(dev);
	if (ret < 0) {
		dev_err(DEV, "failed to reactivate vmm\n");
		return ret;
	}

	setup_timer(&vpp->op_timer, vpp_op_timer_handler,
			(unsigned long)vpp);

#if defined (CONFIG_EXYNOS8890_BTS_OPTIMIZATION_TYPE1)
	vpp->bts_ops = &decon_bts_control;
	pm_qos_add_request(&vpp->vpp_mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
#endif

	mutex_init(&vpp->mlock);

	iovmm_set_fault_handler(dev, vpp_sysmmu_fault_handler, NULL);

	dev_info(DEV, "VPP%d is probed successfully\n", vpp->id);

	return 0;
}

static int vpp_remove(struct platform_device *pdev)
{
	struct vpp_dev *vpp =
		(struct vpp_dev *)platform_get_drvdata(pdev);

	iovmm_deactivate(&vpp->pdev->dev);

#if defined (CONFIG_EXYNOS8890_BTS_OPTIMIZATION_TYPE1)
	pm_qos_remove_request(&vpp->vpp_mif_qos);
#endif

	dev_info(DEV, "%s driver unloaded\n", pdev->name);

	return 0;
}

static const struct of_device_id vpp_device_table[] = {
	{
		.compatible = "samsung,exynos7-vpp",
	},
	{},
};

static const struct dev_pm_ops vpp_pm_ops = {
	.suspend		= vpp_suspend,
	.resume			= vpp_resume,
};

static struct platform_driver vpp_driver __refdata = {
	.probe		= vpp_probe,
	.remove		= vpp_remove,
	.driver = {
		.name	= "exynos-vpp",
		.owner	= THIS_MODULE,
		.pm	= &vpp_pm_ops,
		.of_match_table = of_match_ptr(vpp_device_table),
		.suppress_bind_attrs = true,
	}
};

static int vpp_register(void)
{
	return platform_driver_register(&vpp_driver);
}

device_initcall_sync(vpp_register);

MODULE_AUTHOR("Sungchun, Kang <sungchun.kang@samsung.com>");
MODULE_DESCRIPTION("Samsung EXYNOS Soc VPP driver");
MODULE_LICENSE("GPL");
