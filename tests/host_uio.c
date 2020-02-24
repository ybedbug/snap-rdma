#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <glob.h>

#include "host_uio.h"

#define PCI_COMMAND        0x04 /* 16 bits */
#define PCI_COMMAND_MASTER  0x4 /* Enable bus mastering */
#define PCI_COMMAND_MEMORY  0x2 /* Enable response in memory space */

#define PCI_EXP_DEVCTL         0x8
#define PCI_EXP_DEVCTL_BCR_FLR 0x8000 /* Initiate function level reset (FLR) */

static int uio_fd, bar_fd, cfg_fd;
static char *bar_base;
static off_t bar_size;

static bool pci_has_uio(const char *pci_addr, char *uio_path, int path_len)
{
    char path[PATH_MAX];
    glob_t match;

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/uio/uio*", pci_addr);

    if (glob(path, GLOB_ERR, NULL, &match)) {
        printf("device %s is not connected to UIO\n", pci_addr);
        return false;
    }

    snprintf(uio_path, path_len, "%s", basename(match.gl_pathv[0]));
    return true;
}

static int pci_set_bus_master(int dev_fd, bool enable)
{
	uint16_t reg;
	int ret;

	ret = pread(dev_fd, &reg, sizeof(reg), PCI_COMMAND);
	if (ret != sizeof(reg)) {
		printf("Cannot read command from PCI config space!\n");
		return -errno;
	}

	/* always enable memory space access because this is
	 * what Linux drivers (ex nvme and virtio) do.
	 */
	reg |= PCI_COMMAND_MEMORY;
	if (enable)
		reg |= PCI_COMMAND_MASTER;
	else
		reg &= ~PCI_COMMAND_MASTER;

	ret = pwrite(dev_fd, &reg, sizeof(reg), PCI_COMMAND);
	if (ret != sizeof(reg)) {
		printf("Cannot write command to PCI config space!\n");
		return -errno;
	}
	return 0;
}

/**
 * host_uio_bus_master_enable() - Enable bus mastering
 *
 * Return: 0 on success or -errno
 */
int host_uio_bus_master_enable()
{
	return pci_set_bus_master(cfg_fd, true);
}

/**
 * host_uio_bus_master_disable() - Disable bus mastering
 *
 * Return: 0 on success or -errno
 */
int host_uio_bus_master_disable()
{
	return pci_set_bus_master(cfg_fd, false);
}

/**
 * host_uio_open() - Open UIO device
 * @pci_addr:  PCI address (BDF) of the UIO device
 * @bar:       BAR register number
 *
 * The function opens UIO device, enables bus master and maps BAR to
 * be accessible by the process.
 *
 * The function assumes that device has only one interesting BAR.
 *
 * Note: UIO devices do not support DMA and iommu. So one should set iommu=pt
 * kernel option.
 *
 * Return: 0 on success -errno on failure
 */
int host_uio_open(const char *pci_addr, int bar)
{
	char path[PATH_MAX];
	char uio_name[16];

	if (!pci_has_uio(pci_addr, uio_name, sizeof(uio_name)))
		return -ENODEV;

	snprintf(path, sizeof(path), "/dev/%s", uio_name);
	uio_fd = open(path, O_RDWR|O_SYNC);
	if (uio_fd < 0) {
		printf("failed to open %s %m\n", path);
		return -errno;
	}

	/* we need to setup bus mastering because uio does not do it by default */
	snprintf(path, sizeof(path), "/sys/class/uio/%s/device/config", uio_name);
	cfg_fd = open(path, O_RDWR);
	if (cfg_fd < 0) {
		printf("failed to open %s %m\n", path);
		goto close_uio;
	}

	if (host_uio_bus_master_enable())
		goto close_uio_config;

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource%d", pci_addr, bar);
	bar_fd = open(path, O_RDWR|O_SYNC);
	if (bar_fd < 0) {
		printf("failed to open %s %m\n", path);
		goto close_uio_config;
	}

	bar_size = lseek(bar_fd, 0, SEEK_END);
	if (bar_size == (off_t)-1) {
		printf("failed to get bar size %m\n");
		goto close_uio_bar;
	}

	bar_base = mmap(0, bar_size, PROT_READ|PROT_WRITE, MAP_SHARED, bar_fd, 0);
	if (bar_base == MAP_FAILED) {
		printf("mmap failed %m\n");
		goto close_uio_bar;
	}

	return 0;
close_uio_bar:
	close(bar_fd);
close_uio_config:
	close(cfg_fd);
close_uio:
	close(uio_fd);
	return -errno;
}

/**
 * host_uio_close() - Close UIO device
 *
 * Close UIO device opened by the host_uio_open()
 */
void host_uio_close()
{
	host_uio_bus_master_disable();
	munmap(bar_base, bar_size);
	close(bar_fd);
	close(uio_fd);
	close(cfg_fd);
}

/**
 * host_uio_write4() - Write four bytes to the BAR
 * @bar_offset:  offset in the device BAR
 * @val:         value to write to the BAR
 *
 * The function writes four bytes in the host order to the device BAR
 */
void host_uio_write4(unsigned bar_offset, uint32_t val)
{
	char *p = bar_base + bar_offset;

	*(uint32_t *)p = val;
}

/**
 * host_uio_write4() - Write eight bytes to the BAR
 * @bar_offset:  offset in the device BAR
 * @val:         value to write to the BAR
 *
 * The function writes eight bytes in the host order to the device BAR
 */
void host_uio_write8(unsigned bar_offset, uint64_t val)
{
	char *p = bar_base + bar_offset;

	*(uint64_t *)p = val;
}

/**
 * host_uio_read4() - Read four bytes from the BAR
 * @bar_offset:  offset in the device BAR
 *
 * The function reads four bytes in the host order from the device BAR
 *
 * Return: value read from the BAR
 */
uint32_t host_uio_read4(unsigned bar_offset)
{
	char *p = bar_base + bar_offset;

	return *(uint32_t *)p;
}

/**
 * host_uio_read8() - Read eight bytes from the BAR
 * @bar_offset:  offset in the device BAR
 *
 * The function reads four bytes in the host order from the device BAR
 *
 * Return: value read from the BAR
 */
uint64_t host_uio_read8(unsigned bar_offset)
{
	char *p = bar_base + bar_offset;

	return *(uint64_t *)p;
}

/**
 * host_uio_virt_to_phys() - Get physical address of a virtual address
 * @address:  virtual address
 *
 * The function looks up a virtual address in the pagemap file and
 * returns corresponding physical address.
 *
 * /proc/pid/pagemap.  This file lets a userspace process find out which
 * physical frame each virtual page is mapped to.  It contains one 64-bit
 * value for each virtual page, containing the following data (from
 * fs/proc/task_mmu.c, above pagemap_read):
 *
 * - Bits 0-54  page frame number (PFN) if present
 * - Bits 0-4   swap type if swapped
 * - Bits 5-54  swap offset if swapped
 * - Bit  55    pte is soft-dirty (see Documentation/vm/soft-dirty.txt)
 * - Bit  56    page exclusively mapped (since 4.2)
 * - Bits 57-60 zero
 * - Bit  61    page is file-page or shared-anon (since 3.5)
 * - Bit  62    page swapped
 * - Bit  63    page present
 *
 * For more details see
 * the https://www.kernel.org/doc/Documentation/vm/pagemap.txt
 *
 * Note that in linux only virtual addresses of the locked huge pages
 * will always have the same physical addresses. Anythings else (even mlocked
 * memory) can be remapped to different physical addresses.
 *
 * Return: physical address or (uintptr_t)-1 on failure
 */
static uintptr_t host_uio_virt_to_phys(uintptr_t address)
{
	static const char *pagemap_file = "/proc/self/pagemap";
	const size_t page_size = sysconf(_SC_PAGESIZE);
	uint64_t entry, pfn;
	ssize_t offset, ret;
	uintptr_t pa;
	int fd;

	fd = open(pagemap_file, O_RDONLY);
	if (fd < 0) {
		printf("failed to open %s: %m\n", pagemap_file);
		pa = -1;
		goto out;
	}

	offset = (address / page_size) * sizeof(entry);
	ret = lseek(fd, offset, SEEK_SET);
	if (ret != offset) {
		printf("failed to seek in %s to offset %zu: %m\n",
				pagemap_file, offset);
		pa = -1;
		goto out_close;
	}

	ret = read(fd, &entry, sizeof(entry));
	if (ret != sizeof(entry)) {
		printf("read from %s at offset %zu returned %ld: %m\n",
				pagemap_file, offset, ret);
		pa = -1;
		goto out_close;
	}

	/* Bit 63 page present */
	if (entry & (1ULL << 63)) {
		/* Bits 0-54  page frame number (PFN) if present */
		pfn = entry & ((1ULL << 54) - 1);
		if (!pfn) {
			printf("%p got pfn zero. Check CAP_SYS_ADMIN permissions\n",
					(void *)address);
			pa = -1;
			goto out_close;
		}
		/* Use pfn to calculate page address and add address offset
		 * in the page */
		pa = (pfn * page_size) | (address & (page_size - 1));
	} else {
		/* Bit 62 page swapped */
		printf("%p is not present: %s\n", (void *)address,
				entry & (1ULL << 62) ? "swapped" : "oops");
		pa = -1; /* Page not present */
	}

out_close:
	close(fd);
out:
	return pa;
}


static void *heap_base = MAP_FAILED;
static void *heap_top = NULL;
static void *heap_end = NULL;

#define UIO_DMA_MEM_SIZE    2*1024*1024
#define UIO_DMA_PAGE_SIZE   4096
/**
 * host_uio_dma_init() - initialize UIO DMA memory
 *
 * The function allocates a number of hugepages that can be used
 * as DMA memory by the UIO device
 *
 * Return: 0 on succes or -errno
 */
int host_uio_dma_init()
{
	heap_base = mmap(NULL, UIO_DMA_MEM_SIZE,
			PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED|
			/* MAP_HUGE_2MB is used in *conjunction* with MAP_HUGETLB
			 * to select alternative hugetlb page sizes */
#ifdef MAP_HUGE_2MB
			MAP_HUGETLB|MAP_HUGE_2MB,
#else
			MAP_HUGETLB,
#endif
			-1, 0);
	if (heap_base == MAP_FAILED) {
		printf("Huge page allocation failed errno (%d) %m\n"
		       "Check that hugepages are configured and 'ulimit -l'\n", errno);
		return -errno;
	}
	heap_top = heap_base;
	heap_end = (char *)heap_base + UIO_DMA_MEM_SIZE;
	return 0;
}

void host_uio_dma_destroy()
{
	if (heap_base == MAP_FAILED)
		return;
	munmap(heap_base, UIO_DMA_MEM_SIZE);
	heap_base = MAP_FAILED;
	heap_top = heap_end = NULL;
}

/**
 * host_uio_dma_alloc() - allocate memory suitable for DMA to/from UIO device
 * @size:  amount of memory to allocate
 * @pa:    if not NULL, physical memory address will be returned
 *
 * The function allocates memory that can be used to initiate DMA to or from 
 * the UIO device.
 *
 * Note:
 *  - doing DMA to/from UIO devices requires IOMMU in the passthrough
 *    mode. (iommu=pt kernel boot option).
 *  - size is rounded up to the neareast page.
 *  - the allocator has fixed amount of memory and does not support free
 *
 * Return: pointer to the DMA memory (virtual address) or NULL
 */
void *host_uio_dma_alloc(size_t size, uintptr_t *pa)
{
	void *page;

	if (heap_base == MAP_FAILED)
		return NULL;

	size = UIO_DMA_PAGE_SIZE * ((size + UIO_DMA_PAGE_SIZE - 1)/UIO_DMA_PAGE_SIZE);
	page = heap_top;
	heap_top = (char *)heap_top + size;
	if (heap_top > heap_end) {
		heap_top = (char *)heap_top - size;
		printf("Out of DMA memory");
		return NULL;
	}

	if (pa != NULL)
		*pa = host_uio_virt_to_phys((uintptr_t)page);

	return page;
}

/**
 * host_uio_dma_free() - free DMA memory
 *
 * The function frees memory allocated with host_uio_dma_alloc()
 *
 * Note: current implementation does not free memory.
 */
void host_uio_dma_free(void *buf)
{

}
