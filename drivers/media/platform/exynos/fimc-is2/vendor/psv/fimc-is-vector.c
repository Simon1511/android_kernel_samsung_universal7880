#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/exynos_ion.h>

#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/zlib.h>

#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>

#include "fimc-is-config.h"
#include "fimc-is-binary.h"
#include "fimc-is-mem.h"
#include "fimc-is-vector.h"
#include "fimc-is-vender-specific.h"

static unsigned int get_newline_offset(const char *src, size_t size)
{
	size_t offset = 1;

	do {
		offset++;
		if ((*src == 0x0d) && (*(src + 1) == 0x0a))
			return offset;
		src++;
	} while (offset <= size);

	return 0;
}

static ulong make_chksum_64(void *kva, size_t size)
{
	ulong result = 0;
	ulong *data = (ulong *)kva;
	int i;

	for (i = 0; i < (size >> 3); i++)
		result += data[i];

	return result;
}

static struct fimc_is_priv_buf *fimc_is_vector_pbuf_alloc(
	struct fimc_is_vender *vender, size_t size)
{
	struct fimc_is_core *core;
	struct fimc_is_resourcemgr *rscmgr;
	struct fimc_is_vender_specific *priv;
	struct fimc_is_priv_buf *pbuf;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	core = container_of(vender, struct fimc_is_core, vender);
	rscmgr = &core->resourcemgr;

	pbuf = CALL_PTR_MEMOP(&rscmgr->mem, alloc, priv->alloc_ctx, size, 0);
	if (IS_ERR(pbuf)) {
		err_vec("failed to allocate buffer for size: 0x%zx", size);
		return ERR_PTR(-ENOMEM);
	}

	return pbuf;
}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0
static int strip_gzip_hdr(void *data, size_t size)
{
	char *hdr = data;
	int hdrsize = 10;
	int flags;

	if (size <= hdrsize)
		return -EINVAL;

	/* Check for gzip magic number */
	if ((hdr[0] == 0x1f) && (hdr[1] == 0x8b)) {
		flags = hdr[3];

		if (hdr[2] != Z_DEFLATED || (flags & RESERVED) != 0) {
			err_vec("bad gzipped DMA file");
			return -EINVAL;
		}

		if ((flags & EXTRA_FIELD) != 0)
			hdrsize = 12 + hdr[10] + (hdr[11] << 8);
		if ((flags & ORIG_NAME) != 0)
			while (hdr[hdrsize++] != 0)
				;
		if ((flags & COMMENT) != 0)
			while (hdr[hdrsize++] != 0)
				;
		if ((flags & HEAD_CRC) != 0)
			hdrsize += 2;

		if (hdrsize >= size) {
			err_vec("ran out of data in header");
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	dbg_vec("stripped gzip header size: %d\n", hdrsize);

	return hdrsize;
}

/*
 * [B]Allocate a private buffer for DMA.
 * [B] Load a file into the buffer.
 *    [I] If checksum for input is necessary, make it.
 *    [O] Make a expected checksum with golden output.
 *	  [O] Write 0s to output buffer.
 * [B] Clean cache for the DMA buffer to use it in device space
 * [B] Add the entry to the list.
 */
static int __add_dma_cfg_entry(struct fimc_is_vender *vender,
	struct vector_dma *dma)
{
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	int ret, hdr_size, decomp_size;
	struct fimc_is_binary bin;
	struct fimc_is_binary compressed_bin;
	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	dma->pbuf = fimc_is_vector_pbuf_alloc(vender, dma->size);
	if (IS_ERR_OR_NULL(dma->pbuf)) {
		err_vec("failed to alloc DMA buffer for %pR size: 0x%08x",
				dma, dma->size);
		return ret;
	}

	bin.data = (void *)CALL_BUFOP(dma->pbuf, kvaddr, dma->pbuf);
	bin.size = dma->size;

	if (cfg->item.compressed) {
		ret = request_binary(&compressed_bin, FIMC_IS_ISP_LIB_SDCARD_PATH,
								dma->filename, NULL);
		if (!ret) {
			hdr_size = strip_gzip_hdr(compressed_bin.data,
										compressed_bin.size);
			/* minimum header size is 10 bytes */
			if (hdr_size > 0) {
				decomp_size = zlib_inflate_blob(bin.data, bin.size,
							(void *)((char *)compressed_bin.data + hdr_size),
							compressed_bin.size - hdr_size);

				if (decomp_size != dma->size) {
					err_vec("failed to decompressed 0x%08x:0x%08x",
							dma->size, decomp_size);
					ret = -EINVAL;
				}
			}
		}
	} else {
		ret = get_filesystem_binary(dma->filename, &bin);
	}

	if (ret) {
		if (dma->dir == DMA_DIR_INPUT) {
			err_vec("failed to copy file to DMA[I] buffer for %pR\n"
					"\t\t\t\tkvaddr: %p size: 0x%zx filename: %s",
					dma, bin.data, bin.size, dma->filename);

			goto err_get_file;
		} else if (dma->dir == DMA_DIR_OUTPUT) {
			if (dma->chksum_expect == 0)
			/*
			 * If we are here, it failed to load DMA output file,
			 * and we already failed to get a expected
			 * checksum value from the DMA configuration file.
			 * So, we cannot use the checksum verifiaction.
			 */
				err_vec("failed to get a expected checksum value\n"
						"\t\t\t\tofs: 0x%08x, size: 0x%zx filename: %s",
						dma->sfr_ofs, bin.size, dma->filename);
		}
	} else {
		dma->chksum_expect = 0;
	}

#ifdef INPUT_CHKSUM
	if (dma->dir == DMA_DIR_INPUT) {
		dma->chksum_input = make_chksum_64(bin.data, bin.size);
	} else if (dma->dir == DMA_DIR_OUTPUT) {
#else
	if (dma->dir == DMA_DIR_OUTPUT) {
#endif
		/* A DMA output file was loaded successfully */
		if (dma->chksum_expect == 0)
			dma->chksum_expect = make_chksum_64(bin.data, bin.size);
		memset(bin.data, 0x00, dma->size);
	}

	CALL_VOID_BUFOP(dma->pbuf, sync_for_device, dma->pbuf, 0,
					dma->size, DMA_TO_DEVICE);

	list_add_tail(&dma->list, &cfg->dma);

	return 0;

err_get_file:
	CALL_VOID_BUFOP(dma->pbuf, free, dma->pbuf);

	return ret;
}

/*
 * Remove all DMA entries from the head.
 * Free the DMA private buffer.
 * Free a DMA entry itself.
 */
static void __flush_dma_cfg_entries(struct vector_cfg *cfg)
{
	struct vector_dma *dma;

	while (!list_empty(&cfg->dma)) {
		dma = list_entry((&cfg->dma)->next, struct vector_dma, list);
		dbg_vec("flushing DMA[%c] ofs: 0x%08x, size: 0x%08x\n",
					dma->dir ? 'I' : 'O', dma->sfr_ofs, dma->size);
		CALL_VOID_BUFOP(dma->pbuf, free, dma->pbuf);
		list_del(&dma->list);
		kfree(dma);
 	}
}

#define is_valid_line(cols, entry)		\
	((cols == NUM_OF_COL_DMA_CFG) &&	\
	 ((entry->dir == DMA_DIR_INPUT) ||	\
	  (entry->dir == DMA_DIR_OUTPUT)))
static int fimc_is_vector_dma_load(struct fimc_is_vender *vender, int id)
{
	int ret;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	char *dma_cfg_filename;
	struct fimc_is_binary dma_cfg_bin;
	char *dma_cfg_data;
	size_t cur_pos = 0;
	int cols;
	unsigned int ofs;
	struct vector_dma *entry = NULL;
	char filename[SIZE_OF_NAME];

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	dma_cfg_filename = __getname();
	if (unlikely(!dma_cfg_filename))
		return -ENOMEM;

	snprintf(dma_cfg_filename, PATH_MAX, "%s%d/%s_%d", "vector", id, "dma",
				cfg->framecnt);
	ret = request_binary(&dma_cfg_bin, FIMC_IS_ISP_LIB_SDCARD_PATH,
							dma_cfg_filename, NULL);
	if (ret) {
		err_vec("failed to load vector DMA configuration (%d): name: %s",
				ret, dma_cfg_filename);
		goto err_req_dma_cfg_bin;
	} else {
		info_vec("vector DMA info - size: 0x%zx name: %s\n",
				dma_cfg_bin.size, dma_cfg_filename);
	}

	dma_cfg_data = (char *)dma_cfg_bin.data;
	while (cur_pos < dma_cfg_bin.size) {
		if (!entry) {
			entry = kzalloc(sizeof(struct vector_dma), GFP_KERNEL);
			if (!entry) {
				err_vec("failed to allocate DMA entry");
				__flush_dma_cfg_entries(cfg);
				err_vec("flushed all DMA entries");
				ret = -EINVAL;
				goto err_alloc_dma_entry;
			}
		}

		cols = sscanf(dma_cfg_data, "%x %x %d %127s\n", &entry->sfr_ofs,
					&entry->size, &entry->dir, filename);

		if (is_valid_line(cols, entry)) {
			dbg_vec("DMA[%c] ofs: 0x%08x, size: 0x%08x, filename: %s\n",
					entry->dir ? 'I' : 'O', entry->sfr_ofs,
					entry->size, filename);

					/* in case of using a given checksum value */
					if (entry->dir == DMA_DIR_OUTPUT)
						sscanf(filename, "%lx", &entry->chksum_expect);

					if (cfg->item.compressed)
						/* added only a patial path to use request_binary */
						snprintf(entry->filename, SIZE_OF_NAME, "%s%d/%s",
								"vector", id, filename);
					else
						/* added a full path to entry's filename */
						snprintf(entry->filename, SIZE_OF_NAME, "%s%s%d/%s",
								FIMC_IS_ISP_LIB_SDCARD_PATH, "vector", id,
								filename);

			if (__add_dma_cfg_entry(vender, entry)) {
				err_vec("failed to add DMA entry");
				__flush_dma_cfg_entries(cfg);
				err_vec("flushed all DMA entries");
				ret = -EINVAL;
				goto err_add_dma_entry;
			}
			entry = NULL;
		}

		ofs = get_newline_offset(dma_cfg_data, dma_cfg_bin.size - cur_pos);
		if (ofs) {
			cur_pos += ofs;
			dma_cfg_data += ofs;
		} else {
			break;
		}
	}

err_add_dma_entry:
	if (entry)
		kfree(entry);

err_alloc_dma_entry:
	release_binary(&dma_cfg_bin);

err_req_dma_cfg_bin:
	__putname(dma_cfg_filename);

	return ret;
}

static void fimc_is_vector_dma_set(struct fimc_is_vender *vender, int id)
{
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	struct vector_dma *dma;
	dma_addr_t dva;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	info_vec("start DMA address setting\n");

	list_for_each_entry(dma, &cfg->dma, list) {
		dva = CALL_BUFOP(dma->pbuf, dvaddr, dma->pbuf);
		vector_writel((u32)dva, cfg->baseaddr, dma->sfr_ofs);

		dbg_vec("DMA ofs: 0x%08x, size: 0x%08x, dva: %pa\n",
				dma->sfr_ofs, dma->size, &dva);
	}
}

static int fimc_is_vector_dma_dump(struct fimc_is_vender *vender, int id)
{
	int ret = 0;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	struct vector_dma *dma;
	char *filename;
	struct fimc_is_binary bin;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	filename = __getname();

	if (unlikely(!filename))
		return -ENOMEM;

	list_for_each_entry(dma, &cfg->dma, list) {
		/* 0: none, 1: output, 2: input */
		if (cfg->item.dump_dma & (1 << dma->dir)) {
			snprintf(filename, PATH_MAX, "%s%s%d/dump/%08x_%08x_%d.raw",
						FIMC_IS_ISP_LIB_SDCARD_PATH,"vector",
						id, dma->sfr_ofs, dma->size, cfg->framecnt);

			dbg_vec("dumping DMA[%c] ofs: 0x%08x, size: 0x%08x to %s\n",
				dma->dir ? 'I' : 'O', dma->sfr_ofs, dma->size, filename);

			bin.data = (void *)CALL_BUFOP(dma->pbuf, kvaddr, dma->pbuf);
			bin.size = dma->size;

			ret = put_filesystem_binary(filename, &bin, O_SYNC | O_TRUNC | O_CREAT | O_WRONLY);
			if (ret) {
				err_vec("failed to dump DMA to %s (%d)", filename, ret);
				break;
			}
		}
	}

	__putname(filename);

	return ret;
}

/*
 * Add the entry to the list.
 */
static int __add_crc_cfg_entry(struct fimc_is_vender *vender,
	struct vector_crc *crc)
{
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	list_add_tail(&crc->list, &cfg->crc);

   return 0;
}

/*
 * Remove all DMA entries from the head.
 * Free a DMA entry itself.
 */
static void __flush_crc_cfg_entries(struct vector_cfg *cfg)
{
	struct vector_crc *crc;

	while (!list_empty(&cfg->crc)) {
		crc = list_entry((&cfg->crc)->next, struct vector_crc, list);
		dbg_vec("flusing CRC addr: 0x%08x, value: 0x%08x, mask: 0x%08x\n",
					crc->sfr_addr, crc->value, crc->sfr_mask);
		list_del(&crc->list);
		kfree(crc);
	}
}

static int fimc_is_vector_crc_load(struct fimc_is_vender *vender, int id)
{
	int ret;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	char *crc_cfg_filename;
	struct fimc_is_binary crc_cfg_bin;
	char *crc_cfg_data;
	size_t cur_pos = 0;
	int cols;
	unsigned int ofs;
	struct vector_crc *entry = NULL;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	crc_cfg_filename = __getname();
	if (unlikely(!crc_cfg_filename))
		return -ENOMEM;

	snprintf(crc_cfg_filename, PATH_MAX, "%s%d/%s_%d", "vector", id, "crc",
				cfg->framecnt);
	ret = request_binary(&crc_cfg_bin, FIMC_IS_ISP_LIB_SDCARD_PATH,
							crc_cfg_filename, NULL);
	if (ret) {
		err_vec("failed to load vector CRC configuration (%d): name: %s",
				ret, crc_cfg_filename);
		goto err_req_crc_cfg_bin;
	} else {
		info_vec("vector CRC info - size: 0x%zx name: %s\n",
				crc_cfg_bin.size, crc_cfg_filename);
	}

	crc_cfg_data = (char *)crc_cfg_bin.data;
	while (cur_pos < crc_cfg_bin.size) {
		if (!entry) {
			entry = kzalloc(sizeof(struct vector_crc), GFP_KERNEL);
			if (!entry) {
				err_vec("failed to allocate CRC entry");
				__flush_crc_cfg_entries(cfg);
				err_vec("flushed all CRC entries");
				ret = -EINVAL;
				goto err_alloc_crc_entry;
			}
		}

		cols = sscanf(crc_cfg_data, "%x %x %x\n", &entry->sfr_addr,
					&entry->value, &entry->sfr_mask);

		if (cols == NUM_OF_COL_CRC_CFG) {
			dbg_vec("CRC addr: 0x%08x, value: 0x%08x, mask: 0x%08x\n",
					entry->sfr_addr, entry->value, entry->sfr_mask);

			if (__add_crc_cfg_entry(vender, entry)) {
				err_vec("failed to add CRC entry");
				__flush_crc_cfg_entries(cfg);
				err_vec("flushed all CRC entries");
				ret = -EINVAL;
				goto err_add_crc_entry;
			}
			entry = NULL;
		}

		ofs = get_newline_offset(crc_cfg_data, crc_cfg_bin.size - cur_pos);
		if (ofs) {
			cur_pos += ofs;
			crc_cfg_data += ofs;
		} else {
			break;
		}
	}

err_add_crc_entry:
	if (entry)
		kfree(entry);

err_alloc_crc_entry:
	release_binary(&crc_cfg_bin);

err_req_crc_cfg_bin:
	__putname(crc_cfg_filename);

	return ret;
}

static int fimc_is_vector_cfg_load(struct fimc_is_vender *vender, int id)
{
	int ret;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *config;
	char *config_filename;
	struct fimc_is_binary config_bin;
	char *config_data;
	size_t cur_pos = 0;
	int cols;
	unsigned int ofs;
	int i;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	config = &priv->vector_cfg;

	config_filename = __getname();
	if (unlikely(!config_filename))
		return -ENOMEM;

	snprintf(config_filename, PATH_MAX, "%s%d/%s", "vector", id, "config");
	ret = request_binary(&config_bin, FIMC_IS_ISP_LIB_SDCARD_PATH,
							config_filename, NULL);
	if (ret) {
		err_vec("failed to load vector configuration (%d): name: %s",
				ret, config_filename);
		goto err_req_config_bin;
	} else {
		info_vec("vector configuration info - size: 0x%zx name: %s\n",
				config_bin.size, config_filename);
	}

	config_data = (char *)config_bin.data;

	/* name */
	if (cur_pos >= config_bin.size) {
		err_vec("could not start loading configuration");
		ret = -EINVAL;
		goto err_get_config_item;
	}
	cols = sscanf(config_data, "%127s\n", config->name);
	if (cols != NUM_OF_COL_CONFIG) {
		err_vec("could not load configuration - %s", "name");
		ret = -EINVAL;
		goto err_get_config_item;
	}
	dbg_vec("Configuration name: %s\n", config->name);

	/* numeric items */
	for (i = 0; i < NUM_OF_NUMERIC_CFGS; i++) {
		ofs = get_newline_offset(config_data, config_bin.size - cur_pos);
		if (ofs) {
			cur_pos += ofs;
			config_data += ofs;
			if (cur_pos >= config_bin.size) {
				err_vec("could not go further more after - %s", cfg_name[i]);
				ret = -EINVAL;
				goto err_get_config_item;
			}

			cols = sscanf(config_data, "%d\n", &config->items[i]);
			if (cols != NUM_OF_COL_CONFIG) {
				err_vec("could not load configuration - %s",
						cfg_name[i + OFS_OF_NUMERIC_CFGS]);
				ret = -EINVAL;
				goto err_get_config_item;
			}

			dbg_vec("Configuration %s: %d\n",
					cfg_name[i + OFS_OF_NUMERIC_CFGS], config->items[i]);
		} else {
			err_vec("could not go further more after - %s", cfg_name[i]);
			ret = -EINVAL;
			goto err_get_config_item;
		}
	}

#ifdef USE_ION_DIRECTLY
	config->client = ion_client_create(ion_exynos, "fimc_is_vector");
	if (IS_ERR(client)) {
		err_vec("failed to create ion client for vector\n");
		ret = -EINVAL;
		goto err_create_ion_client;
	}
#endif

	config->baseaddr = ioremap_nocache(IO_MEM_BASE, IO_MEM_SIZE);
	if (!config->baseaddr) {
		err_vec("failed to map IO memory base");
		ret = -EINVAL;
		goto err_remap_iomem;
	}

	config->framecnt = 0;
	INIT_LIST_HEAD(&config->dma);
	INIT_LIST_HEAD(&config->crc);
	atomic_set(&config->taa0done, 0);
	atomic_set(&config->vra0done, 0);
	atomic_set(&config->isp0done, 0);

err_remap_iomem:
#ifdef USE_ION_DIRECTLY
err_create_ion_client:
#endif
err_get_config_item:
	release_binary(&config_bin);

err_req_config_bin:
	__putname(config_filename);

	return ret;
}

static void fimc_is_vector_cfg_unload(struct fimc_is_vender *vender, int id)
{
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *config;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	config = &priv->vector_cfg;

	if (config->baseaddr)
		iounmap(config->baseaddr);
}

static int fimc_is_vector_load_n_set(struct fimc_is_vender *vender,
	int id, const char* type)
{
	int ret;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	char *filename;
	struct fimc_is_binary bin;
	char *data;
	size_t cur_pos = 0;
	int cols;
	unsigned int ofs;
	unsigned int sfr_ofs, sfr_val;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	filename = __getname();
	if (unlikely(!filename))
		return -ENOMEM;

	snprintf(filename, PATH_MAX, "%s%d/%s_%d", "vector", id, type,
				cfg->framecnt);
	ret = request_binary(&bin, FIMC_IS_ISP_LIB_SDCARD_PATH, filename, NULL);
	if (ret) {
		err_vec("failed to load vector %s (%d): name: %s", type, ret,
				filename);
		goto err_req_bin;
	} else {
		info_vec("vector %s info - size: 0x%zx name: %s\n",
				type, bin.size, filename);
	}

	data = (char *)bin.data;
	while (cur_pos < bin.size) {
		cols = sscanf(data, "%x %x\n", &sfr_ofs, &sfr_val);
		if (cols == NUM_OF_COL_SFR_CFG) {
			/* to skip too many meaningless log */
			if (strcmp(type, "sfr"))
				dbg_vec("ofs: 0x%08x, val: 0x%08x\n", sfr_ofs, sfr_val);
			/*
			 * WARN: we are assuming that each SFR set represents as
			 *		 17 characters at least like below
			 *		 'XXXXXXXX XXXXXXXX';
			 */
			cur_pos += MIN_LEN_OF_SFR_CFG;
			data += MIN_LEN_OF_SFR_CFG;

			vector_writel(sfr_val, cfg->baseaddr, sfr_ofs);
		}

		ofs = get_newline_offset(data, bin.size - cur_pos);
		if (ofs) {
			cur_pos += ofs;
			data += ofs;
		} else {
			break;
		}
	}

	/* TODO: remove it */
	if (!strcmp(type, "sfr")) {
		__raw_writel(0x1, cfg->baseaddr + 0x3126c);
		__raw_writel(0x1, cfg->baseaddr + 0x3136c);
		__raw_writel(0x1, cfg->baseaddr + 0x3146c);
		__raw_writel(0x1, cfg->baseaddr + 0x3156c);
	}

	release_binary(&bin);

err_req_bin:
	__putname(filename);

	return ret;
}

static int fimc_is_vector_sysmmu_resume(struct fimc_is_vender *vender)
{
	struct fimc_is_vender_specific *priv;
#ifdef USE_IOMMU
	struct fimc_is_core *core;
	struct fimc_is_resourcemgr *rscmgr;
#else
	struct vector_cfg *cfg;
#endif

	priv = (struct fimc_is_vender_specific *)vender->private_data;

#ifdef USE_IOMMU
	core = container_of(vender, struct fimc_is_core, vender);
	rscmgr = &core->resourcemgr;

	return CALL_MEMOP(&rscmgr->mem, resume, priv->alloc_ctx);
#else
	cfg = &priv->vector_cfg;

	__raw_writel(0x00000000, cfg->baseaddr + SYSMMU_ISP0_OFS);
	__raw_writel(0x00000000, cfg->baseaddr + SYSMMU_ISP1_OFS);
	__raw_writel(0x00000000, cfg->baseaddr + SYSMMU_ISP2_OFS);
#endif

	return 0;
}

static void fimc_is_vector_sysmmu_suspend(struct fimc_is_vender *vender)
{
#ifdef USE_IOMMU
	struct fimc_is_vender_specific *priv;
	struct fimc_is_core *core;
	struct fimc_is_resourcemgr *rscmgr;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	core = container_of(vender, struct fimc_is_core, vender);
	rscmgr = &core->resourcemgr;

	CALL_VOID_MEMOP(&rscmgr->mem, suspend, priv->alloc_ctx);
#endif
}

static irqreturn_t vector_isp0_isr(int irq, void *data)
{
	struct fimc_is_vender *vender;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	unsigned int intstatus, save_val;

	vender = (struct fimc_is_vender *)data;
	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	/* interrupt masking */
		save_val = __raw_readl(cfg->baseaddr + 0x02508);
		__raw_writel(0x0, cfg->baseaddr + 0x02508);

		intstatus = __raw_readl(cfg->baseaddr + 0x02504);

	/* interrupt clear */
		__raw_writel(intstatus, cfg->baseaddr + 0x02510);


		/* frame start -> disable for one shot */

		if (((intstatus >> 1) & 0x1) == 0x1) {
			atomic_set(&cfg->isp0done, 1);
			wake_up(&cfg->wait);

	}


		__raw_writel(save_val, cfg->baseaddr + 0x02508);


	return IRQ_HANDLED;
}

static irqreturn_t vector_3aa0_isr(int irq, void *data)
{
	struct fimc_is_vender *vender;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	unsigned int intstatus, save_val;

	vender = (struct fimc_is_vender *)data;
	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;


	/* interrupt masking */
		save_val = __raw_readl(cfg->baseaddr + 0x80108);
		__raw_writel(0x0, cfg->baseaddr + 0x80108);

		intstatus = __raw_readl(cfg->baseaddr + 0x80104);

	/* interrupt clear */
		__raw_writel(intstatus, cfg->baseaddr + 0x80110);

		/* frame start -> disable for one shot */
		if (((intstatus >> 1) & 0x1) == 0x1) {
			atomic_set(&cfg->taa0done, 1);
	wake_up(&cfg->wait);

		}


		__raw_writel(save_val, cfg->baseaddr + 0x80108);

	return IRQ_HANDLED;
}


static int fimc_is_vector_request_irqs(struct fimc_is_vender *vender)
{
	int ret;
	struct fimc_is_vender_specific *priv;
	struct fimc_is_core *core;
	struct vector_cfg *cfg;
	char irq_name[SIZE_OF_NAME];

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	core = container_of(vender, struct fimc_is_core, vender);
	cfg = &priv->vector_cfg;


	/* isp */
	snprintf(irq_name, SIZE_OF_NAME, "fimc-psv-%d", PDEV_IRQ_NUM_ISP0);
	ret = request_irq(cfg->irq_isp0, vector_isp0_isr, 0, irq_name, vender);
	if (ret) {
		err_vec("failed to register isr for %d", PDEV_IRQ_NUM_ISP0);
		return ret;
	}

	/* 3aa */
	snprintf(irq_name, SIZE_OF_NAME, "fimc-psv-%d", PDEV_IRQ_NUM_3AA0);
	ret = request_irq(cfg->irq_3aa0, vector_3aa0_isr, 0, irq_name, vender);
	if (ret) {
		err_vec("failed to register isr for %d", PDEV_IRQ_NUM_3AA0);
		return ret;
	}

	return 0;
}

static void fimc_is_vector_free_irqs(struct fimc_is_vender *vender)
{
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;


	/* free_irq(cfg->irq_mcsc, vender);
	free_irq(cfg->irq_vra0, vender); */
	free_irq(cfg->irq_isp0, vender);
	free_irq(cfg->irq_3aa0, vender);
}


static int fimc_is_isp_wait_done(struct fimc_is_vender *vender)
{
	int ret = 0;
	long remain;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	remain = wait_event_timeout(cfg->wait, atomic_read(&cfg->isp0done),
						msecs_to_jiffies(cfg->item.timeout));

	if (!remain) {
		err_vec("vector execution timer is expired");
		ret = -ETIME;
	} else {
		dbg_vec("time to be done: %d\n", jiffies_to_msecs(remain));
	}

	return ret;
}

static int fimc_is_3aa_wait_done(struct fimc_is_vender *vender)
{
	int ret = 0;
	long remain;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	remain = wait_event_timeout(cfg->wait, atomic_read(&cfg->taa0done),
						msecs_to_jiffies(cfg->item.timeout));

	if (!remain) {
		err_vec("vector execution timer is expired");
		ret = -ETIME;
	} else {
		dbg_vec("time to be done: %d\n", jiffies_to_msecs(remain));
	}

	return ret;
}

static int fimcis_reset(void)
{
	void __iomem *baseaddr;
	u32 readval;

	/* csis0 */
	baseaddr = ioremap(0x14420000, 0x10000);
	__raw_writel(0x2, baseaddr + 0x4);
	while (1) {
		readval = __raw_readl(baseaddr + 4);
		if (readval == 0x00004000)
			break;
		}
	iounmap(baseaddr);
	info_vec("csis0 reset...\n");

	/* csis1 */
	baseaddr = ioremap(0x14460000, 0x10000);
	__raw_writel(0x2, baseaddr + 0x4);
	while (1) {
		readval = __raw_readl(baseaddr + 4);
		if (readval == 0x00004000)
			break;
		}
	iounmap(baseaddr);
	info_vec("csis1 reset...\n");

	/* bns */
	baseaddr = ioremap(0x14410000, 0x10000);
	__raw_writel(0x00080000, baseaddr + 0x4);
	while (1) {
		readval = __raw_readl(baseaddr + 4);
		if ((readval & (1<<18)) == (1<<18))
			break;
		}
	__raw_writel(0x00020000, baseaddr + 0x4);
	iounmap(baseaddr);
	info_vec("bns reset...\n");

	/* 3aa */
	baseaddr = ioremap(0x14480000, 0x10000);
	__raw_writel(0x00000001, baseaddr + 0xC);
	while (1) {
		readval = __raw_readl(baseaddr + 0xC);
		if (readval  == 0x00000000)
			break;
		}
	iounmap(baseaddr);
	info_vec("3aa reset...\n");

	/* isp */
	baseaddr = ioremap(0x14400000, 0x10000);
	__raw_writel(0x00000001, baseaddr + 0xC);
	while (1) {
		readval = __raw_readl(baseaddr + 0xC);
		if (readval  == 0x00000000)
			break;
		}
	iounmap(baseaddr);
	info_vec("isp reset...\n");

	/* mcsc */
	baseaddr = ioremap(0x14430000, 0x10000);
	__raw_writel(0x00000000, baseaddr + 0x0);
	__raw_writel(0x00000000, baseaddr + 0x07a4);
	__raw_writel(0xFFFFFFFF, baseaddr + 0x07ac);
	__raw_writel(0x00000001, baseaddr + 0x24);
	__raw_writel(0x00000001, baseaddr + 0x800);
	__raw_writel(0x00000001, baseaddr + 0x20);
	while (1) {
		readval = __raw_readl(baseaddr + 0x790);
		if ((readval & 1)  == 0x00000000)
			break;
		}
	iounmap(baseaddr);
	info_vec("mcsc reset...\n");

	/* vra */
	baseaddr = ioremap(0x14440000, 0x10000);
	__raw_writel(0x00000001, baseaddr + 0x3008);
	while (1) {
		readval = __raw_readl(baseaddr + 0x300C);
		if ((readval & 1)  == 0x00000001)
			break;
		}
	__raw_writel(0x00000001, baseaddr + 0x3004);

	__raw_writel(0x00000002, baseaddr + 0xb04C);
	__raw_writel(0x00000001, baseaddr + 0xb008);
	while (1) {
		readval = __raw_readl(baseaddr + 0xb00C);
		if ((readval & 1)  == 0x00000001)
			break;
		}
	__raw_writel(0x00000001, baseaddr + 0xb004);
	__raw_writel(0x00000001, baseaddr + 0xb048);

	iounmap(baseaddr);
	info_vec("vra reset...\n");

	return 0;
}



#define USE_CRC_ADDR /* ? SFR for CRC uses absolute address or offset */
#ifdef USE_CRC_ADDR
#define CRC_EXTRA_OFS	0x14000000
#else
#define CRC_EXTRA_OFS	0
#endif
static int fimc_is_vector_check_crc(struct fimc_is_vender *vender, int id)
{
	int ret = 0;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	struct vector_crc *crc;
	unsigned int result;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	list_for_each_entry(crc, &cfg->crc, list) {
		dbg_vec("checking CRC addr: 0x%08x, value: 0x%08x, mask: 0x%08x\n",
				crc->sfr_addr, crc->value, crc->sfr_mask);
		result = vector_readl(cfg->baseaddr, crc->sfr_addr - CRC_EXTRA_OFS);
		result = result & crc->sfr_mask;
		if (result != crc->value) {
			err_vec("CRC is mismatched at addr: 0x%08x\n"
					"\t\t\t\texpect: 0x%08x, result: 0x%08x",
					crc->sfr_addr, crc->value, result);
			ret = -EFAULT;
		} else {
			dbg_vec("CRC is matched at addr: 0x%08x\n"
					"\t\t\t\texpect: 0x%08x, result: 0x%08x\n",
					crc->sfr_addr, crc->value, result);
		}
	}

	return ret;
}

static int fimc_is_vector_check_chksum(struct fimc_is_vender *vender, int id)
{
	int ret = 0;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	struct vector_dma *dma;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	list_for_each_entry(dma, &cfg->dma, list) {
#ifdef INPUT_CHKSUM
		if (dma->dir == DMA_DIR_INPUT)
			dbg_vec("checksum for DMA[I] ofs: 0x%08x, size: 0x%08x, "
					" value: 0x%016lx\n",
					dma->sfr_ofs, dma->size, dma->chksum_input);
#endif
		if (dma->dir == DMA_DIR_OUTPUT) {
			dbg_vec("making checksum for DMA ofs: 0x%08x, size: 0x%08x\n",
					dma->sfr_ofs, dma->size);

			CALL_VOID_BUFOP(dma->pbuf, sync_for_cpu, dma->pbuf,
					0, dma->size, DMA_FROM_DEVICE);
			dma->chksum_result = make_chksum_64(
					(void *)CALL_BUFOP(dma->pbuf, kvaddr, dma->pbuf),
					dma->size);

			if (dma->chksum_result != dma->chksum_expect) {
				err_vec("checksum for DMA[O] ofs: 0x%08x, size: 0x%08x\n"
						"\t\t\t\texpected: 0x%016lx, result: 0x%016lx",
						dma->sfr_ofs, dma->size,
						dma->chksum_expect, dma->chksum_result);
				ret = -EFAULT;
			} else {
				dbg_vec("checksum for DMA[O] ofs: 0x%08x, size: 0x%08x\n"
						"\t\t\t\texpected: 0x%016lx, result: 0x%016lx\n",
						dma->sfr_ofs, dma->size,
						dma->chksum_expect, dma->chksum_result);
			}
		}
	}

	return ret;
}

int fimc_is_vector_set(struct fimc_is_core *core, int id)
{
	struct fimc_is_vender *vender = &core->vender;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	int ret;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	/* TODO: multiple frame */
	/* TODO: hook origianl ISRs */

	fimcis_reset();

	ret = fimc_is_vector_cfg_load(vender, id);
	if (ret) {
		err_vec("failed to load configuration for vector%d", id);
		return ret;
	}

	ret = fimc_is_vector_dma_load(vender, id);
	if (ret) {
		err_vec("failed to load DMA configuration for vector%d", id);
		return ret;
	}

	ret = fimc_is_vector_crc_load(vender, id);
	if (ret) {
		err_vec("failed to load CRC configuration for vector%d", id);
		goto err_crc_load;
	}

	ret = fimc_is_vector_load_n_set(vender, id, "sfr");
	if (ret) {
		err_vec("failed to set %s configuration %d", "sfr", id);
		goto err_load_n_set_sfr;
	}

	fimc_is_vector_dma_set(vender, id);

	ret = fimc_is_vector_sysmmu_resume(vender);
	if (ret) {
		err_vec("failed to resume SYS.MMU for vector%d", id);
		goto err_sysmmu_resume;
	}

	/*  modified to use irq always    */
		ret = fimc_is_vector_request_irqs(vender);
		if (ret) {
			err_vec("failed to request IRQs vector%d", id);
			goto err_req_irqs;
		}

	ret = fimc_is_vector_load_n_set(vender, id, "enable");
	if (ret) {
		err_vec("failed to set %s configuration for vector%d", "enable", id);
		goto err_load_n_set_enable;
	}

	if (!cfg->item.sync)
		return 0;

	ret = fimc_is_3aa_wait_done(vender);
	if (ret)
		err_vec("error is occurred while waiting 3aadone vector%d", id);

	ret = fimc_is_isp_wait_done(vender);
	if (ret)
		err_vec("error is occurred while waiting ispdone vector%d", id);
	usleep_range(100000, 100000);

	ret = fimc_is_vector_load_n_set(vender, id, "disable");
	if (ret)
		err_vec("failed to set %s configuration vector%d", "disable", id);

	/* CRC */
	if (cfg->item.verification & VERIFICATION_CRC) {
		ret = fimc_is_vector_check_crc(vender, id);
		if (ret) {
			err_vec("CRC mismatch(es) were occured vector%d", id);
			goto err_load_n_set_enable;
		}
	}

	/* checksum */
	if (cfg->item.verification & VERIFICATION_CHKSUM) {
		ret = fimc_is_vector_check_chksum(vender, id);
		if (ret) {
			err_vec("checksum mismatch(es) were occured vector%d", id);
			goto err_load_n_set_enable;
		}
	}

	if (cfg->item.dump_dma) {
		if (fimc_is_vector_dma_dump(vender, id))
			err_vec("failed to dump DMA vector%d", id);
	}

err_load_n_set_enable:
	fimc_is_vector_free_irqs(vender);

err_req_irqs:
	fimc_is_vector_sysmmu_suspend(vender);

err_sysmmu_resume:
err_load_n_set_sfr:
	__flush_crc_cfg_entries(cfg);

err_crc_load:
	__flush_dma_cfg_entries(cfg);

	fimc_is_vector_cfg_unload(vender, id);

	/* TODO: restore origianl ISRs */

	return ret;
}

int fimc_is_vector_get(struct fimc_is_core *core, int id)
{
	struct fimc_is_vender *vender = &core->vender;
	struct fimc_is_vender_specific *priv;
	struct vector_cfg *cfg;
	int ret;

	priv = (struct fimc_is_vender_specific *)vender->private_data;
	cfg = &priv->vector_cfg;

	ret = fimc_is_3aa_wait_done(vender);
	if (ret)
		err_vec("error is occurred while waiting 3aadone vector%d", id);

	ret = fimc_is_isp_wait_done(vender);
	if (ret)
		err_vec("error is occurred while waiting ispdone vector%d", id);

	ret = fimc_is_vector_load_n_set(vender, id, "disable");
	if (ret)
		err_vec("failed to set %s configuration vector%d", "disable", id);

	/* CRC */
	if (cfg->item.verification & VERIFICATION_CRC) {
		ret = fimc_is_vector_check_crc(vender, id);
		if (ret) {
			err_vec("CRC mismatch(es) were occured vector%d", id);
			goto p_err;
		}
	}

	/* checksum */
	if (cfg->item.verification & VERIFICATION_CHKSUM) {
		ret = fimc_is_vector_check_chksum(vender, id);
		if (ret) {
			err_vec("checksum mismatch(es) were occured vector%d", id);
			goto p_err;
		}
	}

	if (cfg->item.dump_dma) {
		if (fimc_is_vector_dma_dump(vender, id))
			err_vec("failed to dump DMA vector%d", id);
	}

p_err:
	fimc_is_vector_free_irqs(vender);
	fimcis_reset();

	fimc_is_vector_sysmmu_suspend(vender);

	__flush_crc_cfg_entries(cfg);

	__flush_dma_cfg_entries(cfg);

	fimc_is_vector_cfg_unload(vender, id);

	/* TODO: restore origianl ISRs */

	return ret;
}
