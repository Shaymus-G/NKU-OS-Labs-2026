#include <dtb.h>

//lab2 challenge DTB bounds and memory discovery 2412456
/* Pure bounded parser; no unbounded strings or unaligned uint64_t loads. */
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static size_t bounded_length(const unsigned char *p, size_t max) {
    size_t i = 0;
    while (i < max && p[i] != 0) { i++; }
    return i;
}

static bool equal(const unsigned char *p, size_t length, const char *text) {
    size_t i = 0;
    while (text[i] != 0 && i < length && p[i] == (unsigned char)text[i]) { i++; }
    return i == length && text[i] == 0;
}

static uint64_t cells_value(const unsigned char *p, unsigned cells) {
    uint64_t result = be32(p);
    return cells == 2 ? (result << 32) | be32(p + 4) : result;
}

int dtb_parse_memory(const void *blob, size_t length, uint64_t *base, uint64_t *size) {
    if (blob == NULL || base == NULL || size == NULL || length < 40) {
        return DTB_INVALID;
    }
    const unsigned char *bytes = blob;
    size_t total = be32(bytes + 4), structure = be32(bytes + 8);
    size_t strings = be32(bytes + 12), strings_size = be32(bytes + 32);
    size_t structure_size = be32(bytes + 36);
    if (be32(bytes) != 0xd00dfeed || total < 40 || total > length ||
        be32(bytes + 20) < 17 || be32(bytes + 24) > 17 ||
        structure < 40 || (structure & 3) || structure > total ||
        structure_size > total - structure || strings < 40 || strings > total ||
        strings_size > total - strings || structure_size == 0 ||
        !(structure + structure_size <= strings || strings + strings_size <= structure)) {
        return DTB_INVALID;
    }
    size_t pos = structure, end = structure + structure_size;
    unsigned address_cells = 2, size_cells = 1;
    int depth = 0, memory_depth = 0;
    bool found = 0, root_seen = 0, root_closed = 0;
    uint64_t found_base = 0, found_size = 0;
    while (pos <= end && end - pos >= 4) {
        uint32_t token = be32(bytes + pos);
        pos += 4;
        if (token == 1) {
            if (root_closed || depth >= 32) { return DTB_INVALID; }
            size_t n = bounded_length(bytes + pos, end - pos);
            if (n == end - pos) { return DTB_INVALID; }
            if (depth == 0) {
                if (root_seen || n != 0) { return DTB_INVALID; }
                root_seen = 1;
            }
            if (depth == 1 && n >= 6 && equal(bytes + pos, 6, "memory") &&
                (n == 6 || bytes[pos + 6] == '@')) { memory_depth = depth + 1; }
            size_t padded = (n + 4) & ~(size_t)3;
            if (padded > end - pos) { return DTB_INVALID; }
            pos += padded;
            depth++;
        } else if (token == 2) {
            if (depth == 0) { return DTB_INVALID; }
            if (memory_depth == depth) { memory_depth = 0; }
            if (--depth == 0) { root_closed = 1; }
        } else if (token == 3) {
            if (depth == 0 || end - pos < 8) { return DTB_INVALID; }
            size_t n = be32(bytes + pos), name_offset = be32(bytes + pos + 4);
            pos += 8;
            if (n > end - pos || name_offset >= strings_size) { return DTB_INVALID; }
            const unsigned char *name = bytes + strings + name_offset;
            size_t name_length = bounded_length(name, strings_size - name_offset);
            if (name_length == strings_size - name_offset) { return DTB_INVALID; }
            if (depth == 1 && (equal(name, name_length, "#address-cells") ||
                               equal(name, name_length, "#size-cells"))) {
                if (n != 4) { return DTB_INVALID; }
                unsigned value = be32(bytes + pos);
                if (value != 1 && value != 2) { return DTB_UNSUPPORTED; }
                if (equal(name, name_length, "#address-cells")) { address_cells = value; }
                else { size_cells = value; }
            } else if (depth == memory_depth && memory_depth != 0 &&
                       equal(name, name_length, "reg")) {
                size_t tuple = (address_cells + size_cells) * 4;
                if (found || (n > tuple && n % tuple == 0)) { return DTB_UNSUPPORTED; }
                if (n != tuple) { return DTB_INVALID; }
                found_base = cells_value(bytes + pos, address_cells);
                found_size = cells_value(bytes + pos + address_cells * 4, size_cells);
                if (found_size == 0 || found_base + found_size < found_base) {
                    return DTB_INVALID;
                }
                found = 1;
            }
            size_t padded = (n + 3) & ~(size_t)3;
            if (padded > end - pos) { return DTB_INVALID; }
            pos += padded;
        } else if (token == 4) {
            continue;
        } else if (token == 9) {
            if (!root_seen || !root_closed || depth != 0) { return DTB_INVALID; }
            if (!found) { return DTB_NO_MEMORY; }
            *base = found_base;
            *size = found_size;
            return 0;
        } else {
            return DTB_INVALID;
        }
    }
    return DTB_INVALID;
}
