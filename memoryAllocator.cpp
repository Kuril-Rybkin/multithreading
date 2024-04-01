#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <cmath>
using namespace std;
#endif /* __PROGTEST__ */

#define SIZE sizeof(TMetadata)
#define MIN_SIZE (SIZE + 1)
#define MAX_2(arg) (int)floor(log2(arg)) /// Biggest power of 2 smaller than arg
#define MIN_2(arg) (int)ceil(log2(arg)) /// Smallest power of 2 greater than arg

struct TMetadata {
    TMetadata() {}
    TMetadata(int size) {
        order = MAX_2(size);
    }

    uint8_t order;
    TMetadata * next = nullptr;
    TMetadata * prev = nullptr;
    bool used = false;
};

const int g_avail_size = 40;
TMetadata * g_avail[g_avail_size];
size_t g_mem_pool = 0;
int g_mem_size = 0;
int g_alloc_num = 0;
bool g_init = false;

size_t get_buddy(size_t address) {
    int power = ((TMetadata *)address)->order;
    address -= g_mem_pool;
    size_t ret;

    if (address % (int)pow(2, power + 1) == 0) {
        ret = address + (int)pow(2, power);
    }
    else {
        ret = address - (int)pow(2, power);
    }

    return g_mem_pool + ret;
}

bool out_of_bounds(size_t loc) {
    return loc < g_mem_pool || loc > (g_mem_pool + g_mem_size);
}

void   HeapInit    ( void * memPool, int memSize )
{
    /// Heap already initialized or no space for metadata struct
    if (g_init || (size_t)memSize < MIN_SIZE) {
        return;
    }

    /// Save memory pool info
    g_mem_pool = (size_t)memPool;
    g_mem_size = memSize;

    /// Initialize g_avail
    for (int i = 0; i < g_avail_size; i++) {
        g_avail[i] = nullptr;
    }

    TMetadata init(g_mem_size); /// Create metadata structure
    memcpy((void *)g_mem_pool, &init, SIZE); /// Copy init into heap

    int remainder = g_mem_size - pow(2, init.order); /// Calculate wasted space
    if (remainder > (int)MIN_SIZE) { /// If remainder is big enough
        TMetadata init2(remainder); /// Initialize extra data structure
        memcpy((void *)(g_mem_pool + (int)pow(2, init.order)), &init2, SIZE);
        g_avail[init2.order] = (TMetadata *)(g_mem_pool + (int)pow(2, init.order));
    }

    g_avail[((TMetadata *)g_mem_pool)->order] = (TMetadata *)g_mem_pool; /// Insert the new metadata struct into g_avail list
    g_init = true;
}
void * HeapAlloc   ( int    size )
{
    /// If heap wasnt initialized yet
    if (!g_init) {
        return nullptr;
    }

    int idx = -1;
    size += SIZE; /// Make size of new block account for metadata structure

    for (int i = MIN_2(size); i < g_avail_size; i++) {
        if (g_avail[i]) {
            idx = i;
            break;
        }
    }

    /// No block big enough was found
    if (idx == -1) {
        return nullptr;
    }

    /// Save free block and mark it as used
    TMetadata * found_block = g_avail[idx];
    found_block->used = true;

    /// Remove metadata structure from linked list
    g_avail[idx] = found_block->next;
    if (g_avail[idx]) {
        g_avail[idx]->prev = nullptr;
    }

    /// If free block is more than twice as big as requested memory block, split it
    while ((int)pow(2, found_block->order) >= size * 2) {
        found_block->order--; /// Decrement power of 2 to halve the block

        TMetadata new_buddy(pow(2, found_block->order)); /// Create new buddy block

        size_t new_buddy_location = get_buddy((size_t)found_block); /// Account size of block
        memcpy((void *)new_buddy_location, &new_buddy, SIZE); /// Copy the new buddy block back into memory

        g_avail[found_block->order] = (TMetadata *)new_buddy_location;
    }

    g_alloc_num++;
    return (void *) ((size_t)found_block + SIZE);
}
bool   HeapFree    ( void * blk )
{
    if (!g_init || out_of_bounds((size_t)blk)) {
        return false;
    }

    TMetadata * loc = (TMetadata *)((size_t)blk - SIZE);

    if (!loc->used) {
        return false;
    }

    loc->used = false;
    TMetadata * buddy = (TMetadata *)(get_buddy((size_t)loc));

    while (!out_of_bounds((size_t)buddy) && !buddy->used && buddy->order == loc->order) {
        /// Remove buddy from linked list
        if (buddy->prev == nullptr) {
            g_avail[buddy->order] = buddy->next;
        }
        else {
            buddy->prev->next = buddy->next;
        }

        if (buddy->next != nullptr) {
            buddy->next->prev = buddy->prev;
        }

        /// Join buddy with loc
        if ((size_t)loc > (size_t)buddy) {
            swap(loc, buddy);
        }

        loc->order++;

        buddy = (TMetadata *) get_buddy((size_t)loc);
    }

    /// Re insert loc into g_avail
    TMetadata * curr = g_avail[loc->order];
    TMetadata * prev = nullptr;
    while (curr && loc > curr) {
        prev = curr;
        curr = curr->next;
    }

    loc->prev = prev;
    loc->next = curr;

    if (curr) {
        curr->prev = loc;
    }
    if (prev) {
        prev->next = loc;
    }
    else {
        g_avail[loc->order] = loc;
    }

    g_alloc_num--;

    return true;
}
void   HeapDone    ( int  * pendingBlk )
{
    *pendingBlk = g_alloc_num;
    memset((void *)g_mem_pool, 0, g_mem_size);
    g_mem_pool = 0;
    g_mem_size = 0;
    g_alloc_num = 0;
    g_init = false;
}

#ifndef __PROGTEST__
int main ( void )
{
    uint8_t       * p0, *p1, *p2, *p3, *p4;
    int             pendingBlk;
    static uint8_t  memPool[3 * 1048576];

    HeapInit ( memPool, 2097152 );
    assert ( ( p0 = (uint8_t*) HeapAlloc ( 512000 ) ) != NULL );
    memset ( p0, 0, 512000 );
    assert ( ( p1 = (uint8_t*) HeapAlloc ( 511000 ) ) != NULL );
    memset ( p1, 0, 511000 );
    assert ( ( p2 = (uint8_t*) HeapAlloc ( 26000 ) ) != NULL );
    memset ( p2, 0, 26000 );
    HeapDone ( &pendingBlk );
    assert ( pendingBlk == 3 );


    HeapInit ( memPool, 2097152 );
    assert ( ( p0 = (uint8_t*) HeapAlloc ( 1000000 ) ) != NULL );
    memset ( p0, 0, 1000000 );
    assert ( ( p1 = (uint8_t*) HeapAlloc ( 250000 ) ) != NULL );
    memset ( p1, 0, 250000 );
    assert ( ( p2 = (uint8_t*) HeapAlloc ( 250000 ) ) != NULL );
    memset ( p2, 0, 250000 );
    assert ( ( p3 = (uint8_t*) HeapAlloc ( 250000 ) ) != NULL );
    memset ( p3, 0, 250000 );
    assert ( ( p4 = (uint8_t*) HeapAlloc ( 50000 ) ) != NULL );
    memset ( p4, 0, 50000 );
    assert ( HeapFree ( p2 ) );
    assert ( HeapFree ( p4 ) );
    assert ( HeapFree ( p3 ) );
    assert ( HeapFree ( p1 ) );
    assert ( ( p1 = (uint8_t*) HeapAlloc ( 500000 ) ) != NULL );
    memset ( p1, 0, 500000 );
    assert ( HeapFree ( p0 ) );
    assert ( HeapFree ( p1 ) );
    HeapDone ( &pendingBlk );
    assert ( pendingBlk == 0 );


    HeapInit ( memPool, 2359296 );
    assert ( ( p0 = (uint8_t*) HeapAlloc ( 1000000 ) ) != NULL );
    memset ( p0, 0, 1000000 );
    assert ( ( p1 = (uint8_t*) HeapAlloc ( 500000 ) ) != NULL );
    memset ( p1, 0, 500000 );
    assert ( ( p2 = (uint8_t*) HeapAlloc ( 500000 ) ) != NULL );
    memset ( p2, 0, 500000 );
    assert ( ( p3 = (uint8_t*) HeapAlloc ( 500000 ) ) == NULL );
    assert ( HeapFree ( p2 ) );
    assert ( ( p2 = (uint8_t*) HeapAlloc ( 300000 ) ) != NULL );
    memset ( p2, 0, 300000 );
    assert ( HeapFree ( p0 ) );
    assert ( HeapFree ( p1 ) );
    HeapDone ( &pendingBlk );
    assert ( pendingBlk == 1 );


    HeapInit ( memPool, 2359296 );
    assert ( ( p0 = (uint8_t*) HeapAlloc ( 1000000 ) ) != NULL );
    memset ( p0, 0, 1000000 );
    assert ( ! HeapFree ( p0 + 1000 ) );
    HeapDone ( &pendingBlk );
    assert ( pendingBlk == 1 );


    HeapInit(memPool, 1024);
    p0 = (uint8_t *)HeapAlloc(400);
    p1 = (uint8_t *) HeapAlloc(150);
    p2 = (uint8_t *) HeapAlloc(150);
    HeapFree(p0);
    HeapFree(p1);
    HeapFree(p2);
    p0 = (uint8_t *) HeapAlloc(900);
    assert (p0 != NULL);
    HeapDone(&pendingBlk);

    HeapInit(memPool, 262143);
    uint8_t * array[100000];
    int idx = 0;
    p0 = (uint8_t *)HeapAlloc(23);
    while (p0) {
        array[idx] = p0;
        p0 = (uint8_t*) HeapAlloc(23);
        idx++;
    }
    printf("MANAGED TO INSERT %d ELEMENTS\n", idx);

    for (int i = 0; i < idx; i++) {
        if (!HeapFree(array[i])) {
            printf("FAILED TO FREE AT INDEX %d\n", i);
        }
        else {
//            printf("FREED AT %d\n", i);
        }
    }

    printf("Success\n");

    return 0;
}
#endif /* __PROGTEST__ */

