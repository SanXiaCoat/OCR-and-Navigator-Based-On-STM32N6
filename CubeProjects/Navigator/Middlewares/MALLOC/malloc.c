/**
 ****************************************************************************************************
 * @file        malloc.c
 * @brief       AXISRAM / HyperRAM 块式内存池
 ****************************************************************************************************
 */

#include "malloc.h"
#include <string.h>

static MT_TYPE mem1mapbase[MEM1_ALLOC_TABLE_SIZE];
static MT_TYPE mem2mapbase[MEM2_ALLOC_TABLE_SIZE] __attribute__((section(".EXTRAM_HEAP")));
static uint8_t mem1base[MEM1_MAX_SIZE];
static uint8_t mem2base[MEM2_MAX_SIZE] __attribute__((section(".EXTRAM_HEAP")));

static uint32_t mem_get_blocksize(uint8_t memx)
{
    if (memx == SRAMIN)
    {
        return MEM1_BLOCK_SIZE;
    }
    return MEM2_BLOCK_SIZE;
}

static uint32_t mem_get_tablesize(uint8_t memx)
{
    if (memx == SRAMIN)
    {
        return MEM1_ALLOC_TABLE_SIZE;
    }
    return MEM2_ALLOC_TABLE_SIZE;
}

static void mem_init(uint8_t memx)
{
    uint32_t i;
    uint32_t tablesize = mem_get_tablesize(memx);
    MT_TYPE *memmap = mallco_dev.memmap[memx];

    for (i = 0; i < tablesize; i++)
    {
        memmap[i] = 0;
    }
}

static uint16_t mem_perused(uint8_t memx)
{
    uint32_t i;
    uint32_t used = 0;
    uint32_t tablesize = mem_get_tablesize(memx);
    MT_TYPE *memmap = mallco_dev.memmap[memx];

    for (i = 0; i < tablesize; i++)
    {
        if (memmap[i] != 0)
        {
            used++;
        }
    }

    return (uint16_t)((used * 100U) / tablesize);
}

static uint32_t mem_malloc(uint8_t memx, uint32_t size)
{
    uint32_t blocksize = mem_get_blocksize(memx);
    uint32_t tablesize = mem_get_tablesize(memx);
    MT_TYPE *memmap = mallco_dev.memmap[memx];
    uint32_t nblocks = (size + blocksize - 1U) / blocksize;
    uint32_t i;
    uint32_t j;
    uint32_t cmem = 0;

    if (nblocks == 0U)
    {
        return 0xFFFFFFFFU;
    }

    for (i = 0; i < tablesize; i++)
    {
        if (memmap[i] == 0U)
        {
            cmem++;
            if (cmem == nblocks)
            {
                for (j = 0; j < nblocks; j++)
                {
                    memmap[i + 1U - nblocks + j] = nblocks;
                }
                return i + 1U - nblocks;
            }
        }
        else
        {
            cmem = 0U;
        }
    }

    return 0xFFFFFFFFU;
}

static uint8_t mem_free(uint8_t memx, uint32_t offset)
{
    uint32_t i;
    uint32_t tablesize = mem_get_tablesize(memx);
    MT_TYPE *memmap = mallco_dev.memmap[memx];
    uint32_t nblocks;

    if (offset >= tablesize)
    {
        return 1U;
    }

    nblocks = memmap[offset];
    if (nblocks == 0U)
    {
        return 1U;
    }

    for (i = 0; i < nblocks; i++)
    {
        memmap[offset + i] = 0;
    }

    return 0U;
}

struct _m_mallco_dev mallco_dev =
{
    mem_init,
    mem_perused,
    {mem1base, mem2base},
    {mem1mapbase, mem2mapbase},
    {0, 0}
};

void my_mem_init(uint8_t memx)
{
    if (memx >= SRAMBANK)
    {
        return;
    }

    mallco_dev.init(memx);
    mallco_dev.memrdy[memx] = 1U;
}

uint16_t my_mem_perused(uint8_t memx)
{
    if (memx >= SRAMBANK)
    {
        return 0U;
    }

    return mallco_dev.perused(memx);
}

void my_mem_set(void *s, uint8_t c, uint32_t count)
{
    (void)memset(s, (int)c, count);
}

void my_mem_copy(void *des, void *src, uint32_t n)
{
    (void)memcpy(des, src, n);
}

void *mymalloc(uint8_t memx, uint32_t size)
{
    uint32_t offset;
    uint32_t blocksize;

    if ((memx >= SRAMBANK) || (mallco_dev.memrdy[memx] == 0U))
    {
        return NULL;
    }

    offset = mem_malloc(memx, size);
    if (offset == 0xFFFFFFFFU)
    {
        return NULL;
    }

    blocksize = mem_get_blocksize(memx);
    return (void *)&mallco_dev.membase[memx][offset * blocksize];
}

void myfree(uint8_t memx, void *ptr)
{
    uint32_t offset;
    uint32_t blocksize;

    if ((memx >= SRAMBANK) || (ptr == NULL) || (mallco_dev.memrdy[memx] == 0U))
    {
        return;
    }

    blocksize = mem_get_blocksize(memx);
    offset = ((uint32_t)ptr - (uint32_t)mallco_dev.membase[memx]) / blocksize;
    (void)mem_free(memx, offset);
}

void *myrealloc(uint8_t memx, void *ptr, uint32_t size)
{
    void *newptr;

    newptr = mymalloc(memx, size);
    if (newptr != NULL)
    {
        if (ptr != NULL)
        {
            my_mem_copy(newptr, ptr, size);
            myfree(memx, ptr);
        }
    }

    return newptr;
}
