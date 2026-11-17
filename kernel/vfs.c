#include "vfs.h"

static size_t vfs_length(const char *s, size_t limit)
{
    size_t n = 0U;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

static void vfs_copy(char *dst, const char *src, size_t length)
{
    size_t i;
    for (i = 0U; i < length; ++i) dst[i] = src[i];
}

static void vfs_zero(void *memory, size_t length)
{
    uint8_t *p = (uint8_t *)memory;
    size_t i;
    for (i = 0U; i < length; ++i) p[i] = 0U;
}

static int vfs_compare(const char *a, const char *b)
{
    size_t i = 0U;
    while (a[i] != '\0' && a[i] == b[i]) ++i;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

struct path_component {
    char name[COXVBN_VFS_NAME_MAX + 1U];
    const char *next;
};

static int parse_component(const char *path, struct path_component *part)
{
    size_t length = 0U;
    if (path == 0 || part == 0 || *path != '/') return COXVBN_VFS_ERR_INVALID;
    if (path[1] == '\0') {
        part->name[0] = '\0';
        part->next = path + 1;
        return 0;
    }
    path++;
    while (path[length] != '\0' && path[length] != '/') {
        if (length >= COXVBN_VFS_NAME_MAX) return COXVBN_VFS_ERR_RANGE;
        part->name[length] = path[length];
        ++length;
    }
    if (length == 0U || (length == 1U && part->name[0] == '.') ||
        (length == 2U && part->name[0] == '.' && part->name[1] == '.'))
        return COXVBN_VFS_ERR_INVALID;
    part->name[length] = '\0';
    part->next = path + length;
    return 0;
}

static int find_child(const struct coxvbn_vfs *fs, uint16_t parent,
                      const char *name, uint16_t *out)
{
    uint32_t i;
    for (i = 1U; i < COXVBN_VFS_NODE_CAPACITY; ++i) {
        const struct coxvbn_vfs_node *node = &fs->nodes[i];
        if (node->type != COXVBN_VFS_NODE_UNUSED &&
            node->parent == parent && vfs_compare(node->name, name) == 0) {
            *out = (uint16_t)i;
            return 0;
        }
    }
    return COXVBN_VFS_ERR_NOT_FOUND;
}

static int resolve_parent(const struct coxvbn_vfs *fs, const char *path,
                          uint16_t *parent_out, char *name_out)
{
    struct path_component part;
    const char *cursor = path;
    uint16_t current = 0U;
    int rc;

    if (fs == 0 || path == 0 || parent_out == 0 || name_out == 0 ||
        path[0] != '/') return COXVBN_VFS_ERR_INVALID;
    if (vfs_length(path, COXVBN_VFS_PATH_MAX + 1U) > COXVBN_VFS_PATH_MAX ||
        vfs_compare(path, "/") == 0)
        return COXVBN_VFS_ERR_INVALID;

    for (;;) {
        rc = parse_component(cursor, &part);
        if (rc != 0) return rc;
        if (part.name[0] == '\0') return COXVBN_VFS_ERR_INVALID;
        cursor = part.next;
        if (*cursor == '\0') {
            size_t n = vfs_length(part.name, COXVBN_VFS_NAME_MAX + 1U);
            vfs_copy(name_out, part.name, n + 1U);
            *parent_out = current;
            return 0;
        }
        if (*cursor != '/') return COXVBN_VFS_ERR_INVALID;
        ++cursor;
        if (*cursor == '\0') return COXVBN_VFS_ERR_INVALID;
        {
            uint16_t child;
            rc = find_child(fs, current, part.name, &child);
            if (rc != 0) return rc;
            if (fs->nodes[child].type != COXVBN_VFS_NODE_DIRECTORY)
                return COXVBN_VFS_ERR_NOT_DIRECTORY;
            current = child;
        }
        cursor--;
    }
}

static int add_node(struct coxvbn_vfs *fs, const char *path,
                    enum coxvbn_vfs_node_type type)
{
    uint16_t parent;
    uint32_t i;
    char name[COXVBN_VFS_NAME_MAX + 1U];
    int rc = resolve_parent(fs, path, &parent, name);
    if (rc != 0) return rc;
    {
        uint16_t existing;
        if (find_child(fs, parent, name, &existing) == 0)
            return COXVBN_VFS_ERR_EXISTS;
    }
    for (i = 1U; i < COXVBN_VFS_NODE_CAPACITY; ++i) {
        struct coxvbn_vfs_node *node = &fs->nodes[i];
        if (node->type == COXVBN_VFS_NODE_UNUSED) {
            size_t n = vfs_length(name, COXVBN_VFS_NAME_MAX + 1U);
            vfs_zero(node, sizeof(*node));
            vfs_copy(node->name, name, n + 1U);
            node->parent = parent;
            node->generation = (uint16_t)++fs->generation;
            if (node->generation == 0U)
                node->generation = (uint16_t)++fs->generation;
            node->type = type;
            ++fs->node_count;
            return 0;
        }
    }
    return COXVBN_VFS_ERR_NO_SPACE;
}

void coxvbn_vfs_init(struct coxvbn_vfs *fs)
{
    if (fs == 0) return;
    vfs_zero(fs, sizeof(*fs));
    fs->nodes[0].name[0] = '/';
    fs->nodes[0].name[1] = '\0';
    fs->nodes[0].parent = 0U;
    fs->nodes[0].generation = 1U;
    fs->nodes[0].type = COXVBN_VFS_NODE_DIRECTORY;
    fs->generation = 1U;
    fs->node_count = 1U;
}

int coxvbn_vfs_mkdir(struct coxvbn_vfs *fs, const char *path)
{
    if (fs == 0) return COXVBN_VFS_ERR_INVALID;
    return add_node(fs, path, COXVBN_VFS_NODE_DIRECTORY);
}

int coxvbn_vfs_create(struct coxvbn_vfs *fs, const char *path)
{
    if (fs == 0) return COXVBN_VFS_ERR_INVALID;
    return add_node(fs, path, COXVBN_VFS_NODE_FILE);
}

int coxvbn_vfs_lookup(const struct coxvbn_vfs *fs, const char *path,
                      uint16_t *node_out)
{
    struct path_component part;
    const char *cursor = path;
    uint16_t current = 0U;
    int rc;
    if (fs == 0 || path == 0 || node_out == 0 || path[0] != '/' ||
        vfs_length(path, COXVBN_VFS_PATH_MAX + 1U) > COXVBN_VFS_PATH_MAX)
        return COXVBN_VFS_ERR_INVALID;
    if (vfs_compare(path, "/") == 0) {
        *node_out = 0U;
        return 0;
    }
    for (;;) {
        uint16_t child;
        rc = parse_component(cursor, &part);
        if (rc != 0 || part.name[0] == '\0')
            return rc != 0 ? rc : COXVBN_VFS_ERR_INVALID;
        rc = find_child(fs, current, part.name, &child);
        if (rc != 0) return rc;
        cursor = part.next;
        if (*cursor == '\0') {
            *node_out = child;
            return 0;
        }
        if (*cursor != '/') return COXVBN_VFS_ERR_INVALID;
        if (fs->nodes[child].type != COXVBN_VFS_NODE_DIRECTORY)
            return COXVBN_VFS_ERR_NOT_DIRECTORY;
        ++cursor;
        if (*cursor == '\0') return COXVBN_VFS_ERR_INVALID;
        cursor--;
        current = child;
    }
}

int coxvbn_vfs_read(const struct coxvbn_vfs *fs, const char *path,
                    size_t offset, void *buffer, size_t length,
                    size_t *read_out)
{
    uint16_t index;
    const struct coxvbn_vfs_node *node;
    size_t amount;
    int rc;
    if (read_out == 0 || (buffer == 0 && length != 0U))
        return COXVBN_VFS_ERR_INVALID;
    *read_out = 0U;
    rc = coxvbn_vfs_lookup(fs, path, &index);
    if (rc != 0) return rc;
    node = &fs->nodes[index];
    if (node->type == COXVBN_VFS_NODE_DIRECTORY)
        return COXVBN_VFS_ERR_IS_DIRECTORY;
    if (offset > node->size) return COXVBN_VFS_ERR_RANGE;
    amount = (size_t)node->size - offset;
    if (amount > length) amount = length;
    if (amount != 0U) vfs_copy((char *)buffer, (const char *)node->data + offset, amount);
    *read_out = amount;
    return 0;
}

int coxvbn_vfs_write(struct coxvbn_vfs *fs, const char *path,
                     size_t offset, const void *buffer, size_t length,
                     size_t *written_out)
{
    uint16_t index;
    struct coxvbn_vfs_node *node;
    int rc;
    if (written_out == 0 || (buffer == 0 && length != 0U))
        return COXVBN_VFS_ERR_INVALID;
    *written_out = 0U;
    rc = coxvbn_vfs_lookup(fs, path, &index);
    if (rc != 0) return rc;
    node = &fs->nodes[index];
    if (node->type == COXVBN_VFS_NODE_DIRECTORY)
        return COXVBN_VFS_ERR_IS_DIRECTORY;
    if (offset > node->size || offset > COXVBN_VFS_FILE_MAX ||
        length > COXVBN_VFS_FILE_MAX - offset)
        return COXVBN_VFS_ERR_RANGE;
    if (length != 0U) vfs_copy((char *)node->data + offset, (const char *)buffer, length);
    if (offset + length > node->size)
        node->size = (uint16_t)(offset + length);
    *written_out = length;
    return 0;
}
