#include "vfs.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    struct coxvbn_vfs fs;
    uint16_t node;
    size_t count;
    char buffer[32] = {0};

    coxvbn_vfs_init(&fs);
    assert(coxvbn_vfs_lookup(&fs, "/", &node) == 0 && node == 0U);
    assert(coxvbn_vfs_mkdir(&fs, "/etc") == 0);
    assert(coxvbn_vfs_mkdir(&fs, "/var") == 0);
    assert(coxvbn_vfs_create(&fs, "/etc/coxvbn.conf") == 0);
    assert(coxvbn_vfs_create(&fs, "/etc/coxvbn.conf") ==
           COXVBN_VFS_ERR_EXISTS);
    assert(coxvbn_vfs_create(&fs, "/missing/file") ==
           COXVBN_VFS_ERR_NOT_FOUND);
    assert(coxvbn_vfs_create(&fs, "/etc/../escape") ==
           COXVBN_VFS_ERR_INVALID);
    assert(coxvbn_vfs_create(&fs, "/etc//bad") ==
           COXVBN_VFS_ERR_INVALID);
    assert(coxvbn_vfs_lookup(&fs, "/etc/coxvbn.conf", &node) == 0);
    assert(coxvbn_vfs_write(&fs, "/etc/coxvbn.conf", 0U,
                            "kernel=COXVBN", 13U, &count) == 0);
    assert(count == 13U);
    assert(coxvbn_vfs_read(&fs, "/etc/coxvbn.conf", 7U,
                           buffer, sizeof(buffer), &count) == 0);
    assert(count == 6U && memcmp(buffer, "COXVBN", 6U) == 0);
    assert(coxvbn_vfs_read(&fs, "/etc", 0U, buffer, sizeof(buffer),
                           &count) == COXVBN_VFS_ERR_IS_DIRECTORY);
    assert(coxvbn_vfs_write(&fs, "/etc/coxvbn.conf", 14U,
                            "x", 1U, &count) == COXVBN_VFS_ERR_RANGE);
    assert(coxvbn_vfs_write(&fs, "/etc/coxvbn.conf", 250U,
                            "123456789", 9U, &count) ==
           COXVBN_VFS_ERR_RANGE);
    assert(coxvbn_vfs_read(&fs, "/etc/coxvbn.conf", 1000U,
                           buffer, sizeof(buffer), &count) ==
           COXVBN_VFS_ERR_RANGE);
    assert(coxvbn_vfs_lookup(&fs, "/etc/", &node) ==
           COXVBN_VFS_ERR_INVALID);
    assert(coxvbn_vfs_lookup(&fs, "/etc/coxvbn.conf/child", &node) ==
           COXVBN_VFS_ERR_NOT_DIRECTORY);
    assert(coxvbn_vfs_read(&fs, "/etc/coxvbn.conf", 0U,
                           0, 0U, &count) == 0 && count == 0U);
    return 0;
}
