#ifndef __XSPI2_NOR_H
#define __XSPI2_NOR_H

#include <stdint.h>

/** XSPI2 NOR CPU 映射基址（与 CMSIS XSPI2_BASE 一致） */
#define XSPI2_NOR_CPU_BASE  0x70000000U

typedef void (*xspi2_nor_progress_fn)(uint32_t done_bytes, uint32_t total_bytes);

/**
 * @brief RIF 后初始化 XSPI2 + NORFlash（命令模式，尚未 mmap）
 * @retval 0 成功；1 NORFlash_Init 失败
 */
uint8_t xspi2_nor_init(void);

/**
 * @brief 在 HyperRAM mmap 之后调用，使 NOR 进入 XIP @ 0x71000000
 * @retval 0 成功；2 mmap 失败
 */
uint8_t xspi2_nor_enable_mmap(void);

/** @brief init + enable_mmap 一步完成（须在未 mmap HyperRAM 前调用 init 部分） */
uint8_t xspi2_nor_mmap_init(void);

void xspi2_nor_get_last_jedec(uint8_t jedec[3]);

/**
 * @brief 退出并重新进入 mmap，修复 XSPI2 总线挂死
 * @retval 0 成功；1 未 init；2 mmap 失败
 */
uint8_t xspi2_nor_ensure_mmap(void);

/**
 * @brief 间接命令读 NOR（cpu_addr >= 0x70000000），内部会短暂退出/恢复 mmap
 * @retval 0 成功；1 参数/未 init；2 退出 mmap 失败；3 读失败；4 恢复 mmap 失败；5 PHY 配置失败
 */
uint8_t xspi2_nor_read_cpu(uint32_t cpu_addr, void *buf, uint32_t len);

/**
 * @brief 单次 mmap 会话内分段间接读（4KB staging），适合大块权重搬运
 * @param progress 可选，每 512KB 回调一次
 * @retval 同 xspi2_nor_read_cpu
 */
uint8_t xspi2_nor_read_bulk(uint32_t cpu_addr, void *buf, uint32_t len,
                          xspi2_nor_progress_fn progress);

/**
 * @brief 一次退出 mmap，分段间接读完后恢复 mmap（read_bulk 的 progress=NULL 包装）
 * @retval 同 xspi2_nor_read_cpu
 */
uint8_t xspi2_nor_read_range(uint32_t cpu_addr, void *buf, uint32_t len);

/** 上次 read_bulk/read_range 失败时的字节偏移（供诊断） */
uint32_t xspi2_nor_last_fail_offset(void);

#endif /* __XSPI2_NOR_H */
