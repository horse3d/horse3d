// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#if defined(_WIN32) || defined(_WIN64)
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) || defined(__linux)
#include <sys/sysinfo.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "osinfo.h"

static int cpu_thread_count = 0;
static int cpu_core_count = 0;
static int _osinfo_inited = 0;

int _init_cpu_thread_count() {
#if defined(__linux__) || defined(__linux)
    // Linux code path
    cpu_thread_count = get_nprocs_conf();
    return 1;
#elif defined(_WIN64) || defined(_WIN32)
    // Winapi code path
    int allocsize = 128;
    char *buf = malloc(allocsize);
    if (!buf)
        return 0;
    DWORD size = allocsize;
    while (1) {
        if (!GetLogicalProcessorInformationEx(
                3, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf,
                &size
                )) {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                return 0;
            allocsize *= 2;
            free(buf);
            buf = malloc(allocsize);
            if (!buf)
                return 0;
            size = allocsize;
        } else {
            break;
        }
    }
    int count = 0;
    char *p = buf;
    int remainingbytes = size;
    while (remainingbytes > 0) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *cpuinfo =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)p;
        if (cpuinfo->Relationship == 0)
            count++;
        p += cpuinfo->Size;
        remainingbytes -= cpuinfo->Size;
    }
    free(buf);
    cpu_thread_count = count;
    return 1;
#else
    // Generic Unix code path
    #error "not implemented yet"
#endif
}

int _init_cpu_core_count() {
#if defined(_WIN64) || defined(_WIN32)
    // Winapi code path
    int allocsize = 128;
    char *buf = malloc(allocsize);
    if (!buf)
        return 0;
    DWORD size = allocsize;
    while (1) {
        if (!GetLogicalProcessorInformationEx(
                3, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf,
                &size
                )) {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                return 0;
            allocsize *= 2;
            free(buf);
            buf = malloc(allocsize);
            if (!buf)
                return 0;
            size = allocsize;
        } else {
            break;
        }
    }
    int count = 0;
    char *p = buf;
    int remainingbytes = size;
    while (remainingbytes > 0) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *cpuinfo =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)p;
        if (cpuinfo->Relationship == 0)
            count++;
        p += cpuinfo->Size;
        remainingbytes -= cpuinfo->Size;
    }
    free(buf);
    cpu_core_count = count;
    return 1;
#elif defined(__linux__) || defined(__linux)
    // Linux code path
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return 0;

    char linebuf[1024] = "";
    int value = fgetc(f);
    while (1) {
        if (value != '\n' && value != '\r' && value != EOF) {
            if (strlen(linebuf) < sizeof(linebuf) - 1) {
                if ((strlen(linebuf) == 0 ||
                        linebuf[strlen(linebuf) - 1] == ' ') && (
                        value == ' ' || value == '\t')) {
                    value = fgetc(f);
                    continue;
                }
                if (value == '\t')
                    value = ' ';
                linebuf[strlen(linebuf) + 1] = '\0';
                linebuf[strlen(linebuf)] = (char)(unsigned char)value;
            }
            value = fgetc(f);
            continue;
        }
        if (strlen(linebuf) > 0) {
            while (strlen(linebuf) > 0 &&
                   linebuf[strlen(linebuf) - 1] == ' ')
                linebuf[strlen(linebuf) - 1] = '\0';
            int matched_cpucores = 0;
            if (strlen(linebuf) > strlen("cpu cores") &&
                    memcmp(linebuf, "cpu cores", strlen("cpu cores"))
                    == 0) {
                memmove(
                    linebuf, linebuf + strlen("cpu cores"),
                    strlen(linebuf) + 1 - strlen("cpu cores")
                );
                while (strlen(linebuf) > 0 && (
                        linebuf[0] == ' ' || linebuf[0] == ':'))
                    memmove(linebuf, linebuf + 1, strlen(linebuf));
                matched_cpucores = 1;
            }
            if (matched_cpucores) {
                int cores = atoi(linebuf);
                if (cores > 0) {
                    fclose(f);
                    cpu_core_count = cores;
                    return 1;
                }
            }
        }

        linebuf[0] = '\0';
        if (value == EOF)
            break;
        value = fgetc(f);
    }
    fclose(f);
    return 0;
#else
    // Generic Unix code path
    #error "not implemented yet"
#endif
}

__attribute__((constructor)) void _init_osinfo() {
    if (_osinfo_inited)
        return;
    _osinfo_inited = 1;
    if (!_init_cpu_thread_count())
        fprintf(
            stderr, "horsevm: warning: "
            "_init_cpu_thread_count() unexpectedly failed, memory issue?\n"
        );
    if (!_init_cpu_core_count())
        fprintf(
            stderr, "horsevm: warning: "
            "_init_cpu_core_count() unexpectedly failed, memory issue?\n"
        );
}

int osinfo_CpuCores() {
    _init_osinfo();
    return cpu_core_count;
}

int osinfo_CpuThreads() {
    _init_osinfo();
    return cpu_thread_count;
}
