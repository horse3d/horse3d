#ifndef HORSE3D_FILESYS_H_
#define HORSE3D_FILESYS_H_

int filesys_FileExists(const char *path);

int filesys_IsDirectory(const char *path);

int filesys_RemoveFolder(const char *path, int recursive);

int filesys_RemoveFile(const char *path);

const char *filesys_AppDataSubFolder(
    const char *appname
);

const char *filesys_DocumentsSubFolder(
    const char *subfolder
);

int filesys_IsSymlink(const char *path, int *result);

void filesys_FreeFolderList(char **list);

int filesys_ListFolder(const char *path,
                       char ***contents,
                       int returnFullPath);

int filesys_GetComponentCount(const char *path);

void filesys_RequestFilesystemAccess();

int filesys_CreateDirectory(const char *path);

char *filesys_GetOwnExecutable();

int filesys_LaunchExecutable(const char *path, int argc, ...);

char *filesys_ParentdirOfItem(const char *path);

char *filesys_Join(const char *path1, const char *path2);

int filesys_IsAbsolutePath(const char *path);

char *filesys_ToAbsolutePath(const char *path);

char *filesys_Basename(const char *path);

char *filesys_Dirname(const char *path);

int filesys_GetSize(const char *path, uint64_t *size);

char *filesys_Normalize(const char *path);

char *filesys_GetCurrentDirectory();

char *filesys_GetRealPath(const char *s);

char *filesys_TurnIntoPathRelativeTo(
    const char *path, const char *makerelativetopath
);

int filesys_PathCompare(const char *p1, const char *p2);

int filesys_FolderContainsPath(
    const char *folder_path, const char *check_path,
    int *result
);

#endif  // HORSE3D_FILESYS_H_
