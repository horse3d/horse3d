#ifndef HORSE3D_VFS_H_
#define HORSE3D_VFS_H_


#define VFSFLAG_NO_REALDISK_ACCESS 1
#define VFSFLAG_NO_VIRTUALPAK_ACCESS 2

int vfs_AddPak(const char *path);

int vfs_Exists(const char *path, int *result, int flags);

int vfs_IsDirectory(const char *path, int *result, int flags);

int vfs_Size(const char *path, uint64_t *result, int flags);

char *vfs_NormalizePath(const char *path);

char *vfs_AbsolutePath(const char *path);

int vfs_GetBytes(
    const char *path, uint64_t offset,
    uint64_t bytesamount, char *buffer,
    int flags
);

void vfs_Init(const char *argv0);

typedef struct VFSFILE VFSFILE;

VFSFILE *vfs_fopen(const char *path, const char *mode, int flags);

int vfs_feof(VFSFILE *f);

size_t vfs_fread(
    char *buffer, int bytes, int numn, VFSFILE *f
);

size_t vfs_freadline(VFSFILE *f, char *buf, size_t bufsize);

int64_t vfs_ftell(VFSFILE *f);

void vfs_fclose(VFSFILE *f);

int vfs_fseek(VFSFILE *f, uint64_t offset);

int vfs_fgetc(VFSFILE *f);

int vfs_peakc(VFSFILE *f);

#endif  // HORSE3D_VFS_H_
