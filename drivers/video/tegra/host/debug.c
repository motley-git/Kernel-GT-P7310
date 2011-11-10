/*
 * drivers/video/tegra/dc/dc.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/io.h>

#include "dev.h"

static struct nvhost_master *debug_master;

enum {
	NVHOST_DBG_STATE_CMD = 0,
	NVHOST_DBG_STATE_DATA = 1,
};

static int nvhost_debug_handle_cmd(struct seq_file *s, u32 val, int *count)
{
	unsigned mask;
	unsigned subop;

	switch (val >> 28) {
	case 0x0:
		mask = val & 0x3f;
		if (mask) {
			seq_printf(s, "SETCL(class=%03x, offset=%03x, mask=%02x, [",
				   val >> 6 & 0x3ff, val >> 16 & 0xfff, mask);
			*count = hweight8(mask);
			return NVHOST_DBG_STATE_DATA;
		} else {
			seq_printf(s, "SETCL(class=%03x)\n", val >> 6 & 0x3ff);
			return NVHOST_DBG_STATE_CMD;
		}

	case 0x1:
		seq_printf(s, "INCR(offset=%03x, [", val >> 16 & 0xfff);
		*count = val & 0xffff;
		return NVHOST_DBG_STATE_DATA;

	case 0x2:
		seq_printf(s, "NONINCR(offset=%03x, [", val >> 16 & 0xfff);
		*count = val & 0xffff;
		return NVHOST_DBG_STATE_DATA;

	case 0x3:
		mask = val & 0xffff;
		seq_printf(s, "MASK(offset=%03x, mask=%03x, [",
			   val >> 16 & 0xfff, mask);
		*count = hweight16(mask);
		return NVHOST_DBG_STATE_DATA;

	case 0x4:
		seq_printf(s, "IMM(offset=%03x, data=%03x)\n",
			   val >> 16 & 0xfff, val & 0xffff);
		return NVHOST_DBG_STATE_CMD;

	case 0x5:
		seq_printf(s, "RESTART(offset=%08x)\n", val << 4);
		return NVHOST_DBG_STATE_CMD;

	case 0x6:
		seq_printf(s, "GATHER(offset=%03x, insert=%d, type=%d, count=%04x, addr=[",
			   val >> 16 & 0x3ff, val >> 15 & 0x1, val >> 15 & 0x1,
			   val & 0x3fff);
		*count = 1;
		return NVHOST_DBG_STATE_DATA;

	case 0xe:
		subop = val >> 24 & 0xf;
		if (subop == 0)
			seq_printf(s, "ACQUIRE_MLOCK(index=%d)\n", val & 0xff);
		else if (subop == 1)
			seq_printf(s, "RELEASE_MLOCK(index=%d)\n", val & 0xff);
		else
			seq_printf(s, "EXTEND_UNKNOWN(%08x)\n", val);

		return NVHOST_DBG_STATE_CMD;

	case 0xf:
		seq_printf(s, "DONE()\n");
		return NVHOST_DBG_STATE_CMD;

	default:
		return NVHOST_DBG_STATE_CMD;
	}
}

static void nvhost_debug_handle_word(struct seq_file *s, int *state, int *count,
				     unsigned long addr, int channel, u32 val)
{
	static int start_count = 0;
	static int dont_print = 0;
	switch (*state) {
	case NVHOST_DBG_STATE_CMD:
		if (addr)
			seq_printf(s, "%d: %08lx: %08x:", channel, addr, val);
		else
			seq_printf(s, "%d: %08x:", channel, val);

		*state = nvhost_debug_handle_cmd(s, val, count);
		dont_print = 0;
		start_count = *count;
		if (*state == NVHOST_DBG_STATE_DATA && *count == 0) {
			*state = NVHOST_DBG_STATE_CMD;
			seq_printf(s, "])\n");
		}
		break;

	case NVHOST_DBG_STATE_DATA:
		(*count)--;
		if (start_count - *count < 64)
			seq_printf(s, "%08x%s", val, *count > 0 ? ", " : "])\n");
		else if (!dont_print && (*count > 0)) {
			seq_printf(s, "[truncated; %d more words]\n", *count);
			dont_print = 1;
		}
		if (*count == 0)
			*state = NVHOST_DBG_STATE_CMD;
		break;
	}
}

static void nvhost_sync_reg_dump(struct seq_file *s)
{
	struct nvhost_master *m = s->private;
	int i;

	/* print HOST1X_SYNC regs 4 per line (from 0x3000 -> 0x31E0) */
	for (i = 0; i <= 0x1E0; i += 4) {
		if ((i & 0xF) == 0x0)
			seq_printf(s, "\n0x%08x : ", i);
		seq_printf(s, "%08x  ", readl(m->sync_aperture + i));
	}

	seq_printf(s, "\n\n");

	/* print HOST1X_SYNC regs 4 per line (from 0x3340 -> 0x3774) */
	for (i = 0x340; i <= 0x774; i += 4) {
		if ((i & 0xF) == 0x0)
			seq_printf(s, "\n0x%08x : ", i);
		seq_printf(s, "%08x  ", readl(m->sync_aperture + i));
	}
}

extern u32 context_save_phys;
void nvhost_cdma_find_gathers(struct nvhost_cdma *cdma, u32 dmaget, u32 (*addrs)[], u32 (*sizes)[], int count);

static int nvhost_debug_show(struct seq_file *s, void *unused)
{
	struct nvhost_master *m = s->private;
	int i;

	nvhost_module_busy(&m->mod);

	seq_printf(s, "---- mlocks ----\n");
	for (i = 0; i < NV_HOST1X_NB_MLOCKS; i++) {
		u32 owner = readl(m->sync_aperture + HOST1X_SYNC_MLOCK_OWNER_0 + i * 4);
		if (owner & 0x1)
			seq_printf(s, "%d: locked by channel %d\n", i, (owner >> 8) * 0xff);
		else if (owner & 0x2)
			seq_printf(s, "%d: locked by cpu\n", i);
		else
			seq_printf(s, "%d: unlocked\n", i);
	}
	seq_printf(s, "\n---- syncpts ----\n");
	for (i = 0; i < NV_HOST1X_SYNCPT_NB_PTS; i++) {
		u32 max = nvhost_syncpt_read_max(&m->syncpt, i);
		if (!max)
			continue;
		seq_printf(s, "id %d (%s) min %d max %d\n",
			i, nvhost_syncpt_name(i),
			nvhost_syncpt_update_min(&m->syncpt, i), max);

	}

	seq_printf(s, "\n---- channels ----\n");
	for (i = 0; i < NVHOST_NUMCHANNELS; i++) {
		void __iomem *regs = m->channels[i].aperture;
		u32 dmaput, dmaget, dmactrl;
		u32 cbstat, cbread;
		u32 fifostat;
		u32 val, base, offset;
		unsigned start, end;
		unsigned wr_ptr, rd_ptr;
		int state;
		int count;
		u32 phys_addr, size;

		dmaput = readl(regs + HOST1X_CHANNEL_DMAPUT);
		dmaget = readl(regs + HOST1X_CHANNEL_DMAGET);
		dmactrl = readl(regs + HOST1X_CHANNEL_DMACTRL);
		cbread = readl(m->aperture + HOST1X_SYNC_CBREAD(i));
		cbstat = readl(m->aperture + HOST1X_SYNC_CBSTAT(i));

		seq_printf(s, "%d-%s (%d): ", i, m->channels[i].mod.name,
			   atomic_read(&m->channels[i].mod.refcount));

		if (dmactrl != 0x0 || !m->channels[i].cdma.push_buffer.mapped) {
			seq_printf(s, "inactive\n\n");
			continue;
		}

		switch (cbstat) {
		case 0x00010008:		/* HOST_WAIT_SYNCPT */
			seq_printf(s, "waiting on syncpt %d val %d\n",
				   cbread >> 24, cbread & 0xffffff);
			break;

		case 0x00010009:		/* HOST_WAIT_SYNCPT_BASE */
			base = cbread >> 15 & 0xf;
			offset = cbread & 0xffff;

			val = readl(m->aperture + HOST1X_SYNC_SYNCPT_BASE(base)) & 0xffff;
			val += offset;

			seq_printf(s, "waiting on syncpt %d val %d (base %d, offset %d)\n",
				   cbread >> 24, val, base, offset);
			break;

		default:
			seq_printf(s, "active class %02x, offset %04x, val %08x\n",
				   cbstat >> 16, cbstat & 0xffff, cbread);
			break;
		}

		seq_printf(s, "PUT %08x, GET %08x, CTRL %08x, READ %08x, STAT %08x\n",
			dmaput, dmaget, dmactrl, cbread, cbstat);

		fifostat = readl(regs + HOST1X_CHANNEL_FIFOSTAT);
		if ((fifostat & 1 << 10) == 0 ) {

			seq_printf(s, "\n%d: fifo:\n", i);
			writel(0x0, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
			writel(1 << 31 | i << 16, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
			rd_ptr = readl(m->aperture + HOST1X_SYNC_CFPEEK_PTRS) & 0x1ff;
			wr_ptr = readl(m->aperture + HOST1X_SYNC_CFPEEK_PTRS) >> 16 & 0x1ff;

			start = readl(m->aperture + HOST1X_SYNC_CF_SETUP(i)) & 0x1ff;
			end = (readl(m->aperture + HOST1X_SYNC_CF_SETUP(i)) >> 16) & 0x1ff;

			state = NVHOST_DBG_STATE_CMD;

			do {
				writel(0x0, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
				writel(1 << 31 | i << 16 | rd_ptr, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
				val = readl(m->aperture + HOST1X_SYNC_CFPEEK_READ);

				nvhost_debug_handle_word(s, &state, &count, 0, i, val);

				if (rd_ptr == end)
					rd_ptr = start;
				else
					rd_ptr++;


			} while (rd_ptr != wr_ptr);

			if (state == NVHOST_DBG_STATE_DATA)
				seq_printf(s, ", ...])\n");
		}
		else 
                    seq_printf(s, "\n%d: fifo EMPTY\n", i);

		{
#define NUM_GATHERS 4
			int j;
			u32 phys_addrs[NUM_GATHERS] = {0}, sizes[NUM_GATHERS] = {0};
			nvhost_cdma_find_gathers(&m->channels[i].cdma, dmaget, phys_addrs, sizes, NUM_GATHERS);
			for (j = 0; j < NUM_GATHERS; j++)
			{
				phys_addr = phys_addrs[j];
				size = sizes[j];
				if (size)
				{
		                        u32 map_base = phys_addr & PAGE_MASK;
                        		u32 map_size = (size * 4 + PAGE_SIZE - 1) & PAGE_MASK;
                        		u32 map_offset = phys_addr - map_base;
                        		void *map_addr = ioremap_nocache(map_base, map_size);
		
                        		if (map_addr) {
                                		u32 ii;
                                		seq_printf(s, "\n%d: gather (-%d) at %08x (%d words)\n", i, j, phys_addr, size);
                                		if (context_save_phys && (phys_addr == context_save_phys)) {
                                        		seq_printf(s, "%d: context save regs 3d\n", i);
                                		}
                                		else {
                                        		state = NVHOST_DBG_STATE_CMD;
                                        		for (ii = 0; ii < size; ii++) {
                                                		val = readl(map_addr + map_offset + ii*sizeof(u32));
                                                		nvhost_debug_handle_word(s, &state, &count, phys_addr + ii, i, val);
                                        		}
                                        		iounmap(map_addr);
                                		}
                        		}
				}
			}
		}

		seq_printf(s, "\n");
	}

	nvhost_sync_reg_dump(s);

	nvhost_module_idle(&m->mod);
	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int nvhost_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvhost_debug_show, inode->i_private);
}

static const struct file_operations nvhost_debug_fops = {
	.open		= nvhost_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void nvhost_debug_init(struct nvhost_master *master)
{
	debug_master = master;
	debugfs_create_file("tegra_host", S_IRUGO, NULL, master, &nvhost_debug_fops);
}
#else
void nvhost_debug_init(struct nvhost_master *master)
{
	debug_master = master;
}

#endif

static char nvhost_debug_dump_buff[16 * 1024];

void nvhost_debug_dump(void)
{
	struct seq_file s;
	int i;
	char c;

	memset(&s, 0x0, sizeof(s));

	s.buf = nvhost_debug_dump_buff;
	s.size = sizeof(nvhost_debug_dump_buff);
	s.private = debug_master;

	nvhost_debug_show(&s, NULL);

	i = 0;
	while (i < s.count ) {
		if ((s.count - i) > 256) {
			c = s.buf[i + 256];
			s.buf[i + 256] = 0;
			printk("%s", s.buf + i);
			s.buf[i + 256] = c;
		} else {
			printk("%s", s.buf + i);
		}
		i += 256;
	}
}

