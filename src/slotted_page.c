#include "slotted_page.h"
#include <string.h>

// Simple CRC32 implementation (IEEE 802.3)
static uint32_t crc32_for_byte(uint32_t r) {
    for (int j = 0; j < 8; ++j)
        r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
    return r ^ (uint32_t)0xFF000000L;
}

static uint32_t calculate_crc32(const void *data, size_t n_bytes) {
    uint32_t crc = 0;
    static uint32_t table[256];
    static bool initialized = false;
    
    if (!initialized) {
        for (size_t i = 0; i < 256; ++i)
            table[i] = crc32_for_byte(i);
        initialized = true;
    }
    
    const uint8_t *ptr = (const uint8_t *)data;
    for (size_t i = 0; i < n_bytes; ++i) {
        crc = table[(uint8_t)crc ^ ptr[i]] ^ crc >> 8;
    }
    return ~crc;
}

void slotted_page_init(char *page_buf, uint32_t page_id, PageType type) {
    memset(page_buf, 0, PAGE_SIZE);
    PageHeader *hdr = (PageHeader *)page_buf;
    hdr->lsn = 0;
    hdr->page_id = page_id;
    hdr->checksum = 0;
    hdr->page_type = (uint16_t)type;
    hdr->slot_count = 0;
    hdr->free_space_lower = sizeof(PageHeader);
    hdr->free_space_upper = PAGE_SIZE;
}

void slotted_page_compute_checksum(char *page_buf) {
    PageHeader *hdr = (PageHeader *)page_buf;
    hdr->checksum = 0; // zero out before compute
    hdr->checksum = calculate_crc32(page_buf, PAGE_SIZE);
}

bool slotted_page_verify_checksum(const char *page_buf) {
    PageHeader *hdr = (PageHeader *)page_buf;
    uint32_t stored_checksum = hdr->checksum;
    
    char temp_page[PAGE_SIZE];
    memcpy(temp_page, page_buf, PAGE_SIZE);
    PageHeader *temp_hdr = (PageHeader *)temp_page;
    temp_hdr->checksum = 0;
    
    uint32_t calculated = calculate_crc32(temp_page, PAGE_SIZE);
    return calculated == stored_checksum;
}

bool slotted_page_insert_tuple(char *page_buf, const void *tuple_data, uint16_t tuple_len) {
    PageHeader *hdr = (PageHeader *)page_buf;
    uint32_t needed_space = sizeof(Slot) + tuple_len;

    if ((hdr->free_space_upper - hdr->free_space_lower) < needed_space) {
        return false; // Out of free space in 4KB page
    }

    // Allocate tuple at top of free space (growing downward)
    hdr->free_space_upper -= tuple_len;
    uint16_t tuple_offset = hdr->free_space_upper;
    memcpy(page_buf + tuple_offset, tuple_data, tuple_len);

    // Allocate slot array entry at lower free space (growing upward)
    Slot *slot = (Slot *)(page_buf + hdr->free_space_lower);
    slot->offset = tuple_offset;
    slot->length = tuple_len;

    hdr->free_space_lower += sizeof(Slot);
    hdr->slot_count++;

    return true;
}

const char* slotted_page_get_tuple(const char *page_buf, uint16_t slot_idx, uint16_t *out_len) {
    const PageHeader *hdr = (const PageHeader *)page_buf;
    if (slot_idx >= hdr->slot_count) {
        return NULL;
    }

    const Slot *slots = (const Slot *)(page_buf + sizeof(PageHeader));
    const Slot *target_slot = &slots[slot_idx];
    
    if (out_len) {
        *out_len = target_slot->length;
    }

    return page_buf + target_slot->offset;
}
