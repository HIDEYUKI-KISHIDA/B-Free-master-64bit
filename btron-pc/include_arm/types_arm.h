// ARM MMU/MPU サポート用型定義・定数
#define PAGE_SIZE      4096
#define PAGE_TABLE_ENTRIES 1024

typedef struct {
	unsigned int entries[PAGE_TABLE_ENTRIES];
} page_table_t;

// MMU制御用レジスタ定数（例: ARMv7）
#define MMU_CONTROL_ENABLE      (1 << 0)
#define MMU_CONTROL_ALIGN      (1 << 1)
#define MMU_CONTROL_DCACHE     (1 << 2)
#define MMU_CONTROL_ICACHE     (1 << 12)

// ...他に必要な定数・型は随時追加
// ARM用型定義ヘッダ雛形
