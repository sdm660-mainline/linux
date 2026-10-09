// SPDX-License-Identifier: GPL-2.0
/*
 * CPU clock/DVFS driver for the "OSM" (Operating State Manager) hardware
 * block on Qualcomm SDM660/636/630 (Kryo 260).
 *
 * SDM660/636 physically reuse the MSM8998 "OSM v1" CPUSS silicon block
 * (see downstream arch/arm/boot/dts/qcom/msm8998-interposer-sdm660.dtsi,
 * compatible = "qcom,cpu-clock-osm-msm8998-v1"). Mainline currently has
 * NO cpufreq support for either MSM8998 or SDM660/630/660 (verified
 * against https://linux-msm.github.io/mainline-status/ - MSM8998 row,
 * "CPUfreq (DVFS)" column = "N" as of the date this was written).
 *
 * STATUS: Functional, but untested on SDM660/SDM630.
 * Tested only on xiaomi, whyred. qcom-sdm636
 *
 * Scope deliberately excludes, for a first bring-up:
 *   - ACD (Adaptive Clock Distribution / droop mitigation)
 *   - LLM (Limits Management - external thermal/battery voting engine)
 *   - the full "no-TZ" sequencer microcode load path (clk_osm_do_
 *     additional_setup() downstream) - this programs the OSM's internal
 *     FSM via a 256-word microcode blob (seq_instr[]/seq_br_instr[])
 *     and is only exercised on boards built with "qcom,osm-no-tz" in DT.
 *     Retail Xiaomi firmware initializes OSM's PLL/GFMUX/microcode in
 *     TrustZone before Linux boots, so this path is expected to be
 *     unnecessary for whyred - but that is an assumption, not something
 *     we've confirmed by reading back OSM_BASE+VERSION_REG/ENABLE_REG
 *     state at early boot. See README.md, step 1.
 *
 *	 If we remove the ACD block, the device won't boot.
 *	 The hardware includes ACD, but it isn't actively used.
 *	 If you don't add the ACD block to your board dtsi, the driver
 *	 will return -EINVAL, but it will keep working.
 *
 *     Copyright (c) 2026, kulesha evgeniy <voovdop@gmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/firmware/qcom/qcom_scm.h>

#define OSM_TABLE_SIZE			40
#define MAX_CORE_COUNT			4
#define SINGLE_CORE			1
#define CORE_COUNT_VAL(val)		(((val) & GENMASK(18, 16)) >> 16)

#define BVAL(msb, lsb, val)		(((val) << (lsb)) & GENMASK((msb), (lsb)))
#define SPM_CC_CTRL			0x1028

#define ENABLE_REG			0x1004
#define INDEX_REG			0x1150
#define FREQ_REG			0x1154
#define VOLT_REG			0x1158
#define OVERRIDE_REG			0x115C
#define SPARE_REG			0x1164
#define OSM_REG_SIZE			32

#define DCVS_PERF_STATE_DESIRED_REG	0x1F10

#define SEQ_REG(n)			(0x300 + (n) * 4)
#define MEM_ACC_SEQ_CONST(n)		(n)
#define MEM_ACC_APM_READ_MASK		0xff

#define APM_MX_MODE			0
#define APM_APC_MODE			BIT(1)
#define APM_MODE_SWITCH_MASK		((0x7 << 2) | 0x3)
#define APM_MX_MODE_VAL			0
#define APM_APC_MODE_VAL		0x3

#define PWRCL_EFUSE_SHIFT		0
#define PWRCL_EFUSE_MASK		0	/* pwrcl: single speed-bin on whyred/660 */
#define PERFCL_EFUSE_SHIFT		29
#define PERFCL_EFUSE_MASK		0x7

/* Only needed for clk_osm_do_additional_setup() - the microcode/PLL/GFMUX
 * bring-up that we confirmed (2026-08-27, real hardware register readback)
 * TZ does NOT do on whyred. */
#define PLL_MIN_LVAL			43
#define PLL_POST_DIV1			0x1F
#define PLL_POST_DIV2			0x11F
#define GPLL_SEL			0x400
#define PLL_EARLY_SEL			0x500
#define PLL_MAIN_SEL			0x300
#define RCG_UPDATE			0x3
#define RCG_UPDATE_SUCCESS		0x2
#define SEQ_MEM_ADDR			0x500
#define SEQ_CFG_BR_ADDR			0x170
#define ITM_CL0_DISABLE_CL1_ENABLED	0x2
#define ITM_CL0_ENABLED_CL1_DISABLE	0x1

#define MEM_ACC_SEQ_REG_CFG_START(n)	SEQ_REG(12 + (n))
#define MEM_ACC_SEQ_REG_VAL_START(n)	SEQ_REG(60 + (n))
#define MAX_MEM_ACC_VAL_PER_LEVEL	3
#define MAX_MEM_ACC_LEVELS		3
#define MAX_MEM_ACC_VALUES		(MAX_MEM_ACC_LEVELS * MAX_MEM_ACC_VAL_PER_LEVEL)

/*
 * FSM sequencer microcode, copied verbatim from downstream clock-osm.c
 * (seq_instr[]/seq_br_instr[]). This is the OSM v1 hardware's internal
 * control program for PLL/GFMUX state transitions - it is NOT board or
 * binning specific (same for every msm8998-generation OSM v1 part), so
 * unlike the LUT/voltage/mem-acc data elsewhere in this file, there is
 * nothing whyred-specific to re-derive here. Loaded into SEQ_MEM_ADDR /
 * SEQ_CFG_BR_ADDR by clk_osm_setup_sequencer() below, once per cluster.
 */
static const u32 seq_instr[] = {
	0xc2005000, 0x2c9e3b21, 0xc0ab2cdc, 0xc2882525, 0x359dc491,
	0x700a500b, 0x5001aefc, 0xaefd7000, 0x390938c8, 0xcb44c833,
	0xce56cd54, 0x341336e0, 0xa4baadba, 0xb480a493, 0x10004000,
	0x70005001, 0x1000500c, 0xc792c5a1, 0x501625e1, 0x3da335a2,
	0x50170006, 0x50150006, 0x1000c633, 0x1000acb3, 0xc422acb4,
	0xaefc1000, 0x700a500b, 0x70005001, 0x5010aefd, 0x5012700b,
	0xad41700c, 0x84e5adb9, 0xb3808566, 0x239b0003, 0x856484e3,
	0xb9800007, 0x2bad0003, 0xac3aa20b, 0x0003181b, 0x0003bb40,
	0xa30d239b, 0x500c181b, 0x5011500f, 0x181b3413, 0x853984b9,
	0x0003bd80, 0xa0012ba4, 0x72050803, 0x500e1000, 0x500c1000,
	0x1c011c0a, 0x3b181c06, 0x1c073b43, 0x1c061000, 0x1c073983,
	0x1c02500c, 0x10001c0a, 0x70015002, 0x81031000, 0x70025003,
	0x70035004, 0x3b441000, 0x81553985, 0x70025003, 0x50054003,
	0xa1467009, 0x0003b1c0, 0x4005238b, 0x835a1000, 0x855c84db,
	0x1000a51f, 0x84de835d, 0xa52c855c, 0x50061000, 0x39cd3a4c,
	0x3ad03a8f, 0x10004006, 0x70065007, 0xa00f2c12, 0x08034007,
	0xaefc7205, 0xaefd700d, 0xa9641000, 0x40071c1a, 0x700daefc,
	0x1000aefd, 0x70065007, 0x50101c16, 0x40075012, 0x700daefc,
	0x2411aefd, 0xa8211000, 0x0803a00f, 0x500c7005, 0x1c1591e0,
	0x500f5014, 0x10005011, 0x500c2bd4, 0x0803a00f, 0x10007205,
	0xa00fa9d1, 0x0803a821, 0xa9d07005, 0x91e0500c, 0x500f1c15,
	0x10005011, 0x1c162bce, 0x50125010, 0xa022a82a, 0x70050803,
	0x1c1591df, 0x5011500f, 0x5014500c, 0x0803a00f, 0x10007205,
	0x501391a4, 0x22172217, 0x70075008, 0xa9634008, 0x1c1a0006,
	0x70085009, 0x10004009, 0x00008ed9, 0x3e05c8dd, 0x1c033604,
	0xabaf1000, 0x856284e1, 0x0003bb80, 0x1000239f, 0x0803a037,
	0x10007205, 0x8dc61000, 0x38a71c2a, 0x1c2a8dc4, 0x100038a6,
	0x1c2a8dc5, 0x8dc73867, 0x38681c2a, 0x8c491000, 0x8d4b8cca,
	0x10001c00, 0x8ccd8c4c, 0x1c008d4e, 0x8c4f1000, 0x8d518cd0,
	0x10001c00, 0xa759a79a, 0x1000a718, 0xbf80af9b, 0x00001000,
};

static const u32 seq_br_instr[] = {
	0x248, 0x20e, 0x21c, 0xf6, 0x112,
	0x11c, 0xe4, 0xea, 0xc6, 0xd6,
	0x126, 0x108, 0x184, 0x1a8, 0x1b0,
	0x134, 0x158, 0x16e, 0x14a, 0xc2,
	0x190, 0x1d2, 0x1cc, 0x1d4, 0x1e8,
	0x0, 0x1f6, 0x32, 0x66, 0xb0,
	0xa6, 0x1fc, 0x3c, 0x44, 0x5c,
	0x60, 0x204, 0x30, 0x22a, 0x234,
	0x23e, 0x0, 0x250, 0x0, 0x0, 0x9a,
	0x20c,
};


/*
 * ---------------------------------------------------------------------
 * LUT entry layout. packs frequency/index/override/spare as
 * raw 32-bit words straight from the qcom,{pwrcl,perfcl}-speedbin*-v*
 * DT arrays (5 cells per row: freq_hz, freq_data, override_data, spare_data
 * OR core-count-encoded index - see clk_osm_get_lut() downstream for the
 * exact bit packing). We keep the same on-disk DT shape here so the
 * property arrays extracted from msm8998-interposer-sdm660.dtsi can be
 * reused as-is without hand re-encoding every row.
 * --------------------------------------------------------------------- */
struct osm_entry {
	u32 frequency;		/* Hz */
	u32 freq_data;		/* raw PLL programming word, opaque passthrough */
	u32 override_data;	/* raw override word, opaque passthrough */
	u32 spare_data;		/* raw spare word, opaque passthrough */
	u32 virtual_corner;	/* index into c->vc_to_uv[], 0-based */
};

struct clk_osm {
	struct clk_hw hw;
	struct device *dev;
	void __iomem *base;		/* OSM_BASE region */
	unsigned int cluster_num;	/* 0 = pwrcl, 1 = perfcl */

	struct osm_entry osm_table[OSM_TABLE_SIZE];
	unsigned int num_entries;

	/* One open-loop microvolt value per virtual corner, from DT
	 * (opp-microvolt on each opp-hz node - see README on why we do
	 * NOT try to derive this from a CPR/rpmh regulator corner table
	 * the way downstream does via regulator_list_corner_voltage()). */
	u32 *vc_to_uv;
	unsigned int num_vc;

	u32 apm_mode_ctl;
	u32 apm_ctrl_status;
	u32 apm_threshold_vc;
	u32 apm_crossover_vc;

	u32 apcs_mem_acc_cfg[3];
	u32 apcs_mem_acc_val[9];	/* MAX_MEM_ACC_LEVELS * MAX_MEM_ACC_VAL_PER_LEVEL */

	u32 l_val_base;
	u32 apcs_pll_user_ctl;
	u32 apcs_cfg_rcgr;
	u32 apcs_cmd_rcgr;
	u32 apcs_itm_present;

	void __iomem *acd_base;
	u32 acd_td;
	u32 acd_cr;
	u32 acd_sscr;
	u32 acd_extint0_cfg;
	u32 acd_extint1_cfg;
	u32 acd_autoxfer_ctl;

	int cur_index;
};

#define to_clk_osm(_hw) container_of(_hw, struct clk_osm, hw)

static inline void osm_write(struct clk_osm *c, u32 val, u32 offset)
{
	writel_relaxed(val, c->base + offset);
}

static inline u32 osm_read(struct clk_osm *c, u32 offset)
{
	return readl_relaxed(c->base + offset);
}


#define ACD_HW_VERSION		0x0
#define ACDCR			0x4
#define ACDTD			0x8
#define ACDSSCR			0x28
#define ACD_EXTINT_CFG		0x30
#define ACD_DCVS_SW		0x34
#define ACD_GFMUX_CFG		0x3c
#define ACD_AUTOXFER_CFG	0x80
#define ACD_AUTOXFER		0x84
#define ACD_AUTOXFER_CTL	0x88
#define ACD_AUTOXFER_STATUS	0x8c
#define ACD_WRITE_CTL		0x90
#define ACD_WRITE_STATUS	0x94

#define ACD_WRITE_CTL_UPDATE_EN	BIT(0)
#define ACD_WRITE_CTL_SELECT_SHIFT	1
#define ACD_GFMUX_CFG_SELECT		BIT(0)
#define ACD_DCVS_SW_DCVS_IN_PRGR_SET	BIT(0)
#define ACD_DCVS_SW_DCVS_IN_PRGR_CLEAR	0
#define ACD_REG_RELATIVE_ADDR(addr)		((addr) / 4)
#define ACD_REG_RELATIVE_ADDR_BITMASK(addr)	(1 << ACD_REG_RELATIVE_ADDR(addr))
#define ACD_LOCAL_XFER_TIMEOUT_US	500

static inline void clk_osm_acd_master_write_reg(struct clk_osm *c, u32 val,
						  u32 offset)
{
	writel_relaxed(val, c->acd_base + offset);
}

/* "Local write through": write to the master copy, then push it to the
 * local (active) copy of a single register and wait for that transfer
 * to complete - downstream's clk_osm_acd_master_write_through_reg(). */
static int clk_osm_acd_write_through_reg(struct clk_osm *c, u32 val,
					   u32 offset)
{
	u32 regval;
	int ret;

	clk_osm_acd_master_write_reg(c, val, offset);
	/* ensure the write above lands before the transfer-trigger below */
	readl_relaxed(c->acd_base + ACD_HW_VERSION);

	writel_relaxed(0, c->acd_base + ACD_WRITE_CTL);
	regval = ACD_REG_RELATIVE_ADDR(offset) << ACD_WRITE_CTL_SELECT_SHIFT;
	regval |= ACD_WRITE_CTL_UPDATE_EN;
	writel_relaxed(regval, c->acd_base + ACD_WRITE_CTL);

	ret = readl_relaxed_poll_timeout(c->acd_base + ACD_WRITE_STATUS, regval,
					  regval & ACD_REG_RELATIVE_ADDR_BITMASK(offset),
					  1, ACD_LOCAL_XFER_TIMEOUT_US);
	if (ret)
		dev_warn(c->dev, "%s: ACD local transfer of reg 0x%x timed out\n",
			 c->hw.init->name, offset);
	return ret;
}

/* Bulk auto-transfer of several master-copy registers at once, selected
 * by a bitmask - downstream's clk_osm_acd_auto_local_write_reg(). */
static int clk_osm_acd_auto_local_write_reg(struct clk_osm *c, u32 mask)
{
	u32 regval;
	int ret;

	writel_relaxed(mask, c->acd_base + ACD_AUTOXFER_CFG);
	writel_relaxed(0, c->acd_base + ACD_AUTOXFER);
	writel_relaxed(1, c->acd_base + ACD_AUTOXFER);

	ret = readl_relaxed_poll_timeout(c->acd_base + ACD_AUTOXFER_STATUS, regval,
					  regval & BIT(0), 1, ACD_LOCAL_XFER_TIMEOUT_US);
	if (ret)
		dev_warn(c->dev, "%s: ACD auto-transfer timed out\n",
			 c->hw.init->name);
	return ret;
}

/* Full init sequence, ported 1:1 from clk_osm_acd_init() in downstream
 * clock-osm.c. No-ops (returns 0 immediately) if c->acd_base is NULL,
 * i.e. no "*-acd" DT reg region was found for this cluster - matches
 * downstream's c->acd_init gate exactly. */
static int clk_osm_acd_init(struct clk_osm *c)
{
	u32 auto_xfer_mask = 0;
	int ret;

	if (!c->acd_base)
		return 0;

	clk_osm_acd_master_write_reg(c, c->acd_td, ACDTD);
	auto_xfer_mask |= ACD_REG_RELATIVE_ADDR_BITMASK(ACDTD);

	clk_osm_acd_master_write_reg(c, c->acd_cr, ACDCR);
	auto_xfer_mask |= ACD_REG_RELATIVE_ADDR_BITMASK(ACDCR);

	clk_osm_acd_master_write_reg(c, c->acd_sscr, ACDSSCR);
	auto_xfer_mask |= ACD_REG_RELATIVE_ADDR_BITMASK(ACDSSCR);

	clk_osm_acd_master_write_reg(c, c->acd_extint0_cfg, ACD_EXTINT_CFG);
	auto_xfer_mask |= ACD_REG_RELATIVE_ADDR_BITMASK(ACD_EXTINT_CFG);

	clk_osm_acd_master_write_reg(c, c->acd_autoxfer_ctl, ACD_AUTOXFER_CTL);

	/* ensure all the master-copy writes above land before transfer */
	readl_relaxed(c->acd_base + ACD_HW_VERSION);

	ret = clk_osm_acd_auto_local_write_reg(c, auto_xfer_mask);
	if (ret)
		return ret;

	/* Switch CPUSS clock source to ACD clock */
	ret = clk_osm_acd_write_through_reg(c, ACD_GFMUX_CFG_SELECT, ACD_GFMUX_CFG);
	if (ret)
		return ret;

	/* Pulse ACD_DCVS_SW (set, then clear) */
	ret = clk_osm_acd_write_through_reg(c, ACD_DCVS_SW_DCVS_IN_PRGR_SET, ACD_DCVS_SW);
	if (ret)
		return ret;
	ret = clk_osm_acd_write_through_reg(c, ACD_DCVS_SW_DCVS_IN_PRGR_CLEAR, ACD_DCVS_SW);
	if (ret)
		return ret;

	udelay(1);

	/* Program the final external interface config */
	ret = clk_osm_acd_write_through_reg(c, c->acd_extint1_cfg, ACD_EXTINT_CFG);
	if (ret)
		return ret;

	/* ACDCR/ACDTD/ACDSSCR/ACD_EXTINT_CFG/ACD_GFMUX_CFG must be
	 * re-copied master->local automatically on power-collapse exit. */
	auto_xfer_mask |= ACD_REG_RELATIVE_ADDR_BITMASK(ACD_GFMUX_CFG);
	clk_osm_acd_master_write_reg(c, auto_xfer_mask, ACD_AUTOXFER_CFG);

	dev_info(c->dev, "%s: ACD initialized\n", c->hw.init->name);
	return 0;
}

/* Ensure a write actually lands before anything downstream of it (a
 * frequency switch, an FSM kick) can be observed. Downstream does this
 * with a dummy read-back; keep the same pattern rather than trusting a
 * bare memory barrier to be enough across the OSM's internal AHB path. */
static inline void osm_mb(struct clk_osm *c)
{
	readl_relaxed(c->base + ENABLE_REG);
}

/* ---------------------------------------------------------------------
 * LUT programming - clk_osm_setup_hw_table() downstream, unchanged
 * register semantics. Plain iowrite, no TZ/SCM dependency.
 * --------------------------------------------------------------------- */
static void clk_osm_setup_hw_table(struct clk_osm *c)
{
	unsigned int i, off;
	u32 volt_val;

	for (i = 0; i < OSM_TABLE_SIZE; i++) {
		off = i * OSM_REG_SIZE;

		osm_write(c, i, INDEX_REG + off);

		if (i < c->num_entries) {
			struct osm_entry *e = &c->osm_table[i];
			u32 uv = (e->virtual_corner < c->num_vc) ?
				 c->vc_to_uv[e->virtual_corner] : 0;

			/* bits[21:16] = virtual corner, bits[11:0] = open
			 * loop voltage in mV (downstream: BVAL(21,16,vc) |
			 * BVAL(11,0,open_loop_mv)). */
			volt_val = ((e->virtual_corner & 0x3f) << 16) |
				   ((uv / 1000) & 0xfff);

			osm_write(c, e->freq_data, FREQ_REG + off);
			osm_write(c, volt_val, VOLT_REG + off);
			osm_write(c, e->override_data, OVERRIDE_REG + off);
			osm_write(c, e->spare_data, SPARE_REG + off);
		} else {
			osm_write(c, 0, FREQ_REG + off);
			osm_write(c, 0, VOLT_REG + off);
			osm_write(c, 0, OVERRIDE_REG + off);
			osm_write(c, 0, SPARE_REG + off);
		}
	}

	osm_mb(c);
}

/* ---------------------------------------------------------------------
 * APM control-register wiring - clk_osm_program_apm_regs() downstream.
 * Plain iowrite in the downstream source too (no scm_io_write calls in
 * that function), so ported verbatim.
 * --------------------------------------------------------------------- */
static void clk_osm_program_apm_regs(struct clk_osm *c)
{
	osm_write(c, c->apm_mode_ctl, SEQ_REG(2));
	osm_write(c, c->apm_ctrl_status, SEQ_REG(3));
	osm_write(c, APM_MX_MODE, SEQ_REG(77));
	osm_write(c, APM_MX_MODE_VAL, SEQ_REG(78));
	osm_write(c, APM_MODE_SWITCH_MASK, SEQ_REG(79));
	osm_write(c, APM_APC_MODE, SEQ_REG(80));
	osm_write(c, APM_APC_MODE_VAL, SEQ_REG(81));
}

/* ---------------------------------------------------------------------
 * APM voltage-crossover corner programming - clk_osm_apm_vc_setup(),
 * secure_init==true branch (i.e. board where TZ has NOT already brought
 * up OSM - confirmed to be whyred's case by reading ENABLE_REG/FREQ_REG
 * back as zero on real hardware, 2026-08-23). This branch is plain
 * MMIO in the downstream source, no SCM involved - unlike an earlier
 * draft of this file, which wrongly assumed the opposite (TZ-present)
 * branch and used qcom_scm_io_writel() here. Left the SCM path further
 * down as a fallback in case that assumption ever needs revisiting on
 * a different board/firmware.
 * --------------------------------------------------------------------- */
static void clk_osm_apm_vc_setup(struct clk_osm *c)
{
	osm_write(c, c->apm_threshold_vc, SEQ_REG(1));
	osm_write(c, c->apm_crossover_vc, SEQ_REG(72));
	/* SEQ_REG(8) downstream stores this cluster's own OSM base address
	 * plus SEQ_REG(1)'s offset - an internal self-reference the FSM
	 * microcode reads back at runtime, not a value we chose. */
	osm_write(c, SEQ_REG(1), SEQ_REG(8));
	osm_write(c, c->apm_threshold_vc, SEQ_REG(15));
	/* apm_threshold_pre_vc: downstream derives this from an optional
	 * mem-acc-threshold-voltage crossover; we don't model that corner
	 * (see mem_acc TODO), so fall back to apm_threshold_vc itself -
	 * matches downstream's own fallback when no mem-acc crossover is
	 * configured. */
	osm_write(c, c->apm_threshold_vc, SEQ_REG(31));
	osm_write(c, 0x3b | (c->apm_threshold_vc << 6), SEQ_REG(73));
	osm_write(c, 0x39 | (c->apm_threshold_vc << 6), SEQ_REG(76));
	osm_mb(c);
}

/* Fallback for a board that turns out to need the TZ-present path after
 * all (secure_init==false downstream) - NOT currently called, kept for
 * reference/completeness. Needs the physical base address, unlike the
 * function above. */
static int __maybe_unused clk_osm_apm_vc_setup_scm(struct clk_osm *c, phys_addr_t base_phys)
{
	int ret;

	ret = qcom_scm_io_writel(base_phys + SEQ_REG(72), c->apm_crossover_vc);
	if (ret)
		return ret;
	ret = qcom_scm_io_writel(base_phys + SEQ_REG(15), c->apm_threshold_vc);
	if (ret)
		return ret;
	ret = qcom_scm_io_writel(base_phys + SEQ_REG(1), c->apm_threshold_vc);
	if (ret)
		return ret;
	ret = qcom_scm_io_writel(base_phys + SEQ_REG(73),
				  0x3b | (c->apm_threshold_vc << 6));
	if (ret)
		return ret;

	return 0;
}

/* ---------------------------------------------------------------------
 * MEM-ACC trim programming - clk_osm_program_mem_acc_regs(),
 * secure_init==true branch. mem_acc_level_map[] is NOT DT data - it's
 * derived here from the LUT's own spare_data column (downstream does
 * the same derivation inline in this function, not in DT parsing).
 * --------------------------------------------------------------------- */
static void clk_osm_program_mem_acc_regs(struct clk_osm *c)
{
	u32 mem_acc_level_map[MAX_MEM_ACC_LEVELS] = { 0, 0, 0 };
	unsigned int i, j = 0;
	u32 curr_level;

	if (!c->num_entries)
		return;

	curr_level = c->osm_table[0].spare_data;
	for (i = 0; i < c->num_entries && curr_level < MAX_MEM_ACC_LEVELS; i++) {
		if (c->osm_table[i].spare_data != curr_level) {
			mem_acc_level_map[j++] = c->osm_table[i].virtual_corner;
			curr_level = c->osm_table[i].spare_data;
			if (j >= MAX_MEM_ACC_LEVELS)
				break;
		}
	}

	osm_write(c, MEM_ACC_SEQ_CONST(1), SEQ_REG(51));
	osm_write(c, MEM_ACC_SEQ_CONST(2), SEQ_REG(52));
	osm_write(c, MEM_ACC_SEQ_CONST(3), SEQ_REG(53));
	osm_write(c, MEM_ACC_SEQ_CONST(4), SEQ_REG(54));
	osm_write(c, MEM_ACC_APM_READ_MASK, SEQ_REG(59));
	osm_write(c, mem_acc_level_map[0], SEQ_REG(55));
	osm_write(c, mem_acc_level_map[0] + 1, SEQ_REG(56));
	osm_write(c, mem_acc_level_map[1], SEQ_REG(57));
	osm_write(c, mem_acc_level_map[1] + 1, SEQ_REG(58));

	for (i = 0; i < MAX_MEM_ACC_VALUES; i++)
		osm_write(c, c->apcs_mem_acc_val[i], MEM_ACC_SEQ_REG_VAL_START(i));
	for (i = 0; i < MAX_MEM_ACC_VAL_PER_LEVEL; i++)
		osm_write(c, c->apcs_mem_acc_cfg[i], MEM_ACC_SEQ_REG_CFG_START(i));

	osm_mb(c);
}

/* ---------------------------------------------------------------------
 * Sequencer microcode load - clk_osm_setup_sequencer(). Plain MMIO,
 * no SCM. Must run AFTER clk_osm_do_additional_setup()'s GFMUX/PLL
 * register-address programming below, matching downstream's call order
 * in clk_osm_do_additional_setup() (sequencer load is the last step).
 * --------------------------------------------------------------------- */
static void clk_osm_setup_sequencer(struct clk_osm *c)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(seq_instr); i++)
		osm_write(c, seq_instr[i], SEQ_MEM_ADDR + i * 4);
	for (i = 0; i < ARRAY_SIZE(seq_br_instr); i++)
		osm_write(c, seq_br_instr[i], SEQ_CFG_BR_ADDR + i * 4);
	osm_mb(c);
}

/* ---------------------------------------------------------------------
 * ITM (Inter-cluster Transition Manager?) handoff - downstream operates
 * on both clusters together here since it decides which cluster's ITM
 * is "primary". We call it once, from the perfcl probe (see probe()
 * wiring), passing both clk_osm structs.
 * --------------------------------------------------------------------- */
static void clk_osm_setup_itm_to_osm_handoff(struct clk_osm *pwrcl,
					      struct clk_osm *perfcl)
{
	osm_write(pwrcl, pwrcl->apcs_itm_present, SEQ_REG(37));
	osm_write(pwrcl, 0, SEQ_REG(38));
	osm_write(perfcl, perfcl->apcs_itm_present, SEQ_REG(37));
	osm_write(perfcl, 0, SEQ_REG(38));

	osm_write(pwrcl, ITM_CL0_DISABLE_CL1_ENABLED, SEQ_REG(39));
	osm_write(perfcl, ITM_CL0_ENABLED_CL1_DISABLE, SEQ_REG(39));
}

/* ---------------------------------------------------------------------
 * clk_osm_do_additional_setup() - PLL L-val / post-div / GFMUX / APM
 * control-register address programming, run ONLY because we confirmed
 * TZ leaves this undone on whyred. All plain MMIO in the downstream
 * source (no SCM here, unlike apm_vc_setup's TZ-present branch).
 * --------------------------------------------------------------------- */
static void clk_osm_do_additional_setup(struct clk_osm *c)
{
	osm_write(c, BVAL(23, 16, 0xF), SPM_CC_CTRL);

	osm_write(c, c->l_val_base, SEQ_REG(0));
	osm_write(c, PLL_MIN_LVAL, SEQ_REG(21));

	osm_write(c, c->apcs_pll_user_ctl, SEQ_REG(18));
	osm_write(c, PLL_POST_DIV2, SEQ_REG(19));
	osm_write(c, PLL_POST_DIV1, SEQ_REG(29));

	clk_osm_program_apm_regs(c);

	osm_write(c, c->apcs_cfg_rcgr, SEQ_REG(16));
	osm_write(c, c->apcs_cmd_rcgr, SEQ_REG(33));
	osm_write(c, RCG_UPDATE, SEQ_REG(34));
	osm_write(c, GPLL_SEL, SEQ_REG(17));
	osm_write(c, PLL_EARLY_SEL, SEQ_REG(82));
	osm_write(c, PLL_MAIN_SEL, SEQ_REG(83));
	osm_write(c, RCG_UPDATE_SUCCESS, SEQ_REG(84));
	osm_write(c, RCG_UPDATE, SEQ_REG(85));

	osm_mb(c);
	/* seq_instr/seq_br_instr load happens separately, see probe():
	 * clk_osm_setup_sequencer() is called once per cluster after both
	 * clusters have had do_additional_setup() applied, matching
	 * downstream's ordering (do_additional_setup for both clusters,
	 * THEN itm_to_osm_handoff, THEN setup_sequencer for both). */
}

/* ---------------------------------------------------------------------
 * clk_ops - determine_rate/set_rate mirror clk_osm_search_table() +
 * clk_osm_set_rate() downstream: rate -> LUT row index -> write index
 * into DCVS_PERF_STATE_DESIRED_REG. The hardware does the actual PLL/
 * voltage sequencing from there; this is the same mechanism mainline's
 * qcom-cpufreq-hw.c uses for SDM845+, just invoked from a clk_ops
 * instead (matching how this generation's LUT has to be *written* by
 * Linux rather than merely *read*, see file header).
 * --------------------------------------------------------------------- */
static int clk_osm_find_index(struct clk_osm *c, unsigned long rate)
{
	int quad_idx, single_idx = -1;

	for (quad_idx = 0; quad_idx < c->num_entries; quad_idx++) {
		unsigned int cores = CORE_COUNT_VAL(c->osm_table[quad_idx].freq_data);

		if (c->osm_table[quad_idx].frequency != rate)
			continue;
		if (cores == SINGLE_CORE)
			single_idx = quad_idx;
		else if (cores == MAX_CORE_COUNT)
			return quad_idx;
	}
	return single_idx;
}

static int clk_osm_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct clk_osm *c = to_clk_osm(hw);
	int index = clk_osm_find_index(c, rate);

	if (index < 0)
		return -EINVAL;

	osm_write(c, index, DCVS_PERF_STATE_DESIRED_REG);
	osm_mb(c);
	c->cur_index = index;

	return 0;
}

static unsigned long clk_osm_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_osm *c = to_clk_osm(hw);

	if (c->cur_index < 0 || c->cur_index >= c->num_entries)
		return 0;

	return c->osm_table[c->cur_index].frequency;
}

static int clk_osm_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_osm *c = to_clk_osm(hw);
	unsigned int i;
	unsigned long best = 0;

	for (i = 0; i < c->num_entries; i++) {
		unsigned int cores =
			CORE_COUNT_VAL(c->osm_table[i].freq_data);
		unsigned long freq = c->osm_table[i].frequency;

		if (cores != MAX_CORE_COUNT)
			continue;

		if (freq >= req->rate) {
			req->rate = freq;
			return 0;
		}

		best = freq;
	}

	if (!best)
		return -EINVAL;

	req->rate = best;
	return 0;
}
/*static long clk_osm_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	struct clk_osm *c = to_clk_osm(hw);
	unsigned int i;
	long best = -1;

	for (i = 0; i < c->num_entries; i++) {
		unsigned int cores = CORE_COUNT_VAL(c->osm_table[i].freq_data);

		if (cores != MAX_CORE_COUNT)
			continue;
		if (c->osm_table[i].frequency >= rate)
			return c->osm_table[i].frequency;
		best = c->osm_table[i].frequency;
	}

	return best;
} old code */

static int clk_osm_enable(struct clk_hw *hw)
{
	struct clk_osm *c = to_clk_osm(hw);

	udelay(5);
	osm_write(c, 1, ENABLE_REG);
	osm_mb(c);
	udelay(5);

	return 0;
}

static const struct clk_ops clk_osm_ops = {
	.enable		= clk_osm_enable,
	.set_rate	= clk_osm_set_rate,
	.recalc_rate	= clk_osm_recalc_rate,
	//.round_rate	= clk_osm_round_rate, old code :(
	.determine_rate	= clk_osm_determine_rate,
};

/* ---------------------------------------------------------------------
 * DT parsing. Property names deliberately match the downstream
 * qcom,{pwrcl,perfcl}-speedbin<N>-v<M> array shape so whyred's existing
 * board DT data (already extracted from msm8998-interposer-sdm660.dtsi)
 * can be reused without re-encoding. See sdm660-whyred-cpu-osm.dtsi.
 *
 * Each row is 5 cells, order taken from downstream's own field enum
 * (FREQ, FREQ_DATA, PLL_OVERRIDES, SPARE_DATA, VIRTUAL_CORNER - in that
 * order, NUM_FIELDS=5 in clock-osm.c):
 *   <freq_hz  freq_data  override_data  spare_data  virtual_corner_1based>
 * virtual_corner is 1-based in DT and converted to 0-based on read, same
 * as downstream's "array[i + VIRTUAL_CORNER] - 1".
 * --------------------------------------------------------------------- */
static int clk_osm_parse_lut(struct platform_device *pdev, struct clk_osm *c,
			      const char *propname)
{
	struct device_node *np = pdev->dev.of_node;
	int count, rows, i;
	u32 *buf;
	int ret;

	count = of_property_count_u32_elems(np, propname);
	if (count <= 0) {
		dev_err(&pdev->dev, "missing/empty %s\n", propname);
		return -EINVAL;
	}
	if (count % 5) {
		dev_err(&pdev->dev, "%s: element count %d not a multiple of 5\n",
			propname, count);
		return -EINVAL;
	}
	rows = count / 5;
	if (rows > OSM_TABLE_SIZE) {
		dev_warn(&pdev->dev, "%s: %d rows, truncating to %d\n",
			 propname, rows, OSM_TABLE_SIZE);
		rows = OSM_TABLE_SIZE;
	}

	buf = kcalloc(count, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, propname, buf, rows * 5);
	if (ret) {
		kfree(buf);
		return ret;
	}

	for (i = 0; i < rows; i++) {
		struct osm_entry *e = &c->osm_table[i];
		u32 vc_1based = buf[i * 5 + 4];

		e->frequency	  = buf[i * 5 + 0];
		e->freq_data	  = buf[i * 5 + 1];
		e->override_data = buf[i * 5 + 2];
		e->spare_data	  = buf[i * 5 + 3];
		/* virtual corners are 1-based in the DT array, same as
		 * downstream's clk_osm_get_lut(). */
		e->virtual_corner = vc_1based ? vc_1based - 1 : 0;
	}
	c->num_entries = rows;

	kfree(buf);
	return 0;
}

static int clk_osm_read_speedbin(struct platform_device *pdev,
				  const char *nvmem_name, u32 shift, u32 mask,
				  u32 *speedbin)
{
	struct nvmem_cell *cell;
	void *val;
	size_t len;
	u32 raw = 0;

	if (!mask) {
		*speedbin = 0;
		return 0;
	}

	cell = nvmem_cell_get(&pdev->dev, nvmem_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	val = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);
	if (IS_ERR(val))
		return PTR_ERR(val);

	memcpy(&raw, val, min(len, sizeof(raw)));
	kfree(val);

	*speedbin = (raw >> shift) & mask;
	return 0;
}


/*
 * Probe :)
 */
static int clk_osm_probe_cluster(struct platform_device *pdev,
				  struct clk_osm *c, void __iomem *osm_base,
				  const char *name, unsigned int cell_idx,
				  const char *nvmem_name, u32 efuse_shift,
				  u32 efuse_mask,
				  u32 l_val_base, u32 apcs_pll_user_ctl,
				  u32 apcs_cfg_rcgr, u32 apcs_cmd_rcgr,
				  u32 apcs_itm_present)
{
	char propname[40];
	u32 speedbin = 0;
	u32 mem_acc_cfg[3];
	struct clk_init_data init = {};
	int ret;

	c->dev = &pdev->dev;
	c->cur_index = -1;
	c->cluster_num = cell_idx;
	/* Both clusters share one "osm" MMIO region (downstream: a single
	 * qcom,cpu-clock-8998@0x179c0000 node drives pwrcl+perfcl together)
	 * - see sdm660-whyred-cpu-osm.dtsi for why this replaced the
	 * earlier (wrong) two-separate-region draft. */
	c->base = osm_base;
	c->l_val_base = l_val_base;
	c->apcs_pll_user_ctl = apcs_pll_user_ctl;
	c->apcs_cfg_rcgr = apcs_cfg_rcgr;
	c->apcs_cmd_rcgr = apcs_cmd_rcgr;
	c->apcs_itm_present = apcs_itm_present;

	ret = clk_osm_read_speedbin(pdev, nvmem_name, efuse_shift, efuse_mask,
				     &speedbin);
	if (ret == -EPROBE_DEFER)
		return ret;
	if (ret) {
		dev_warn(&pdev->dev, "%s: no speedbin nvmem cell, using bin 0 (%d)\n",
			 name, ret);
		speedbin = 0;
	}

	snprintf(propname, sizeof(propname), "qcom,%s-speedbin%u-v0", name, speedbin);
	ret = clk_osm_parse_lut(pdev, c, propname);
	if (ret) {
		/* fall back to bin 0 if the selected bin isn't present */
		dev_warn(&pdev->dev, "%s not found, retrying speedbin0\n", propname);
		snprintf(propname, sizeof(propname), "qcom,%s-speedbin0-v0", name);
		ret = clk_osm_parse_lut(pdev, c, propname);
		if (ret)
			return ret;
	}

	dev_info(&pdev->dev, "%s: %u LUT rows loaded, speedbin=%u\n",
		 name, c->num_entries, speedbin);

	/* TODO: vc_to_uv[] should come from per-cluster opp-microvolt
	 * values - THIS IS STILL THE ONE REMAINING REAL BLOCKER, see
	 * README. clk_osm_setup_hw_table() will program 0mV open-loop
	 * voltage for every row until this is filled in. DO NOT run this
	 * on real silicon with vc_to_uv unpopulated. */
	c->num_vc = 0;
	c->vc_to_uv = NULL;

	snprintf(propname, sizeof(propname), "qcom,apm-mode-ctl");
	of_property_read_u32_index(pdev->dev.of_node, propname, cell_idx, &c->apm_mode_ctl);
	of_property_read_u32_index(pdev->dev.of_node, "qcom,apm-ctrl-status", cell_idx, &c->apm_ctrl_status);

	/* apm_threshold_vc/apm_crossover_vc: downstream derives these from
	 * a CPRh regulator's corner count (see clk_osm_resolve_crossover_
	 * corners()), which we deliberately don't port (would require the
	 * full CPR/rpmh regulator stack). Approximation used here instead:
	 * both default to the highest LUT virtual corner, which is at
	 * least a safe (if not necessarily optimal) crossover point - it
	 * means APM switches to APC mode at/near the top of the table
	 * rather than at the true CPR-measured threshold. Revisit once
	 * vc_to_uv[] is populated: proper fix is "first vc whose uv >=
	 * qcom,apm-threshold-voltage". */
	c->apm_threshold_vc = c->num_entries ? c->osm_table[c->num_entries - 1].virtual_corner : 0;
	c->apm_crossover_vc = c->apm_threshold_vc;

	ret = of_property_read_u32_array(pdev->dev.of_node,
					  cell_idx ? "qcom,perfcl-apcs-mem-acc-cfg"
						   : "qcom,pwrcl-apcs-mem-acc-cfg",
					  mem_acc_cfg, 3);
	if (!ret)
		memcpy(c->apcs_mem_acc_cfg, mem_acc_cfg, sizeof(mem_acc_cfg));

	of_property_read_u32_array(pdev->dev.of_node,
				    cell_idx ? "qcom,perfcl-apcs-mem-acc-val"
					     : "qcom,pwrcl-apcs-mem-acc-val",
				    c->apcs_mem_acc_val, MAX_MEM_ACC_VALUES);

	/* ACD - fully optional */
	c->acd_base = devm_platform_ioremap_resource_byname(pdev,
				cell_idx ? "perfcl-acd" : "pwrcl-acd");
	if (IS_ERR(c->acd_base)) {
		c->acd_base = NULL;
	} else {
		of_property_read_u32_index(pdev->dev.of_node, "qcom,acdtd-val",
					    cell_idx, &c->acd_td);
		of_property_read_u32_index(pdev->dev.of_node, "qcom,acdcr-val",
					    cell_idx, &c->acd_cr);
		of_property_read_u32_index(pdev->dev.of_node, "qcom,acdsscr-val",
					    cell_idx, &c->acd_sscr);
		of_property_read_u32_index(pdev->dev.of_node, "qcom,acdextint0-val",
					    cell_idx, &c->acd_extint0_cfg);
		of_property_read_u32_index(pdev->dev.of_node, "qcom,acdextint1-val",
					    cell_idx, &c->acd_extint1_cfg);
		of_property_read_u32_index(pdev->dev.of_node, "qcom,acdautoxfer-val",
					    cell_idx, &c->acd_autoxfer_ctl);
	}

	init.name = name;
	init.ops = &clk_osm_ops;
	c->hw.init = &init;

	/* Bring-up order matches downstream cpu_clock_osm_driver_probe():
	 * LUT -> hw table push -> (cc/llm policy tuning, not ported) ->
	 * setup_fsms (not ported, optional) -> do_additional_setup
	 * (PLL/GFMUX programming - needed here, TZ doesn't do it) ->
	 * mem_acc -> apm_vc_setup -> acd_init (last, per-cluster) ->
	 * (itm handoff + sequencer load happen once for both clusters,
	 * done by the caller after both clusters reach this point -
	 * see clk_osm_probe()).
	 *
	 * All of this - INCLUDING the initial enable+index write right
	 * below - now runs BEFORE devm_clk_hw_register(). Doing it after
	 * registration was the actual bug behind cpufreq-dt's ENODEV/
	 * "->get() failed": the clk core caches its rate exactly once, at
	 * registration time, by calling recalc_rate() itself - it does
	 * NOT re-invoke recalc_rate() on every clk_get_rate() call. A
	 * direct call to our own set_rate() *after* registration writes
	 * the hardware correctly but never touches that cache, so
	 * clk_get_rate() (which cpufreq-dt's init path calls before ever
	 * setting a rate itself) kept reading the stale value cached at
	 * registration - 0, since cur_index was still -1 back then.
	 * Setting cur_index and enabling the hardware before registration
	 * means recalc_rate() reports the real frequency on that first,
	 * only call the core ever makes on its own. */
	clk_osm_setup_hw_table(c);
	clk_osm_do_additional_setup(c);
	clk_osm_program_mem_acc_regs(c);
	clk_osm_apm_vc_setup(c);

	ret = clk_osm_acd_init(c);
	if (ret)
		dev_warn(&pdev->dev,
			 "%s: ACD init failed (%d) - continuing without it\n",
			 name, ret);

	if (c->num_entries) {
		clk_osm_enable(&c->hw);
		ret = clk_osm_set_rate(&c->hw, c->osm_table[0].frequency, 0);
		if (ret)
			dev_warn(&pdev->dev,
				 "%s: could not set initial rate (%d) - "
				 "cpufreq-dt registration will likely fail\n",
				 name, ret);
		else
			dev_info(&pdev->dev, "%s: enabled, initial rate %u Hz\n",
				 name, c->osm_table[0].frequency);
	}

	ret = devm_clk_hw_register(&pdev->dev, &c->hw);
	if (ret)
		return ret;

	return 0;
}

/* One shared "osm" MMIO region drives both clusters (see DT file),
 * matching downstream's single-node/two-static-struct layout. We
 * register two clk_hw providers off that one region and hand them out
 * via #clock-cells = <1> (0 = pwrcl, 1 = perfcl). */
struct clk_osm_cpucc {
	struct clk_osm pwrcl;
	struct clk_osm perfcl;
	struct clk_hw_onecell_data onecell;
};

static int clk_osm_probe(struct platform_device *pdev)
{
	struct clk_osm_cpucc *cc;
	void __iomem *osm_base;
	u32 l_val_base[2] = {}, apcs_pll_user_ctl[2] = {};
	u32 apcs_cfg_rcgr[2] = {}, apcs_cmd_rcgr[2] = {};
	u32 apcs_itm_present[2] = {};
	int ret;

	cc = devm_kzalloc(&pdev->dev,
			   struct_size(cc, onecell.hws, 2), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	osm_base = devm_platform_ioremap_resource_byname(pdev, "osm");
	if (IS_ERR(osm_base))
		return PTR_ERR(osm_base);

	/* All four of these are <pwrcl_val perfcl_val> 2-cell DT arrays,
	 * matching downstream's qcom,l-val-base etc layout - see
	 * sdm660-whyred-cpu-osm.dtsi. */
	of_property_read_u32_array(pdev->dev.of_node, "qcom,l-val-base", l_val_base, 2);
	of_property_read_u32_array(pdev->dev.of_node, "qcom,apcs-pll-user-ctl", apcs_pll_user_ctl, 2);
	of_property_read_u32_array(pdev->dev.of_node, "qcom,apcs-cfg-rcgr", apcs_cfg_rcgr, 2);
	of_property_read_u32_array(pdev->dev.of_node, "qcom,apcs-cmd-rcgr", apcs_cmd_rcgr, 2);
	of_property_read_u32_array(pdev->dev.of_node, "qcom,apcs-itm-present", apcs_itm_present, 2);

	ret = clk_osm_probe_cluster(pdev, &cc->pwrcl, osm_base, "pwrcl", 0,
				     "pwrcl_efuse", PWRCL_EFUSE_SHIFT, PWRCL_EFUSE_MASK,
				     l_val_base[0], apcs_pll_user_ctl[0],
				     apcs_cfg_rcgr[0], apcs_cmd_rcgr[0],
				     apcs_itm_present[0]);
	if (ret)
		return ret;

	ret = clk_osm_probe_cluster(pdev, &cc->perfcl, osm_base, "perfcl", 1,
				     "perfcl_efuse", PERFCL_EFUSE_SHIFT, PERFCL_EFUSE_MASK,
				     l_val_base[1], apcs_pll_user_ctl[1],
				     apcs_cfg_rcgr[1], apcs_cmd_rcgr[1],
				     apcs_itm_present[1]);
	if (ret)
		return ret;

	/* Both clusters have had do_additional_setup() applied at this
	 * point (inside clk_osm_probe_cluster) - now do the two steps
	 * downstream does across BOTH clusters together, in this order:
	 * ITM handoff, then sequencer microcode load for each. */
	clk_osm_setup_itm_to_osm_handoff(&cc->pwrcl, &cc->perfcl);
	clk_osm_setup_sequencer(&cc->pwrcl);
	clk_osm_setup_sequencer(&cc->perfcl);

	/* Only now, with PLL/GFMUX/microcode actually loaded, is it
	 * meaningful to enable the clocks. clk_osm_enable() (ENABLE_REG=1)
	 * runs lazily via the clk framework on first clk_prepare_enable()
	 * from a consumer (e.g. a cpufreq driver) - not forced here. */

	cc->onecell.num = 2;
	cc->onecell.hws[0] = &cc->pwrcl.hw;
	cc->onecell.hws[1] = &cc->perfcl.hw;

	ret = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_onecell_get,
					   &cc->onecell);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, cc);
	return 0;
}

static const struct of_device_id clk_osm_match_table[] = {
	/*
	 * Tested only on sdm636. I don't know if it works on sdm660/630 or not.
	 */
	{ .compatible = "qcom,sdm630-cpu-clock-osm" },
	{ .compatible = "qcom,sdm636-cpu-clock-osm" },
	{ .compatible = "qcom,sdm660-cpu-clock-osm" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_osm_match_table);

static struct platform_driver clk_osm_driver = {
	.probe = clk_osm_probe,
	.driver = {
		.name = "qcom-clk-cpu-osm-sdm660",
		.of_match_table = clk_osm_match_table,
	},
};
module_platform_driver(clk_osm_driver);

MODULE_DESCRIPTION("SDM660/636 OSM v1 CPU clock driver (experimental port)");
MODULE_AUTHOR("Kulesha evgeniy <voovdop@gmail.com>");
MODULE_LICENSE("GPL");
