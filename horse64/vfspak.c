// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include "physfs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "archiver.h"
#include "filesys.h"
#include "vfs.h"
#include "vfspak.h"
#include "vfspartialfileio.h"


int vfs_AddPakEx(
        const char *path, uint64_t start_offset, uint64_t max_len,
        int ignore_extension) {
    if (!path || !filesys_FileExists(path) ||
            filesys_IsDirectory(path) ||
            strlen(path) < strlen(".h64pak") ||
            (!ignore_extension &&
             memcmp(path + strlen(path) - strlen(".h64pak"),
                    ".h64pak", strlen(".h64pak")) != 0))
        return 0;
    #if defined(DEBUG_VFS) && !defined(NDEBUG)
    h64printf("horse64/vfs.c: debug: "
           "adding resource pack: %s\n", path);
    #endif
    if (start_offset == 0 && max_len == 0) {
        if (!PHYSFS_mount(path, "/", 1)) {
            return 0;
        }
    } else {
        FILE *f = filesys_OpenFromPath(path, "rb");
        if (!f) {
            return 0;
        }
        PHYSFS_Io *io = (PHYSFS_Io *)(
            _PhysFS_Io_partialFileReadOnlyStruct(f, start_offset, max_len)
        );
        if (!io) {
            fclose(f);
            return 0;
        }
        fclose(f);
        f = NULL;
        if (!PHYSFS_mountIo(io, path, "/", 1)) {
            io->destroy(io);
            return 0;
        }
    }

    return 1;
}

int vfs_AddPak(const char *path) {
    return vfs_AddPakEx(path, 0, 0, 0);
}

int vfs_GetEmbbeddedPakInfo(
        const char *path, int64_t end_offset,
        embeddedvfspakinfo **einfo
        ) {
    VFSFILE *f = vfs_fopen(
        path, "rb", VFSFLAG_NO_VIRTUALPAK_ACCESS
    );
    if (!f) {
        *einfo = NULL;
        return 0;
    }
    if (vfs_fseektoend(f) < 0) {
        vfs_fclose(f);
        *einfo = NULL;
        return 0;
    }
    int64_t file_len = vfs_ftell(f);
    if (file_len <= 0) {
        vfs_fclose(f);
        *einfo = NULL;
        return 0;
    }
    uint64_t end_check = file_len - end_offset;
    char magic_pakappend[] = (
        "\x00\xFF\x00H64PAKAPPEND_V1\x00\xFF\x00"
    );
    int magiclen = strlen("\x01\xFF\x01H64PAKAPPEND_V1\x01\xFF\x01");
    if (end_check < magiclen + sizeof(uint64_t) * 2) {
        vfs_fclose(f);
        *einfo = NULL;
        return 1;  // no pak append header, so no pak to find.
    }
    if (vfs_fseek(f,
            end_check - magiclen - sizeof(uint64_t) * 2
            ) < 0) {
        vfs_fclose(f);
        *einfo = NULL;
        return 0;
    }
    char comparedata[256] = {0};
    if (vfs_fread(comparedata,
            magiclen + sizeof(uint64_t) * 2,
            1, f) != 1) {
        vfs_fclose(f);
        *einfo = NULL;
        return 0;
    }
    if (memcmp(comparedata + sizeof(uint64_t) * 2, magic_pakappend,
            magiclen) != 0) {
        vfs_fclose(f);
        *einfo = NULL;
        return 1;  // no pak append header, so no pak to find.
    }
    uint64_t pak_start, pak_end;
    memcpy(&pak_start, comparedata, sizeof(pak_start));
    memcpy(&pak_end, comparedata + sizeof(pak_start), sizeof(pak_end));
    if (pak_end != end_check - magiclen -
            sizeof(uint64_t) * 2 ||
            pak_start >= pak_end || end_check - pak_start <= 0) {
        vfs_fclose(f);
        *einfo = NULL;
        return 1;  // wrong pak offsets, so no working pak appended
    }
    vfs_fclose(f);
    
    *einfo = malloc(sizeof(**einfo));
    if (!*einfo) {
        return 0;
    }
    memset(*einfo, 0, sizeof(**einfo));
    (*einfo)->data_start_offset = pak_start;
    (*einfo)->data_end_offset = pak_end;
    (*einfo)->full_with_header_start_offset = pak_start;
    (*einfo)->full_with_header_end_offset = (
        pak_end + magiclen + sizeof(uint64_t) * 2
    );
    embeddedvfspakinfo *next_einfo = NULL;
    if (!vfs_GetEmbbeddedPakInfo(
            path, file_len - pak_start, &next_einfo
            )) {
        free(*einfo);
        *einfo = NULL;
        return 0;
    }
    (*einfo)->next = next_einfo;
    return 1;
}

int vfs_HasEmbbededPakGivenFilePath(
        embeddedvfspakinfo *einfo, const char *binary_path,
        const char *file_path, int *out_result
        ) {
    h64archive *a = archive_FromFilePathSlice(
        binary_path, einfo->data_start_offset,
        einfo->data_end_offset - einfo->data_start_offset,
        0, 0, H64ARCHIVE_TYPE_AUTODETECT
    );
    if (!a) {
        return 0;
    }
    int64_t idx = -1;
    if (!h64archive_GetEntryIndex(a, file_path, &idx)) {
        return 0;
    }
    *out_result = (idx >= 0);
    return 1;
}

void vfs_FreeEmbeddedPakInfo(embeddedvfspakinfo *einfo) {
    if (!einfo)
        return;
    vfs_FreeEmbeddedPakInfo(einfo->next);
    free(einfo);
}

int _vfs_AddPaksFromBinaryWithEndOffset(
        const char *path, int64_t end_offset
        ) {
    embeddedvfspakinfo *einfo = NULL;
    int result = (
        vfs_GetEmbbeddedPakInfo(path, end_offset, &einfo)
    );
    if (!result)
        return 0;
    embeddedvfspakinfo *einfo_orig = einfo;
    while (einfo) {
        if (!vfs_AddPakEx(
                path, einfo->data_start_offset,
                einfo->data_end_offset - einfo->data_start_offset, 1
                )) {
            vfs_FreeEmbeddedPakInfo(einfo_orig);
            return 0;
        }
        einfo = einfo->next;
    }
    vfs_FreeEmbeddedPakInfo(einfo_orig);
    return 1;
}

int vfs_AddPaksEmbeddedInBinary(const char *path) {
    return _vfs_AddPaksFromBinaryWithEndOffset(path, 0);
}