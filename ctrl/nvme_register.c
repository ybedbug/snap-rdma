#include "nvme_register.h"

struct nvme_register_desc {
	unsigned int			offset;
	unsigned int			size;
	int				type;
	const char			*name;
	const char			*desc;
	void (*reg_dump_func)(struct nvme_register_desc *reg, uint64_t value);
};

static void nvme_reg_cap_dump(struct nvme_register_desc *reg, uint64_t cap);
static void nvme_reg_vs_dump(struct nvme_register_desc *reg, uint64_t vs);
static void nvme_reg_cc_dump(struct nvme_register_desc *reg, uint64_t cc);
static void nvme_reg_csts_dump(struct nvme_register_desc *reg, uint64_t csts);
static void nvme_reg_aqa_dump(struct nvme_register_desc *reg, uint64_t aqa);

static struct nvme_register_desc nvme_regs[] = {
	{ NVME_REG_CAP, 8, NVME_REG_RO, "CAP", "Controller Capabilities", nvme_reg_cap_dump},
	{ NVME_REG_VS, 4, NVME_REG_RO, "VS", "Controller Version", nvme_reg_vs_dump},
	{ NVME_REG_INTMS, 4, NVME_REG_RW1S, "INTMS", "Interrupt Mask Set"},
	{ NVME_REG_INTMC, 4, NVME_REG_RW1C, "INTMC", "Interrupt Mask Clear" },
	{ NVME_REG_CC, 4, NVME_REG_RW, "CC", "Controller Configuration", nvme_reg_cc_dump},
	{ NVME_REG_CSTS, 4, NVME_REG_RO|NVME_REG_RW1C, "CSTS", "Controller Status", nvme_reg_csts_dump},
	{ NVME_REG_NSSR, 4, NVME_REG_RW, "NSSR", "NVM Subsystem Reset"},

	/* Adming Queue */
	{ NVME_REG_AQA, 4, NVME_REG_RW, "AQA", "Admin Queue Attributes", nvme_reg_aqa_dump },
	{ NVME_REG_ASQ, 8, NVME_REG_RW, "ASQ", "Admin Submission Queue Base Address" },
	{ NVME_REG_ACQ, 8, NVME_REG_RW, "ACQ", "Admin Completion Queue Base Address" },

	/* Optional registers */
	{ NVME_REG_CMBLOC, 4, NVME_REG_RO, "CMBLOC", "Controller Memory Buffer Location" },
	{ NVME_REG_CMBSZ, 4, NVME_REG_RO, "CMBSZ", "Controller Memory Buffer Size" },
	{ NVME_REG_BPINFO, 4, NVME_REG_RO, "BPINFO", "Boot Partition Information" },
	{ NVME_REG_BPRSEL, 4, NVME_REG_RW, "BPRSEL", "Boot Partition Select" },
	{ NVME_REG_BPMBL, 8, NVME_REG_RW, "BPMBL", "Boot Partition Memory Buffer Location" },
	{ NVME_REG_LAST, 0, 0, 0, 0},
};

/* Maximum Queue Entries Supported */
static int nvme_reg_cap_mqes(uint64_t cap)
{
	return cap & 0xffff;
}

/* Contiguous Queues Required (for Submission and Completion queues) */
static int nvme_reg_cap_cqr(uint64_t cap)
{
	return (cap >> 16) & 0x1;
}

/* Arbitration Mechanism Supported */
static int nvme_reg_cap_ams(uint64_t cap)
{
	return (cap >> 17) & 0x3;
}

/*
 * Timeout (the worst case time that host software shall wait for CSTS.RDY
 * transitions)
 */
static int nvme_reg_cap_to(uint64_t cap)
{
	return (cap >> 24) & 0xff;
}

/* Doorbell Stride */
static int nvme_reg_cap_dstrd(uint64_t cap)
{
	return (cap >> 32) & 0xf;
}

/* NVM Subsystem Reset Supported */
static int nvme_reg_cap_nssrs(uint64_t cap)
{
	return (cap >> 33) & 0x1;
}

/* Command Sets Supported */
static int nvme_reg_cap_css(uint64_t cap)
{
	return (cap >> 37) & 0xff;
}

/* Boot Partition Support */
static int nvme_reg_cap_bps(uint64_t cap)
{
	return (cap >> 45) & 0x1;
}

/* Memory Page Size Minimum */
static int nvme_reg_cap_mpsmin(uint64_t cap)
{
	return (cap >> 48) & 0xf;
}

/* Memory Page Size Maximum */
static int nvme_reg_cap_mpsmax(uint64_t cap)
{
	return (cap >> 52) & 0xf;
}

static void nvme_reg_cap_dump(struct nvme_register_desc *reg, uint64_t cap)
{
	printf("%-6s [%s, 0x%02x..0x%02x]: 0x%llx {MQES:%d CQR:%d AMS:%d TO:%d "
	       "DSTRD:%d NSSRS:%d CSS:%d BPS:%d MPSMIN:%d MPSMAX:%d}\n",
	       reg->name, reg->desc, reg->offset, reg->offset + reg->size, cap,
	       nvme_reg_cap_mqes(cap), nvme_reg_cap_cqr(cap),
	       nvme_reg_cap_ams(cap), nvme_reg_cap_to(cap),
	       nvme_reg_cap_dstrd(cap), nvme_reg_cap_nssrs(cap),
	       nvme_reg_cap_css(cap), nvme_reg_cap_bps(cap),
	       nvme_reg_cap_mpsmin(cap), nvme_reg_cap_mpsmax(cap));
}

/* Tertiary Version Number */
static int nvme_reg_vs_ter(uint32_t vs)
{
	return vs & 0xff;
}

/* Minor Version Number */
static int nvme_reg_vs_mnr(uint32_t vs)
{
	return (vs >> 8) & 0xff;
}

/* Major Version Number */
static int nvme_reg_vs_mjr(uint32_t vs)
{
	return (vs >> 16) & 0xffff;
}

static void nvme_reg_vs_dump(struct nvme_register_desc *reg, uint64_t vs)
{
	printf("%-6s [%s, 0x%02x..0x%02x]: 0x%llx {%d.%d.%d}\n",
	       reg->name, reg->desc, reg->offset, reg->offset + reg->size, vs,
	       nvme_reg_vs_mjr(vs), nvme_reg_vs_mnr(vs), nvme_reg_vs_ter(vs));
}

/* controller configuration */
static int nvme_reg_cc_en(uint32_t cc)
{
	return cc & 0x1;
}

static int nvme_reg_cc_css(uint32_t cc)
{
	return (cc >> 4) & 0x7;
}

static int nvme_reg_cc_mps(uint32_t cc)
{
	return (cc >> 7) & 0xf;
}

static int nvme_reg_cc_ams(uint32_t cc)
{
	return (cc >> 11) & 0x7;
}

static int nvme_reg_cc_shn(uint32_t cc)
{
	return (cc >> 14) & 0x3;
}

static int nvme_reg_cc_iosqes(uint32_t cc)
{
	return (cc >> 16) & 0xf;
}

static int nvme_reg_cc_iocqes(uint32_t cc)
{
	return (cc >> 20) & 0xf;
}

static void nvme_reg_cc_dump(struct nvme_register_desc *reg, uint64_t cc)
{
	printf("%-6s [%s, 0x%02x..0x%02x]: 0x%llx {EN:%d CSS:%d MPS:%d AMS:%d "
	       "SHN:%d IOSQES:%d IOCQES:%d}\n", reg->name, reg->desc,
	       reg->offset, reg->offset + reg->size, cc, nvme_reg_cc_en(cc),
	       nvme_reg_cc_css(cc), nvme_reg_cc_mps(cc), nvme_reg_cc_ams(cc),
	       nvme_reg_cc_shn(cc), nvme_reg_cc_iosqes(cc),
	       nvme_reg_cc_iocqes(cc));
}

static int nvme_reg_csts_rdy(uint32_t csts)
{
	return (csts >> CSTS_RDY_SHIFT) & CSTS_RDY_MASK;
}

static int nvme_reg_csts_cfs(uint32_t csts)
{
	return (csts >> CSTS_CFS_SHIFT) & CSTS_CFS_MASK;
}

static int nvme_reg_csts_shst(uint32_t csts)
{
	return (csts >> CSTS_SHST_SHIFT) & CSTS_SHST_MASK;
}

static int nvme_reg_csts_nssro(uint32_t csts)
{
	return (csts >> CSTS_NSSRO_SHIFT) & CSTS_NSSRO_MASK;
}

static int nvme_reg_csts_pp(uint32_t csts)
{
	return (csts >> CSTS_PP_SHIFT) & CSTS_PP_MASK;
}

static void nvme_reg_csts_dump(struct nvme_register_desc *reg, uint64_t csts)
{
	printf("%-6s [%s, 0x%02x..0x%02x]: 0x%llx {RDY:%d CFS:%d SHST:%d "
	       "NSSRO:%d PP:%d}\n", reg->name, reg->desc,
	       reg->offset, reg->offset + reg->size, csts,
	       nvme_reg_csts_rdy(csts), nvme_reg_csts_cfs(csts),
	       nvme_reg_csts_shst(csts), nvme_reg_csts_nssro(csts),
	       nvme_reg_csts_pp(csts));
}

static int nvme_reg_aqa_asqs(uint32_t aqa)
{
	return aqa & 0xfff;
}

static int nvme_reg_aqa_acqs(uint32_t aqa)
{
	return (aqa >> 16) & 0xfff;
}

static void nvme_reg_aqa_dump(struct nvme_register_desc *reg, uint64_t aqa)
{
	printf("%-6s [%s, 0x%02x..0x%02x]: 0x%llx {ASQS:%d ACQS:%d}\n",
	       reg->name, reg->desc, reg->offset, reg->offset + reg->size, aqa,
	       nvme_reg_aqa_asqs(aqa), nvme_reg_aqa_acqs(aqa));
}

static uint64_t nvme_register_get_value(struct nvme_register_desc *reg,
		void *bar)
{
	if (reg->size == 4)
		return *((uint32_t *)(bar + reg->offset));
	else
		return *((uint64_t *)(bar + reg->offset));
}

static void nvme_register_set_value(struct nvme_register_desc *reg, void *bar,
		uint64_t new_value)
{
	if (reg->size == 4)
		*((uint32_t *)(bar + reg->offset)) = new_value;
	else
		*((uint64_t *)(bar + reg->offset)) = new_value;
}

void nvme_bar_dump(struct nvme_bar *bar)
{
	int i;

	for (i = 0; nvme_regs[i].offset != NVME_REG_LAST; i++) {
		struct nvme_register_desc *reg = &nvme_regs[i];

		if (reg->reg_dump_func)
			reg->reg_dump_func(reg, nvme_register_get_value(reg, bar));
	}
}

int nvme_initial_register_check(struct nvme_bar *bar)
{
	/* Controller Capabilities */
	if (!bar->cap)
		goto out_err;
	if (nvme_reg_cap_mqes(bar->cap) <= 0)
		goto out_err;
	if (nvme_reg_cap_cqr(bar->cap) != 1)
		goto out_err;
	if (nvme_reg_cap_ams(bar->cap) != 0)
		goto out_err;
	if (nvme_reg_cap_nssrs(bar->cap) != 0)
		goto out_err;
	if (nvme_reg_cap_css(bar->cap) != 1)
		goto out_err;
	if (nvme_reg_cap_bps(bar->cap) != 0)
		goto out_err;
	if (nvme_reg_cap_mpsmin(bar->cap) != 0)
		goto out_err;

	/* Version */
	if (!bar->vs)
		goto out_err;
	if (nvme_reg_vs_mjr(bar->vs) != 1)
		goto out_err;
	if (nvme_reg_vs_mnr(bar->vs) < 1)
		goto out_err;

	return 0;
out_err:
	return -EINVAL;
}

static const char *nvme_reg_str(unsigned int reg)
{
	switch (reg) {
	case NVME_REG_CAP:
		return "Controller Capabilities (CAP)";
	case NVME_REG_VS:
		return "Controller Version (VS)";
	case NVME_REG_INTMS:
		return "Interrupt Mask Set (INTMS)";
	case NVME_REG_INTMC:
		return "Interrupt Mask Set (INTMC)";
	case NVME_REG_CC:
		return "Controller Configuration (CC)";
	case NVME_REG_CSTS:
		return "Controller Status (CSTS)";
	case NVME_REG_NSSR:
		return "NVM Subsystem Reset (NSSR)";
	case NVME_REG_AQA:
		return "Admin Queue Attributes (AQA)";
	case NVME_REG_ASQ:
		return "Admin Submission Queue Base Address (ASQ)";
	case NVME_REG_ACQ:
		return "Admin Completion Queue Base Address (ACQ)";
	case NVME_REG_CMBLOC:
		return "Controller Memory Buffer Location (CMBLOC)";
	case NVME_REG_CMBSZ:
		return "Controller Memory Buffer Size (CMBSZ)";
	case NVME_REG_BPINFO:
		return "Boot Partition Information (BPINFO)";
	case NVME_REG_BPRSEL:
		return "Boot Partition Select (BPRSEL)";
	case NVME_REG_BPMBL:
		return "Boot Partition Memory Buffer Location (BPMBL)";
	default:
		return "unrecognized register";
	}
}

void nvme_register_identify_change(struct nvme_bar *prev,
		struct nvme_bar *curr, nvme_register_change_cb_t change_cb,
		void *ctx)
{
	//TODO: parse bar changes
	printf("bar change detected\n");
}

