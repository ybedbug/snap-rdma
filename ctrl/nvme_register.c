#include "nvme_register.h"

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

