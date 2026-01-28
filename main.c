#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ==========================================
   SELF-CONTAINED DEFINITIONS (Replaces p4machine.h)
   ========================================== */

// Define heap size (e.g., 64KB)
#define P4HEAP_TOTAL_SIZE 65536 

// Define layout: Metadata starts at 0, Dynamic data starts at 4096 (arbitrary buffer)
#define P4HEAP_ALLOC_START 0
#define P4HEAP_DYNAMIC_START 4096
#define P4HEAP_HIGH_ADDR (P4HEAP_TOTAL_SIZE - 1)

// The main heap structure
typedef struct {
    unsigned char memory[P4HEAP_TOTAL_SIZE];
} p4heap;

// Structure for the free list (used in infer_free_list)
typedef struct free_list {
    uint16_t start;
    uint16_t size;
    struct free_list *next;
} free_list;

// Structure for passing stack roots to the GC
typedef struct address_list {
    void *addr;
    struct address_list *next;
} address_list;

/* ==========================================
   IMPLEMENTATION
   ========================================== */

//helper functions 
static uint16_t read_u16(unsigned char *base)
{
  return ((uint16_t)base[0] << 8) | (uint16_t)base[1];
}

static void write_u16(unsigned char *base, uint16_t value)
{
  base[0] = (unsigned char)((value >> 8) & 0xff);
  base[1] = (unsigned char)(value & 0xff);
}

static unsigned char *allocation_start(p4heap *h)
{
  return h->memory + P4HEAP_ALLOC_START;
}

static unsigned char *dynamic_start(p4heap *h)
{
  return h->memory + P4HEAP_DYNAMIC_START;
}

static uint16_t heap_offset(p4heap *h, void *addr)
{
  unsigned char *bytes = (unsigned char *)addr;
  return (uint16_t)(bytes - h->memory);
}

static void clear_object(p4heap *h, uint16_t offset, uint16_t size)
{
  memset(h->memory + offset, 0, size);
}

static int is_pointer_in_heap(p4heap *h, void *addr)
{
  unsigned char *bytes = (unsigned char *)addr;
  return bytes >= dynamic_start(h) && bytes <= h->memory + P4HEAP_HIGH_ADDR;
}

//Returns the pointer to the allocation record, it accounts for the offset or we return NULL
static unsigned char *find_record(p4heap *h, uint16_t offset)
{
  unsigned char *cursor = allocation_start(h);
  while (read_u16(cursor) != 0) {
    uint16_t current_offset = read_u16(cursor);
    if (current_offset == offset) {
      return cursor;
    }
    cursor += 5;
  }
  return NULL;
}

// count allocations in the heap
static uint8_t count_records(p4heap *h)
{
  uint8_t count = 0;
  unsigned char *cursor = allocation_start(h);
  while (read_u16(cursor) != 0) 
  {
    count++;
    cursor += 5;
  }
  return count;
}

static int record_index_for_offset(p4heap *h, uint16_t target_offset)
{
  unsigned char *cursor = allocation_start(h);
  int index = 0;
  while (read_u16(cursor) != 0) 
  {
    if (read_u16(cursor) == target_offset) 
    {
      return index;
    }
    index++;
    cursor += 5;
  }
  return -1;
}

static void mark_object(p4heap *h, uint8_t *marks, uint16_t offset)
{
  unsigned char *alloc_base = allocation_start(h);
  int idx = record_index_for_offset(h, offset);
  if (idx < 0 || marks[idx]) 
  {
    return;
  }
  marks[idx] = 1;

  unsigned char *rec = alloc_base + idx * 5;
  uint16_t obj_size = read_u16(rec + 2);
  uint8_t num_patterns = rec[4];
  void **obj = (void **)(h->memory + offset);
  uint16_t max_pattern_bytes = (uint16_t)(num_patterns * sizeof(void *));
  if (max_pattern_bytes > obj_size) 
  {
    max_pattern_bytes = obj_size;
  }

  for (uint16_t i = 0; i + sizeof(void *) <= max_pattern_bytes; i += sizeof(void *)) {
    void *possible = obj[i / sizeof(void *)];
    if (possible != NULL && is_pointer_in_heap(h, possible)) 
    {
      uint16_t child_offset = heap_offset(h, possible);
      int child_index = record_index_for_offset(h, child_offset);
      if (child_index >= 0) 
      {
        mark_object(h, marks, child_offset);
      }
    }
  }
}

p4heap *p4heap_create()
{
  p4heap *heap = (p4heap *)malloc(sizeof(p4heap));
  if (heap == NULL) 
  {
    return NULL;
  }
  memset(heap->memory, 0, P4HEAP_TOTAL_SIZE);
  return heap;
}

uint8_t p4heap_num_allocs(p4heap *h)
{
  if (h == NULL) 
  {
    return 0;
  }
  return count_records(h);
}

void *p4malloc(p4heap *h, uint16_t num_bytes, uint8_t num_pointers)
{
  if (h == NULL) 
  {
    return NULL;
  }

  if (num_bytes < 8) 
  {
    num_bytes = 8;
  }
  if (num_bytes % 8 != 0) 
  {
    num_bytes = (uint16_t)(num_bytes + (8 - (num_bytes % 8)));
  }

  unsigned char *alloc_base = allocation_start(h);
  unsigned char *cursor = alloc_base;
  uint16_t previous_end = P4HEAP_DYNAMIC_START;

  while (read_u16(cursor) != 0) 
  {
    uint16_t existing_offset = read_u16(cursor);
    uint16_t existing_size = read_u16(cursor + 2);
    if (existing_offset - previous_end >= num_bytes) 
    {
      break;
    }
    previous_end = (uint16_t)(existing_offset + existing_size);
    cursor += 5;
  }

  if (P4HEAP_TOTAL_SIZE - previous_end < num_bytes) 
  {
    return NULL;
  }
  uint16_t new_offset = previous_end;

  //shift 
  unsigned char *shift_from = cursor;
  unsigned char *end_marker = alloc_base;
  while (read_u16(end_marker) != 0) 
  {
    end_marker += 5;
  }
  memmove(shift_from + 5, shift_from, (size_t)(end_marker - shift_from));

  write_u16(cursor, new_offset);
  write_u16(cursor + 2, num_bytes);
  cursor[4] = num_pointers;

  return (void *)(h->memory + new_offset);
}

void *p4calloc(p4heap *h, uint16_t count, uint16_t size, uint8_t num_pointers)
{
  uint32_t total = (uint32_t)count * (uint32_t)size;
  void *addr = p4malloc(h, (uint16_t)total, num_pointers);
  if (addr != NULL) 
  {
    memset(addr, 0, total);
  }
  return addr;
}

free_list *infer_free_list(p4heap *h)
{
  if (h == NULL) 
  {
    return NULL;
  }

  free_list *head = NULL;
  free_list **tail = &head;

  uint16_t current_start = P4HEAP_DYNAMIC_START;
  unsigned char *cursor = allocation_start(h);

  while (read_u16(cursor) != 0) 
  {
    uint16_t alloc_offset = read_u16(cursor);
    uint16_t alloc_size = read_u16(cursor + 2);
    if (alloc_offset > current_start) 
    {
      free_list *node = (free_list *)malloc(sizeof(free_list));
      node->start = current_start;
      node->size = (uint16_t)(alloc_offset - current_start);
      node->next = NULL;
      *tail = node;
      tail = &node->next;
    }
    current_start = (uint16_t)(alloc_offset + alloc_size);
    cursor += 5;
  }

  if (current_start <= P4HEAP_HIGH_ADDR) {
    free_list *node = (free_list *)malloc(sizeof(free_list));
    node->start = current_start;
    node->size = (uint16_t)(P4HEAP_TOTAL_SIZE - current_start);
    node->next = NULL;
    *tail = node;
  }

  return head;
}

void p4free(p4heap *h, void *addr)
{
  if (h == NULL || addr == NULL) 
  {
    return;
  }

  uint16_t offset = heap_offset(h, addr);
  unsigned char *record = find_record(h, offset);
  if (record == NULL) 
  {
    return;
  }

  uint16_t size = read_u16(record + 2);
  clear_object(h, offset, size);

  unsigned char *end_marker = record;
  while (read_u16(end_marker) != 0) 
  {
    end_marker += 5;
  }
  memmove(record, record + 5, (size_t)(end_marker - (record + 5)) + 5);
}

uint8_t p4gc(p4heap *h, address_list *live_roots)
{
  if (h == NULL) {
    return 0;
  }

  uint8_t alloc_count = count_records(h);
  if (alloc_count == 0) 
  {
    return 0;
  }

  uint8_t *marks = (uint8_t *)calloc(alloc_count, sizeof(uint8_t));

  address_list *root = live_roots;
  while (root != NULL) {
    if (root->addr != NULL && is_pointer_in_heap(h, root->addr)) {
      uint16_t off = heap_offset(h, root->addr);
      mark_object(h, marks, off);
    }
    root = root->next;
  }

  uint8_t freed = 0;
  int index = 0;
  unsigned char *cursor = allocation_start(h);
  while (read_u16(cursor) != 0) {
    if (!marks[index]) 
    {
      uint16_t obj_offset = read_u16(cursor);
      p4free(h, h->memory + obj_offset);
      freed++;
      cursor = allocation_start(h);
      index = 0;
      continue;
    }
    index++;
    cursor += 5;
  }
  free(marks);
  return freed;
}

void allocation_list_show(p4heap *h)
{
  if (h == NULL) 
  {
    return;
  }

  unsigned char *cursor = allocation_start(h);
  printf("allocation list:\n");
  while (read_u16(cursor) != 0) {
    uint16_t offset = read_u16(cursor);
    uint16_t size = read_u16(cursor + 2);
    uint8_t pointers = cursor[4];
    printf("offset %u size %u pointers %u\n", offset, size, pointers);
    cursor += 5;
  }
}

// Small main function for testing (Optional - remove if you just want a library)
int main() {
    p4heap *h = p4heap_create();
    printf("Heap initialized. Ready for allocation.\n");
    return 0;
}
