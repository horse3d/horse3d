// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause


#include "compileconfig.h"


#define _FILE_OFFSET_BITS 64
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64 1
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#define _LARGEFILE_SOURCE
#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "archiver.h"
#include "bytecode.h"
#include "bytecodeserialize.h"
#include "filesys32.h"
#include "vfs.h"
#include "vfspak.h"
#include "widechar.h"

int vmbinarywriter_WriteProgram(
        const h64wchar *targetfile, int64_t targetfilelen,
        h64program *programcode,
        char **error
        ) {
    // Variables used later:
    h64wchar *pakfolder = NULL;
    int64_t pakfolderlen = 0;
    h64wchar *pakarchivepath = NULL;
    int64_t pakarchivepathlen = 0;
    h64archive *appendpakarchive = NULL;
    FILE *pakarchivef = NULL;

    // Get and open our own binary for reading:
    FILE *fbin = filesys32_OpenOwnExecutable();
    if (!fbin) {
        *error = strdup("failed to access own binary");
        return 0;
    }

    // Serialize bytecode:
    char *out = NULL;
    int64_t outlen = 0;
    if (!h64program_Dump(programcode, &out, &outlen)) {
        *error = strdup("failed to dump code");
        fclose(fbin);
        return 0;
    }

    // Get pak info:
    embeddedvfspakinfo *appendedpaks = NULL;
    int result = vfs_GetEmbbeddedPakInfoByStdioFile(
        fbin, &appendedpaks
    );
    if (!result || !appendedpaks) {
        *error = strdup("failed to read embedded h64pak data");
        fclose(fbin);
        free(out);
        return 0;
    }
    int64_t prepaklen = -1;
    {
        embeddedvfspakinfo *pinfo = appendedpaks;
        while (pinfo) {
            if (prepaklen < 0 ||
                    pinfo->data_start_offset < (uint64_t)prepaklen)
                prepaklen = pinfo->data_start_offset;
            pinfo = pinfo->next;
        }
    }
    assert(prepaklen > 0);

    // Open output file:
    int _err = 0;
    FILE *f = filesys32_OpenFromPath(
        targetfile, targetfilelen, "wb", &_err
    );
    if (!f) {
        *error = strdup("failed to open output file");
        abort:
        if (pakarchivef)
            fclose(pakarchivef);
        if (fbin)
            fclose(fbin);
        free(out);
        vfs_FreeEmbeddedPakInfo(appendedpaks);
        if (f) {
            // Since we didn't finish writing it, truncate to
            // zero bytes so it's obvious the result is broken.
            // Don't want the user to try to use a half written binary.
            fflush(f);
            ftruncate(fileno(f), 0);
        }
        fclose(f);
        free(pakarchivepath);
        if (appendpakarchive)
            h64archive_Close(appendpakarchive);
        int _ignoreerr = 0;
        if (pakfolder)
            filesys32_RemoveFolderRecursively(
                pakfolder, pakfolderlen, &_ignoreerr
            );
        free(pakfolder);
        return 0;
    }
    // Write out the binary itself:
    if (fseek64(fbin, 0, SEEK_SET) != 0) {
        *error = strdup("failed to read base binary");
        goto abort;
    }
    int64_t outputoffset = 0;
    int64_t i = 0;
    while (i < prepaklen) {
        char buf[512];
        uint64_t transferbytes = sizeof(buf);
        if (i + transferbytes > (uint64_t)prepaklen)
            transferbytes = ((uint64_t)prepaklen) - i;
        assert(transferbytes > 0);

        size_t read = fread(buf, 1, transferbytes, fbin);
        if (read != transferbytes) {
            *error = strdup("failed to read base binary");
            goto abort;
        }
        size_t written = fwrite(buf, 1, transferbytes, f);
        if (written != transferbytes) {
            *error = strdup("failed to write base binary");
            goto abort;
        }
        i += transferbytes;
        outputoffset += transferbytes;
    }
    // Write out all the .h64pak files which belong to the core:
    {
        embeddedvfspakinfo *pinfo = appendedpaks;
        while (pinfo) {
            int result_programcode = 0;
            int result_builtinmods = 0;
            if (!vfs_HasEmbbededPakThatContainsFilePath_Stdio(
                    pinfo, fbin, "horse_modules_builtin",
                    &result_builtinmods
                    )) {
                *error = strdup("failed to examine h64paks for "
                    "detail contents");
                goto abort;
            }
            if (!vfs_HasEmbbededPakThatContainsFilePath_Stdio(
                    pinfo, fbin, "program.hasm_raw",
                    &result_programcode
                    )) {
                *error = strdup("failed to examine h64paks for "
                    "detail contents");
                goto abort;
            }
            if (!result_programcode && result_builtinmods) {
                // We want this appended.
                int64_t pakstart = outputoffset;

                // First, copy over the data contents verbatim:
                i = 0;
                if (fseek64(fbin, pinfo->data_start_offset,
                            SEEK_SET) != 0) {
                    *error = strdup("failed to copy over h64paks "
                        "from source binary");
                    goto abort;
                }
                int64_t paklen = pinfo->data_end_offset -
                    pinfo->data_start_offset;
                assert(paklen > 0);
                while (i < paklen) {
                    char buf[512];
                    uint64_t transferbytes = sizeof(buf);
                    if (i + transferbytes > (uint64_t)paklen)
                        transferbytes = ((uint64_t)paklen) - i;
                    assert(transferbytes > 0);

                    size_t read = fread(buf, 1, transferbytes, fbin);
                    if (read != transferbytes) {
                        *error = strdup("failed to read base h64pak");
                        goto abort;
                    }
                    size_t written = fwrite(buf, 1, transferbytes, f);
                    if (written != transferbytes) {
                        *error = strdup("failed to write base h64pak");
                        goto abort;
                    }
                    i += transferbytes;
                    outputoffset += transferbytes;
                }
                // Now, append the trailing header with info:
                size_t written = fwrite(
                    &pakstart, 1, sizeof(pakstart), f
                );
                if (written != sizeof(pakstart)) {
                    *error = strdup("failed to write base h64pak");
                    goto abort;
                }
                int64_t pakend = pakstart + paklen;
                written = fwrite(
                    &pakend, 1, sizeof(pakend), f
                );
                if (written != sizeof(pakend)) {
                    *error = strdup("failed to write base h64pak");
                    goto abort;
                }
                char magic_pakappend[] = (
                    "\x00\xFF\x00H64PAKAPPEND_V1\x00\xFF\x00"
                );
                int magiclen = strlen(
                    "\x01\xFF\x01H64PAKAPPEND_V1\x01\xFF\x01"
                );
                written = fwrite(
                    magic_pakappend, 1, magiclen, f
                );
                if (written != (size_t)magiclen) {
                    *error = strdup("failed to write base h64pak");
                    goto abort;
                }
            }
            pinfo = pinfo->next;
        }
    }
    fclose(fbin);  // don't need that anymore
    fbin = NULL;

    // Now, create custom .h64pak with program code & resources:
    pakfolder = NULL;
    pakfolderlen = 0;
    {
        static h64wchar _prefix[] = {
            'h', '6', '4', 'v', 'm', 'w', 'r',
            'i', 't', 'e', 'r'
        };
        int64_t _prefixlen = strlen("h64vmwriter");
        h64wchar *_ignorefile = NULL;
        int64_t _ignorefilelen = 0;
        FILE *_ignoreresult = filesys32_TempFile(
            1, 1, _prefix, _prefixlen, NULL, 0,
            &pakfolder, &pakfolderlen,
            &_ignorefile, &_ignorefilelen
        );
        assert(!_ignoreresult && !_ignorefile);
        if (!pakfolder) {
            *error = strdup("failed to get temporary folder");
            goto abort;
        }
    }
    {
        static h64wchar fname[] = {
            'm', 'y',
            'd', 'a', 't', 'a', '.', 'h', '6', '4', 'p', 'a', 'k'
        };
        int64_t fnamelen = strlen("mydata.h64pak");
        pakarchivepath = filesys32_Join(
            pakfolder, pakfolderlen, fname, fnamelen,
            &pakarchivepathlen
        );
        if (!pakarchivepath) {
            *error = strdup("failed to alloc temp h64pak path");
            goto abort;
        }
    }
    appendpakarchive = archive_FromFilePath(
        pakarchivepath, pakarchivepathlen, 1,
        VFSFLAG_NO_VIRTUALPAK_ACCESS, H64ARCHIVE_TYPE_ZIP
    );
    if (!appendpakarchive) {
        *error = strdup("failed to create h64pak archive");
        goto abort;
    }
    if (!h64archive_AddFileFromMem(
            appendpakarchive, "program.hasm_raw",
            out, outlen
            )) {
        *error = strdup("failed to add bytecode to h64pak archive");
        goto abort;
    }
    h64archive_Close(appendpakarchive);

    // Now, append the new .h64pak we made to the binary:
    int _openerr = 0;
    pakarchivef = filesys32_OpenFromPath(
        pakarchivepath, pakarchivepathlen, "rb", &_openerr
    );
    if (!pakarchivef) {
        *error = strdup("failed to read back new h64pak archive");
        goto abort;
    }
    int64_t newarchivesize = 0;
    if (fseek64(pakarchivef, 0, SEEK_END) != 0 ||
            (newarchivesize = ftell64(pakarchivef)) <= 0) {
        *error = strdup("failed to read back new h64pak archive");
        goto abort;
    }
    if (fseek64(pakarchivef, 0, SEEK_SET) != 0) {
        *error = strdup("failed to read back new h64pak archive");
        goto abort;
    }
    int64_t newpakstart = outputoffset;
    i = 0;
    while (i < newarchivesize) {
        char buf[512];
        uint64_t transferbytes = sizeof(buf);
        if (i + transferbytes > (uint64_t)newarchivesize)
            transferbytes = ((uint64_t)newarchivesize) - i;
        assert(transferbytes > 0);

        size_t read = fread(buf, 1, transferbytes, pakarchivef);
        if (read != transferbytes) {
            *error = strdup("failed to write new h64pak");
            goto abort;
        }
        size_t written = fwrite(buf, 1, transferbytes, f);
        if (written != transferbytes) {
            *error = strdup("failed to write new h64pak");
            goto abort;
        }
        i += transferbytes;
        outputoffset += transferbytes;
    }
    int64_t newpakend = outputoffset;
    fflush(f);
    fclose(pakarchivef);
    int _ignoreerr = 0;
    filesys32_RemoveFolderRecursively(
        pakfolder, pakfolderlen, &_ignoreerr
    );
    // Append header for our custom .h64pak:
    size_t written = fwrite(&newpakstart, 1, sizeof(newpakstart), f);
    if (written != sizeof(newpakstart)) {
        *error = strdup("failed to write new h64pak");
        goto abort;
    }
    written = fwrite(&newpakend, 1, sizeof(newpakend), f);
    if (written != sizeof(newpakend)) {
        *error = strdup("failed to write new h64pak");
        goto abort;
    }
    char magic_pakappend[] = (
        "\x00\xFF\x00H64PAKAPPEND_V1\x00\xFF\x00"
    );
    int magiclen = strlen(
        "\x01\xFF\x01H64PAKAPPEND_V1\x01\xFF\x01"
    );
    written = fwrite(
        magic_pakappend, 1, magiclen, f
    );
    if (written != (size_t)magiclen) {
        *error = strdup("failed to write base h64pak");
        goto abort;
    }

    // When done, close file & free data:
    fflush(f);
    fclose(f);
    f = NULL;
    free(out);
    return 1;
}