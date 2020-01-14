#ifndef TEST_SNAP_DMA_H
#define TEST_SNAP_DMA_H

/* Physically continuous memory block */
class pmem_block {
	public:
	char     *va_base;
	uintptr_t pa_base;
	size_t    blk_size;

	virtual int alloc(size_t size);
	virtual void free(void);
	void dump();
	static void dma_init();
	static void dma_destroy();
	static uintptr_t virt_to_phys(uintptr_t vaddr);

	private:
	static void *heap_base;
	static void *heap_top;
	static void *heap_end;
	static const int  HUGE_PAGE_SIZE  = 2*1024*1024;
	static const int  SMALL_PAGE_SIZE = 4096;

	static void *dma_page_alloc();
	static void *dma_page_free();
};

class SnapDmaTest : public ::testing::Test {
	virtual void SetUp();
	virtual void TearDown();

	private:
	void alloc_bufs();
	void free_bufs();

	protected:
	struct ibv_pd *m_pd;
	struct ibv_comp_channel *m_comp_channel;
	struct ibv_mr *m_lmr;
	struct ibv_mr *m_rmr;
	char *m_lbuf;
	char *m_rbuf;
	int   m_bsize;
	int   m_bcount;
	struct snap_dma_q_create_attr m_dma_q_attr;

	void dma_xfer_test(struct snap_dma_q *q, bool is_read, bool poll_mode,
			void *rvaddr, void *rpaddr, uint32_t rkey, int len);
	struct snap_dma_q *create_queue();
	public:
	/* Send data to sw qp */
	static int snap_dma_q_fw_send(struct snap_dma_q *q, void *src_buf,
			size_t len, uint32_t lkey);
	/* For testing only. Send with imm */
	static int snap_dma_q_fw_send_imm(struct snap_dma_q *q, uint32_t imm);
};

#endif
