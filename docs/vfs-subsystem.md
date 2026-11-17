# COXVBN VFS foundation

The current VFS slice is a bounded in-memory filesystem core intended to establish
path and file-operation contracts before block-device and persistent-filesystem
work begins.

## Current contract

- Fixed capacity: 48 nodes, 31-byte names, 255-byte paths, and 256 bytes per file.
- Root is created as directory node zero.
- Directories must be created explicitly; parent directories are not synthesized.
- Absolute paths only. Empty components, trailing separators, `.`, and `..`
  are rejected rather than normalized.
- Files support bounded reads and writes. Writes cannot create sparse gaps.
- Directories cannot be read or written as files.
- Node and file storage are statically embedded in the caller-owned VFS object;
  there is no allocator dependency.

## Deliberate limitations

This is not a POSIX VFS, mount manager, permission model, symlink implementation,
or persistent filesystem. There is no unlink/rename, open-handle lifetime,
concurrent access control, block-device interface, journaling, crash consistency,
or boot-time mount. The fixed file-size and node limits are bring-up constraints,
not a general storage design. Host tests validate path handling and bounded
read/write behavior only.
