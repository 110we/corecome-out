#ifndef COXVBN_VFS_H
#define COXVBN_VFS_H

#include <stddef.h>
#include <stdint.h>

#define COXVBN_VFS_NODE_CAPACITY 48U
#define COXVBN_VFS_NAME_MAX 31U
#define COXVBN_VFS_PATH_MAX 255U
#define COXVBN_VFS_FILE_MAX 256U
#define COXVBN_VFS_INVALID_NODE UINT16_MAX

enum coxvbn_vfs_node_type {
    COXVBN_VFS_NODE_UNUSED = 0,
    COXVBN_VFS_NODE_DIRECTORY,
    COXVBN_VFS_NODE_FILE
};

enum coxvbn_vfs_error {
    COXVBN_VFS_OK = 0,
    COXVBN_VFS_ERR_INVALID = -1,
    COXVBN_VFS_ERR_NOT_FOUND = -2,
    COXVBN_VFS_ERR_EXISTS = -3,
    COXVBN_VFS_ERR_NOT_DIRECTORY = -4,
    COXVBN_VFS_ERR_IS_DIRECTORY = -5,
    COXVBN_VFS_ERR_NO_SPACE = -6,
    COXVBN_VFS_ERR_RANGE = -7
};

struct coxvbn_vfs_node {
    char name[COXVBN_VFS_NAME_MAX + 1U];
    uint16_t parent;
    uint16_t generation;
    uint16_t size;
    enum coxvbn_vfs_node_type type;
    uint8_t data[COXVBN_VFS_FILE_MAX];
};

struct coxvbn_vfs {
    struct coxvbn_vfs_node nodes[COXVBN_VFS_NODE_CAPACITY];
    uint32_t generation;
    uint32_t node_count;
};

void coxvbn_vfs_init(struct coxvbn_vfs *fs);
int coxvbn_vfs_mkdir(struct coxvbn_vfs *fs, const char *path);
int coxvbn_vfs_create(struct coxvbn_vfs *fs, const char *path);
int coxvbn_vfs_lookup(const struct coxvbn_vfs *fs, const char *path,
                      uint16_t *node_out);
int coxvbn_vfs_read(const struct coxvbn_vfs *fs, const char *path,
                    size_t offset, void *buffer, size_t length,
                    size_t *read_out);
int coxvbn_vfs_write(struct coxvbn_vfs *fs, const char *path,
                     size_t offset, const void *buffer, size_t length,
                     size_t *written_out);

#endif
