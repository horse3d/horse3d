
#include <assert.h>
#include <mathc/mathc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesys.h"
#include "glheaders.h"
#include "hash.h"
#include "meshes.h"
#include "texture.h"
#include "vfs.h"


//#define DEBUG_MESH_LOADER
//#define DEBUG_MESH_TEXTURE_FINDER


extern SDL_gles2funcs *_gl;
int _texture_LoadTextureGL(h3dtexture *tex, int alpha);
extern hashmap *_seen_textures;
extern hashmap *mesh_cache;
void _clean_seen_textures();


static void objface_LoadCoordinate(
        char **p,
        int *vindex, int *vtindex,
        int *vnindex) {
    while (**p == ' ' || **p == '\t')
        (*p)++;
    if (**p == '\0') {
        abort:
        *vindex = -1;
        *vtindex = -1;
        *vnindex = -1;
        return;
    }
    char vindexval[62] = "";
    while (**p != '\0' && **p != '/' && **p != ' ' && **p != '\t') {
        if (strlen(vindexval) < sizeof(vindexval) - 1) {
            vindexval[strlen(vindexval) + 1] = '\0';
            vindexval[strlen(vindexval)] = **p;
        }
        (*p)++;
    }
    *vindex = atoi(vindexval);
    if (strlen(vindexval) == 0)
        *vindex = -1;
    while (**p == '/')
        (*p)++;
    char vtindexval[62] = "";
    while (**p != '\0' && **p != '/' && **p != ' ' && **p != '\t') {
        if (strlen(vtindexval) < sizeof(vtindexval) - 1) {
            vtindexval[strlen(vtindexval) + 1] = '\0';
            vtindexval[strlen(vtindexval)] = **p;
        }
        (*p)++;
    }
    *vtindex = atoi(vtindexval);
    if (strlen(vtindexval) == 0)
        *vtindex = -1;
    while (**p == '/')
        (*p)++;
    char vnindexval[62] = "";
    while (**p != '\0' && **p != '/' && **p != ' ' && **p != '\t') {
        if (strlen(vnindexval) < sizeof(vnindexval) - 1) {
            vnindexval[strlen(vnindexval) + 1] = '\0';
            vnindexval[strlen(vnindexval)] = **p;
        }
        (*p)++;
    }
    *vnindex = atoi(vnindexval);
    if (strlen(vnindexval) == 0)
        *vnindex = -1;

    // Skip everything else we're not familiar with:
    while (**p == '/' || (**p >= '0' && **p <= '9') || **p == '.')
        (*p)++;
}


static int mesh_ParseMTLLine(
        const char *mtldir_path,
        const char *line,
        int *material_count,
        char ***material_names,
        char ***material_diffuse_map
        ) {
    if (strlen(line) < strlen("mtllib ") ||
            memcmp(line, "mtllib ", strlen("mtllib ")) != 0)
        return 0;
    const char *p = line + strlen("mtllib ");
    while (*p == ' ' || *p == '\t')
        p++;
    char fnamebuf[128];
    int copylen = strlen(p);
    if (copylen > sizeof(fnamebuf) - 1)
        copylen = sizeof(fnamebuf) - 1;
    memcpy(fnamebuf, p, copylen);
    fnamebuf[copylen] = '\0';
    while (strlen(fnamebuf) > 0 &&
            (fnamebuf[strlen(fnamebuf) - 1] == ' ' ||
             fnamebuf[strlen(fnamebuf) - 1] == '\t' ||
             fnamebuf[strlen(fnamebuf) - 1] == '\r'))
        fnamebuf[strlen(fnamebuf) - 1] = '\0';

    char *mtlpath = filesys_ParentdirOfItem(fnamebuf);
    if (!mtlpath)
        return 0;
    if (!filesys_IsAbsolutePath(mtlpath)) {
        char *changed = filesys_Join(mtldir_path, mtlpath);
        free(mtlpath);
        if (!changed)
            return 0;
        mtlpath = changed;
    }

    char *mtlname = filesys_Basename(fnamebuf);
    if (!mtlname) {
        free(mtlpath);
        return 0;
    }
    char *openpath = filesys_Join(mtlpath, mtlname);
    free(mtlpath); free(mtlname);
    if (!openpath)
        return 0;

    VFSFILE *f2 = vfs_fopen(openpath, "rb");
    free(openpath);
    if (!f2)
        return 0;

    char current_mtl_name[128] = "";
    int current_mtl_had_diffuse = 0;
    char mtlline[128] = "";
    while (1) {
        int c = -1;
        if (!vfs_feof(f2))
            c = vfs_fgetc(f2);
        if (c < 0 && !vfs_feof(f2)) {
            vfs_fclose(f2);
            return 0;
        }
        if (c < 0 || c == '\r' || c == '\n') {
            while (strlen(mtlline) > 0 &&
                   (mtlline[strlen(mtlline) - 1] == '\n' ||
                    mtlline[strlen(mtlline) - 1] == '\r' ||
                    mtlline[strlen(mtlline) - 1] == ' ' ||
                    mtlline[strlen(mtlline) - 1] == '\t'))
                mtlline[strlen(mtlline) - 1] = '\0';
            while (mtlline[0] == ' ' || mtlline[0] == '\t')
                memmove(mtlline, mtlline + 1, strlen(mtlline));
            if (strlen(mtlline) > strlen("newmtl ") &&
                    memcmp(mtlline, "newmtl ", strlen("newmtl ")) == 0) {
                const char *p = mtlline + strlen("newmtl ");
                while (*p == ' ' || *p == '\t')
                    p++;
                snprintf(current_mtl_name, sizeof(current_mtl_name) - 1,
                         "%s", p);
                current_mtl_had_diffuse = 0;
            } else if (strlen(mtlline) > strlen("map_Kd ") && (
                    memcmp(mtlline, "map_Kd ", strlen("map_Kd ")) == 0 ||
                    memcmp(mtlline, "map_kd ", strlen("map_kd ")) == 0) &&
                    strlen(current_mtl_name) > 0 &&
                    !current_mtl_had_diffuse) {
                const char *p = mtlline + strlen("map_Kd ");
                while (*p == ' ' || *p == '\t')
                    p++;
                if (strlen(p) <= 0)
                    continue;

                current_mtl_had_diffuse = 1;
                int newc = *material_count + 1;
                char **material_names_new = realloc(
                    *material_names,
                    sizeof(*material_names_new) * newc
                );
                if (!material_names_new) {
                    vfs_fclose(f2);
                    return 0;
                }
                *material_names = material_names_new;
                char **material_diffuse_map_new = realloc(
                    *material_diffuse_map,
                    sizeof(*material_diffuse_map_new) * newc
                );
                if (!material_diffuse_map_new) {
                    vfs_fclose(f2);
                    return 0;
                }
                *material_diffuse_map = material_diffuse_map_new;

                (*material_names)[*material_count] = strdup(current_mtl_name);
                if (!(*material_names)[*material_count]) {
                    vfs_fclose(f2);
                    return 0;
                }
                (*material_diffuse_map)[*material_count] = strdup(p);
                if (!(*material_diffuse_map)[*material_count]) {
                    free((*material_names)[*material_count]);
                    vfs_fclose(f2);
                    return 0;
                }
                *material_count = newc;
            }
            mtlline[0] = '\0';
        } else {
            mtlline[strlen(mtlline) + 1] = '\0';
            mtlline[strlen(mtlline)] = c;
        }
        if (c < 0 && vfs_feof(f2))
            break;
    }
    if (f2)
        vfs_fclose(f2);
    return 1;
} 


h3dmesh *mesh_LoadFromOBJEx(
        const char *path, int usecache,
        h3dmeshloadinfo *loadinfo,
        char **error
        ) {
    if (error)
        *error = NULL;
    if (usecache) {
        int freeloadinfo = 0;
        if (!loadinfo) {
            freeloadinfo = 1;
            loadinfo = malloc(sizeof(*loadinfo));
            if (!loadinfo)
                return NULL;
            memset(loadinfo, 0, sizeof(*loadinfo));
        }
        char *p = filesys_Normalize(path);
        if (!p) {
            if (loadinfo && freeloadinfo)
                free(loadinfo);
            return NULL;
        }

        if (!mesh_cache) {
            mesh_cache = hash_NewBytesMap(128);
            if (!mesh_cache) {
                free(p);
                if (loadinfo && freeloadinfo)
                    free(loadinfo);
                return NULL;
            }
        }
        uintptr_t hashptrval = 0;
        if (!hash_BytesMapGet(
                mesh_cache, p, strlen(p),
                (uint64_t*)&hashptrval)) {
            h3dmesh *m = mesh_LoadFromOBJEx(
                p, 0, loadinfo, error
            );
            if (!m) {
                free(p);
                if (loadinfo && freeloadinfo)
                    free(loadinfo);
                return NULL;
            }
            if (!hash_BytesMapSet(
                    mesh_cache, p, strlen(p),
                    (uint64_t)(uintptr_t)m)) {
                free(p);
                mesh_DestroyMesh(m);
                if (loadinfo && freeloadinfo)
                    free(loadinfo);
                return NULL;
            }
            free(p);
            if (loadinfo && freeloadinfo)
                free(loadinfo);
            return m;
        }
        free(p);
        if (loadinfo && freeloadinfo)
            free(loadinfo);
        return (h3dmesh*)(void*)hashptrval;
    }
    assert(loadinfo != NULL);
    _clean_seen_textures();

    char *mtldir_path = filesys_ParentdirOfItem(path);
    #ifdef DEBUG_MESH_LOADER
    printf("horse3d/meshes.c: debug: OBJ loading: \"%s\"\n", path);
    #endif

    int allocated_v = 0;
    int v_count = 0;
    double *v_x = NULL;
    double *v_y = NULL;
    double *v_z = NULL;

    int allocated_vt = 0;
    int vt_count = 0;
    double *vt_x = NULL;
    double *vt_y = NULL;

    int allocated_vn = 0;
    int vn_count = 0;
    double *vn_x = NULL;
    double *vn_y = NULL;
    double *vn_z = NULL;

    int allocated_faces = 0;
    int faces_count = 0;
    char **faces_material = NULL;
    int *face_vindex1 = NULL;
    int *face_vindex2 = NULL;
    int *face_vindex3 = NULL;
    int *face_vtindex1 = NULL;
    int *face_vtindex2 = NULL;
    int *face_vtindex3 = NULL;
    int *face_vnindex1 = NULL;
    int *face_vnindex2 = NULL;
    int *face_vnindex3 = NULL;

    int material_count = 0;
    char **material_names = NULL;
    char **material_diffuse_map = NULL;

    h3dmesh *builtmesh = NULL;
    h3dmesh *_result = NULL;

    VFSFILE *f = vfs_fopen(path, "rb");
    if (!f) {
        if (mtldir_path)
            free(mtldir_path);
        if (error)
            *error = NULL;
        int exists_result = 0;
        if (vfs_Exists(path, &exists_result))
            if (!exists_result)
                if (error)
                    *error = strdup("file not found");
        if (error && !*error)
            *error = strdup(
                "failed to read file, disk error or out of memory?"
            );
        return NULL;
    }

    char activematerial[128] = "";
    char linebuf[128] = "";
    while (1) {
        int c = -1;
        if (!vfs_feof(f))
            c = vfs_fgetc(f);
        if ((c < 0 && vfs_feof(f)) || c == '\n' || c == '\r') {
            while (linebuf[0] == ' ' || linebuf[0] == '\t')
                memmove(linebuf, linebuf + 1, strlen(linebuf));

            if (strlen(linebuf) > strlen("usemtl ") &&
                    memcmp(linebuf, "usemtl ", strlen("usemtl ")) == 0) {
                const char *p = linebuf + strlen("usemtl ");
                while (*p == ' ' || *p == '\t')
                    p++;
                memcpy(activematerial, p, strlen(p) + 1);
            } else if (strlen(linebuf) > strlen("mtllib ") &&
                    memcmp(linebuf, "mtllib ", strlen("mtllib ")) == 0) {
                if (!mesh_ParseMTLLine(
                        mtldir_path,
                        linebuf, &material_count,
                        &material_names, &material_diffuse_map))
                    goto errororquit;
            } else if (linebuf[0] == 'f' &&
                       (linebuf[1] == ' ' || linebuf[1] == '\t')) {
                char *p = linebuf + 2;
                while (*p == ' ' || *p == '\t')
                    p++;
                int vindex1 = -1;
                int vtindex1 = -1;
                int vnindex1 = -1;
                int vindex2 = -1;
                int vtindex2 = -1;
                int vnindex2 = -1;
                int vindex3 = -1;
                int vtindex3 = -1;
                int vnindex3 = -1;
                int vindex4 = -1;
                int vtindex4 = -1;
                int vnindex4 = -1;
                objface_LoadCoordinate(
                    &p, &vindex1, &vtindex1, &vnindex1
                );
                objface_LoadCoordinate(
                    &p, &vindex2, &vtindex2, &vnindex2
                );
                objface_LoadCoordinate(
                    &p, &vindex3, &vtindex3, &vnindex3
                );
                objface_LoadCoordinate(
                    &p, &vindex4, &vtindex4, &vnindex4
                );
                if (vindex1 >= 0 && vindex2 >= 0 && vindex3 >= 0) {
                    if (faces_count + 1 >= allocated_faces) {
                        int newc = allocated_faces * 2;
                        if (newc < 16)
                            newc = 16;
                        if (newc < faces_count + 2)
                            newc = faces_count + 2;
                        int *face_vindex1_new = realloc(
                            face_vindex1, sizeof(*face_vindex1) * newc
                        );
                        if (!face_vindex1_new)
                            goto errororquit;
                        face_vindex1 = face_vindex1_new;
                        int *face_vindex2_new = realloc(
                            face_vindex2, sizeof(*face_vindex2) * newc
                        );
                        if (!face_vindex2_new)
                            goto errororquit;
                        face_vindex2 = face_vindex2_new;
                        int *face_vindex3_new = realloc(
                            face_vindex3, sizeof(*face_vindex3) * newc
                        );
                        if (!face_vindex3_new)
                            goto errororquit;
                        face_vindex3 = face_vindex3_new;
                        int *face_vtindex1_new = realloc(
                            face_vtindex1, sizeof(*face_vtindex1) * newc
                        );
                        if (!face_vtindex1_new)
                            goto errororquit;
                        face_vtindex1 = face_vtindex1_new;
                        int *face_vtindex2_new = realloc(
                            face_vtindex2, sizeof(*face_vtindex2) * newc
                        );
                        if (!face_vtindex2_new)
                            goto errororquit;
                        face_vtindex2 = face_vtindex2_new;
                        int *face_vtindex3_new = realloc(
                            face_vtindex3, sizeof(*face_vtindex3) * newc
                        );
                        if (!face_vtindex3_new)
                            goto errororquit;
                        face_vtindex3 = face_vtindex3_new;
                        int *face_vnindex1_new = realloc(
                            face_vnindex1, sizeof(*face_vnindex1) * newc
                        );
                        if (!face_vnindex1_new)
                            goto errororquit;
                        face_vnindex1 = face_vnindex1_new;
                        int *face_vnindex2_new = realloc(
                            face_vnindex2, sizeof(*face_vnindex2) * newc
                        );
                        if (!face_vnindex2_new)
                            goto errororquit;
                        face_vnindex2 = face_vnindex2_new;
                        int *face_vnindex3_new = realloc(
                            face_vnindex3, sizeof(*face_vnindex3) * newc
                        );
                        if (!face_vnindex3_new)
                            goto errororquit;
                        face_vnindex3 = face_vnindex3_new;
                        char **faces_material_new = realloc(
                            faces_material, sizeof(*faces_material) * newc
                        );
                        if (!faces_material_new)
                            goto errororquit;
                        faces_material = faces_material_new;
                        allocated_faces = newc;
                    }
                    if (strlen(activematerial) > 0) {
                        faces_material[faces_count] = strdup(
                            activematerial   
                        );
                        if (!faces_material[faces_count])
                            goto errororquit;
                    } else {
                        faces_material[faces_count] = NULL;
                    }
                    face_vindex1[faces_count] = vindex1 - 1;
                    face_vindex2[faces_count] = vindex2 - 1;
                    face_vindex3[faces_count] = vindex3 - 1;
                    face_vtindex1[faces_count] = vtindex1 - 1;
                    face_vtindex2[faces_count] = vtindex2 - 1;
                    face_vtindex3[faces_count] = vtindex3 - 1;
                    face_vnindex1[faces_count] = vnindex1 - 1;
                    face_vnindex2[faces_count] = vnindex2 - 1;
                    face_vnindex3[faces_count] = vnindex3 - 1;
                    faces_count++;
                    if (vindex4 >= 0) {
                        if (strlen(activematerial) > 0) {
                            faces_material[faces_count] = strdup(
                                activematerial
                            );
                            if (!faces_material[faces_count])
                                goto errororquit;
                        } else {
                            faces_material[faces_count] = NULL;
                        }
                        face_vindex1[faces_count] = vindex3 - 1;
                        face_vindex3[faces_count] = vindex1 - 1;
                        face_vindex2[faces_count] = vindex4 - 1;
                        face_vtindex1[faces_count] = vtindex3 - 1;
                        face_vtindex3[faces_count] = vtindex1 - 1;
                        face_vtindex2[faces_count] = vtindex4 - 1;
                        face_vnindex1[faces_count] = vnindex3 - 1;
                        face_vnindex3[faces_count] = vnindex1 - 1;
                        face_vnindex2[faces_count] = vnindex4 - 1;
                        faces_count++;
                    }
                } else {
                    #ifdef DEBUG_MESH_LOADER
                    printf(
                        "horse3d/meshes.c: debug: OBJ "
                        "invalid face line: %s\n",
                        linebuf
                    );
                    #endif
                }
            } else if (linebuf[0] == 'v' &&
                       (linebuf[1] == ' ' || linebuf[1] == '\t')) {
                char v1buf[64] = "";
                char v2buf[64] = "";
                char v3buf[64] = "";

                const char *p = linebuf + 1;
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v1buf) < sizeof(v1buf) - 1) {
                    v1buf[strlen(v1buf) + 1] = '\0';
                    v1buf[strlen(v1buf)] = *p;
                    p++;
                }
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v2buf) < sizeof(v1buf) - 1) {
                    v2buf[strlen(v2buf) + 1] = '\0';
                    v2buf[strlen(v2buf)] = *p;
                    p++;
                }
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v3buf) < sizeof(v3buf) - 1) {
                    v3buf[strlen(v3buf) + 1] = '\0';
                    v3buf[strlen(v3buf)] = *p;
                    p++;
                }

                if (v_count >= allocated_v) {
                    int newc = v_count * 2;
                    if (newc < 16)
                        newc = 16;

                    double *v_xnew = realloc(v_x, newc * sizeof(*v_x));
                    if (!v_xnew)
                        goto errororquit;
                    v_x = v_xnew;
                    double *v_ynew = realloc(v_y, newc * sizeof(*v_y));
                    if (!v_ynew)
                        goto errororquit;
                    v_y = v_ynew;
                    double *v_znew = realloc(v_z, newc * sizeof(*v_z));
                    if (!v_znew)
                        goto errororquit;
                    v_z = v_znew;
                    allocated_v = newc;
                }
                assert(v_count < allocated_v);
                v_x[v_count] = atof(v1buf);
                v_y[v_count] = atof(v2buf);
                v_z[v_count] = atof(v3buf);
                v_count++;
            } else if (linebuf[0] == 'v' &&
                    linebuf[1] == 't' &&
                    (linebuf[2] == ' ' || linebuf[2] == '\t')) {
                char v1buf[64] = "";
                char v2buf[64] = "";

                const char *p = linebuf + 2;
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v1buf) < sizeof(v1buf) - 1) {
                    v1buf[strlen(v1buf) + 1] = '\0';
                    v1buf[strlen(v1buf)] = *p;
                    p++;
                }
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v2buf) < sizeof(v1buf) - 1) {
                    v2buf[strlen(v2buf) + 1] = '\0';
                    v2buf[strlen(v2buf)] = *p;
                    p++;
                }

                if (vt_count >= allocated_vt) {
                    int newc = vt_count * 2;
                    if (newc < 16)
                        newc = 16;

                    double *vt_xnew = realloc(
                        vt_x, newc * sizeof(*vt_x)
                    );
                    if (!vt_xnew)
                        goto errororquit;
                    vt_x = vt_xnew;
                    double *vt_ynew = realloc(
                        vt_y, newc * sizeof(*vt_y)
                    );
                    if (!vt_ynew)
                        goto errororquit;
                    vt_y = vt_ynew;
                    allocated_vt = newc;
                }
                vt_x[vt_count] = atof(v1buf);
                vt_y[vt_count] = -atof(v2buf);
                vt_count++;
            } else if (linebuf[0] == 'v' &&
                    linebuf[1] == 'n' &&
                    (linebuf[2] == ' ' || linebuf[2] == '\t')) {
                char v1buf[64] = "";
                char v2buf[64] = "";
                char v3buf[64] = "";

                const char *p = linebuf + 2;
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v1buf) < sizeof(v1buf) - 1) {
                    v1buf[strlen(v1buf) + 1] = '\0';
                    v1buf[strlen(v1buf)] = *p;
                    p++;
                }
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v2buf) < sizeof(v1buf) - 1) {
                    v2buf[strlen(v2buf) + 1] = '\0';
                    v2buf[strlen(v2buf)] = *p;
                    p++;
                }
                while (*p == ' ' || *p == '\t')
                    p++;
                while (*p != ' ' && *p != '\t' && *p != '\0' &&
                       strlen(v3buf) < sizeof(v3buf) - 1) {
                    v3buf[strlen(v3buf) + 1] = '\0';
                    v3buf[strlen(v3buf)] = *p;
                    p++;
                }

                if (vn_count >= allocated_vn) {
                    int newc = vn_count * 2;
                    if (newc < 16)
                        newc = 16;

                    double *vn_xnew = realloc(
                        vn_x, newc * sizeof(*vn_x)
                    );
                    if (!vn_xnew)
                        goto errororquit;
                    vn_x = vn_xnew;
                    double *vn_ynew = realloc(
                        vn_y, newc * sizeof(*vn_y)
                    );
                    if (!vn_ynew)
                        goto errororquit;
                    vn_y = vn_ynew;
                    double *vn_znew = realloc(
                        vn_z, newc * sizeof(*vn_z)
                    );
                    if (!vn_znew)
                        goto errororquit;
                    vn_z = vn_znew;
                    allocated_vn = newc;
                }
                vn_x[vn_count] = atof(v1buf);
                vn_y[vn_count] = atof(v2buf);
                vn_z[vn_count] = atof(v3buf);
                vn_count++;
            } 

            if (c >= 0) {
                linebuf[0] = '\0';
                continue;
            }
        }
        if (c < 0) {
            if (!vfs_feof(f))
                goto errororquit;
            break;
        }
        if (strlen(linebuf) < sizeof(linebuf) - 1) {
            linebuf[strlen(linebuf) + 1] = '\0';
            linebuf[strlen(linebuf)] = c;
        }
    }
    if (f) {
        vfs_fclose(f);
        f = NULL;
    }

    #ifdef DEBUG_MESH_LOADER
    int z = 0;
    while (z < v_count) {
        printf("horse3d/meshes.c: debug: OBJ V: %f,%f,%f\n",
               v_x[z], v_y[z], v_z[z]);
        z++;
    }
    z = 0;
    while (z < vt_count) {
        printf("horse3d/meshes.c: debug: OBJ VT: %f,%f\n",
               vt_x[z], vt_y[z]);
        z++;
    }
    z = 0;
    while (z < vn_count) {
        printf("horse3d/meshes.c: debug: OBJ VN: %f,%f,%f\n",
               vn_x[z], vn_y[z], vn_z[z]);
        z++;
    }
    printf(
        "horse3d/meshes.c: debug: OBJ FACE COUNT: %d\n",
        faces_count
    );
    #endif
    builtmesh = malloc(sizeof(*builtmesh));
    if (!builtmesh)
        goto errororquit;
    memset(builtmesh, 0, sizeof(*builtmesh));
    h3dmesh_geometry *builtmeshgeo =
        &(builtmesh->default_geometry);

    int firstpolygon = 1;
    int i = 0;
    while (i < faces_count) {
        if (face_vindex1[i] < 0 ||
                face_vindex1[i] >= v_count ||
                face_vindex2[i] < 0 ||
                face_vindex2[i] >= v_count ||
                face_vindex3[i] < 0 ||
                face_vindex3[i] >= v_count) {
            #ifdef DEBUG_MESH_LOADER
            printf(
                "horse3d/meshes.c: debug: OBJ invalid polygon %d! "
                "(vindex1:%d, vindex2: %d, vindex3:%d, "
                "vcount:%d)\n",
                i, face_vindex1[i], face_vindex2[i], face_vindex3[i],
                v_count
            );
            #endif
            i++;
        }
        double i1[3];
        i1[0] = (
            v_x[face_vindex2[i]] -
            v_x[face_vindex1[i]]
        );
        i1[1] = (
            v_y[face_vindex2[i]] -
            v_y[face_vindex1[i]]
        );
        i1[2] = (
            v_z[face_vindex2[i]] -
            v_z[face_vindex1[i]]
        );
        double i2[3];
        i2[0] = (
            v_x[face_vindex3[i]] -
            v_x[face_vindex1[i]]
        );
        i2[1] = (
            v_y[face_vindex3[i]] -
            v_y[face_vindex1[i]]
        );
        i2[2] = (
            v_z[face_vindex3[i]] -
            v_z[face_vindex1[i]]
        );
        double vnormalall_unnormalized[3];
        memset(vnormalall_unnormalized, 0,
               sizeof(vnormalall_unnormalized));
        double vnormalall[3];
        vec3_cross(vnormalall_unnormalized, i1, i2);
        if (fabs(vnormalall_unnormalized[0]) > 0.001 ||
                fabs(vnormalall_unnormalized[1]) > 0.001 ||
                fabs(vnormalall_unnormalized[2]) > 0.001)
            vec3_normalize(vnormalall, vnormalall_unnormalized);

        double vnormal1[3];
        double vnormal2[3];
        double vnormal3[3];

        if (face_vnindex1[i] >= 0 &&
                face_vnindex1[i] < vn_count) {
            vnormal1[0] = vn_x[face_vnindex1[i]];
            vnormal1[1] = vn_y[face_vnindex1[i]];
            vnormal1[2] = vn_z[face_vnindex1[i]];
        } else {
            vnormal1[0] = vnormalall[0];
            vnormal1[1] = vnormalall[1];
            vnormal1[2] = vnormalall[2];
        }
        if (face_vnindex2[i] >= 0 &&
                face_vnindex2[i] < vn_count) {
            vnormal2[0] = vn_x[face_vnindex2[i]];
            vnormal2[1] = vn_y[face_vnindex2[i]];
            vnormal2[2] = vn_z[face_vnindex2[i]];
        } else {
            vnormal2[0] = vnormalall[0];
            vnormal2[1] = vnormalall[1];
            vnormal2[2] = vnormalall[2];
        }
        if (face_vnindex3[i] >= 0 &&
                face_vnindex3[i] < vn_count) {
            vnormal3[0] = vn_x[face_vnindex3[i]];
            vnormal3[1] = vn_y[face_vnindex3[i]];
            vnormal3[2] = vn_z[face_vnindex3[i]];
        } else {
            vnormal3[0] = vnormalall[0];
            vnormal3[1] = vnormalall[1];
            vnormal3[2] = vnormalall[2];
        }

        double uv1[2];
        double uv2[2];
        double uv3[2];
        if (face_vtindex1[i] >= 0 &&
                face_vtindex1[i] < vt_count) {
            uv1[0] = vt_x[face_vtindex1[i]];
            uv1[1] = vt_y[face_vtindex1[i]];
        } else {
            uv1[0] = 0.0;
            uv1[1] = 0.0;
        }
        if (face_vtindex2[i] >= 0 &&
                face_vtindex2[i] < vt_count) {
            uv2[0] = vt_x[face_vtindex2[i]];
            uv2[1] = vt_y[face_vtindex2[i]];
        } else {
            uv2[0] = 0.0;
            uv2[1] = 0.0;
        }
        if (face_vtindex3[i] >= 0 &&
                face_vtindex3[i] < vt_count) {
            uv3[0] = vt_x[face_vtindex3[i]];
            uv3[1] = vt_y[face_vtindex3[i]];
        } else {
            uv3[0] = 0.0;
            uv3[1] = 0.0;
        }

        const char *texture_path = NULL;
        if (faces_material[i]) {
            int k = 0;
            while (k < material_count) {
                if (faces_material[i] &&
                        strcmp(material_names[k],
                               faces_material[i]) == 0) {
                    texture_path = material_diffuse_map[k];
                }
                k++;
            }
        }

        h3dtexture *tex = mesh_LocateModelReferencedTexture(
            path, texture_path
        );
        if (!tex) {
            #ifdef DEBUG_MESH_LOADER
            printf(
                "horse3d/meshes.c: debug: OBJ texture obtain fail "
                "(should NEVER happen, we should get blank instead): %s\n",
                texture_path
            );
            #endif
            goto errororquit;
        }

        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: OBJ POLYGON: "
            " 1: pos:%f,%f,%f uv:%f,%f normal:%f,%f,%f "
            " 2: pos:%f,%f,%f uv:%f,%f normal:%f,%f,%f "
            " 3: pos:%f,%f,%f uv:%f,%f normal:%f,%f,%f "
            " material: %s  texture: %s\n",
            v_x[face_vindex1[i]], v_y[face_vindex1[i]],
            v_z[face_vindex1[i]], uv1[0], uv1[1],
            vnormal1[0], vnormal1[1], vnormal1[2],
            v_x[face_vindex2[i]], v_y[face_vindex2[i]],
            v_z[face_vindex2[i]], uv2[0], uv2[1],
            vnormal2[0], vnormal2[1], vnormal2[2],
            v_x[face_vindex3[i]], v_y[face_vindex3[i]],
            v_z[face_vindex3[i]], uv3[0], uv3[1],
            vnormal3[0], vnormal3[1], vnormal3[2],
            faces_material[i], texture_path
        );
        #endif

        h3dmaterialprops props;
        memset(&props, 0, sizeof(props));
        int _error_invalidgeometry = 0;
        if (!meshgeo_AddPolygon(
                builtmeshgeo, tex, &props,
                v_x[face_vindex1[i]],
                -v_z[face_vindex1[i]],
                v_y[face_vindex1[i]],
                uv1[0], uv1[1],
                vnormal1[0], -vnormal1[2], vnormal1[1],
                v_x[face_vindex2[i]],
                -v_z[face_vindex2[i]],
                v_y[face_vindex2[i]],
                uv2[0], uv2[1],
                vnormal2[0], -vnormal2[2], vnormal2[1],
                v_x[face_vindex3[i]],
                -v_z[face_vindex3[i]],
                v_y[face_vindex3[i]],
                uv3[0], uv3[1],
                vnormal3[0], -vnormal3[2], vnormal3[1],
                loadinfo,
                &_error_invalidgeometry)) {
            if (!_error_invalidgeometry) {
                if (error)
                    *error = strdup(
                        "failed to add polygon, out of memory?"
                    );
                goto errororquit;
            } else {
                fprintf(stderr,
                    "horse3d/meshes.c: warning: OBJ "
                    "skipped invalid polygon in model %s\n",
                    path
                );
            }
        }
        i++;
    }
    if (builtmeshgeo->texturepart_count <= 0) {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: OBJ has not a "
            "single polygon, aborting.\n"
        );
        #endif
        if (error)
            *error = strdup("mesh has no polygons");
        goto errororquit;
    } else {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: OBJ parsing done!\n"
        );
        #endif
    }

    builtmesh->min_x = builtmesh->default_geometry.min_x;
    builtmesh->max_x = builtmesh->default_geometry.max_x;
    builtmesh->min_y = builtmesh->default_geometry.min_y;
    builtmesh->max_y = builtmesh->default_geometry.max_y;
    builtmesh->min_z = builtmesh->default_geometry.min_z;
    builtmesh->max_z = builtmesh->default_geometry.max_z;
    i = 0;
    while (i < builtmesh->default_geometry.texturepart_count) {
        if (builtmesh->default_geometry.
                texturepart[i].attribute_withalpha) {
            builtmesh->has_transparent_parts = 1;
            break;
        }
        i++;
    }
    _result = builtmesh;

    errororquit:
    if (f) {
        vfs_fclose(f);
        f = NULL;
    }
    if (v_x)
        free(v_x);
    if (v_y)
        free(v_y);
    if (v_z)
        free(v_z);
    if (vt_x)
        free(vt_x);
    if (vt_y)
        free(vt_y);
    if (vn_x)
        free(vn_x);
    if (vn_y)
        free(vn_y);
    if (vn_z)
        free(vn_z);
    if (faces_material) {
        int i = 0;
        while (i < faces_count) {
            if (faces_material[i])
                free(faces_material[i]);
            i++;
        }
        free(faces_material);
    }
    if (face_vindex1)
        free(face_vindex1);
    if (face_vindex2)
        free(face_vindex2);
    if (face_vindex3)
        free(face_vindex3);
    if (face_vtindex1)
        free(face_vtindex1);
    if (face_vtindex2)
        free(face_vtindex2);
    if (face_vtindex3)
        free(face_vtindex3);
    if (face_vnindex1)
        free(face_vnindex1);
    if (face_vnindex2)
        free(face_vnindex2);
    if (face_vnindex3)
        free(face_vnindex3);

    if (material_names) {
        int i = 0;
        while (i < material_count) {
            free(material_names[i]);
            i++;
        }
        free(material_names);
    }
    if (material_diffuse_map) {
        int i = 0;
        while (i < material_count) {
            free(material_diffuse_map[i]);
            i++;
        }
        free(material_diffuse_map);
    }

    if (!_result && builtmesh) {
        mesh_DestroyMesh(builtmesh);
    }

    if (mtldir_path)
        free(mtldir_path);

    #ifdef DEBUG_MESH_LOADER
    if (_result) {
        printf(
            "horse3d/meshes.c: debug: OBJ DONE. (parts: #%d, "
            "invis words: [",
            _result->default_geometry.texturepart_count
        );
        int i = 0;
        while (i < loadinfo->invisible_keywords_count) {
            if (i > 0)
                printf(",");
            printf("\"%s\"", loadinfo->invisible_keywords[i]);
            i++;
        }
        printf("], nocol words: [");
        i = 0;
        while (i < loadinfo->nocollision_keywords_count) {
            if (i > 0)
                printf(",");
            printf("\"%s\"", loadinfo->nocollision_keywords[i]);
            i++;
        }
        printf("]\n");
    }
    #endif

    return _result;
}
