#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "file_operations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strcasestr */
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>

/* ---------- Helpers ---------- */

static const char *find_case_insensitive(const char *haystack, const char *needle) {
    if (!*needle) return haystack;
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, strlen(needle)) == 0) return p;
    }
    return NULL;
}

static int is_safe_component(const char *name) {
    if (!name || !*name) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    if (strchr(name, '/') || strchr(name, '\\')) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 32) return 0;
    }
    return 1;
}

int fo_sanitize_path(const char *user_path, char *out_path, size_t out_size) {
    if (!user_path || !out_path || out_size < 32) return -1;

    /* Normalize empty / root */
    if (user_path[0] == '\0' || strcmp(user_path, "/") == 0 || strcmp(user_path, ".") == 0) {
        snprintf(out_path, out_size, "%s", STORAGE_ROOT);
        return 0;
    }

    /* Reject absolute and traversal attempts early */
    if (user_path[0] == '/' || strstr(user_path, "..") != NULL) {
        return -1;
    }

    /* Copy and validate each component */
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", user_path);

    char *save = NULL;
    char *token = strtok_r(tmp, "/", &save);
    char built[MAX_PATH_LEN];
    snprintf(built, sizeof(built), "%s", STORAGE_ROOT);

    while (token) {
        if (!is_safe_component(token)) return -1;
        size_t len = strlen(built);
        if (len + 1 + strlen(token) + 1 >= sizeof(built)) return -1;
        built[len] = '/';
        strcpy(built + len + 1, token);
        token = strtok_r(NULL, "/", &save);
    }

    /* Ensure final path stays under STORAGE_ROOT using realpath when possible */
    char root_real[PATH_MAX];
    if (realpath(STORAGE_ROOT, root_real) == NULL) {
        /* storage may not exist yet on first run */
        snprintf(out_path, out_size, "%s", built);
        return 0;
    }

    char resolved[PATH_MAX];
    if (realpath(built, resolved) != NULL) {
        if (strncmp(resolved, root_real, strlen(root_real)) != 0) {
            return -1; /* escaped */
        }
        snprintf(out_path, out_size, "%s", resolved);
    } else {
        /* Path does not exist yet (create cases) – still enforce prefix */
        if (strncmp(built, root_real, strlen(root_real)) != 0 &&
            strncmp(built, STORAGE_ROOT, strlen(STORAGE_ROOT)) != 0) {
            return -1;
        }
        snprintf(out_path, out_size, "%s", built);
    }
    return 0;
}

const char *fo_result_str(fo_result_t r) {
    switch (r) {
        case FO_OK: return "OK";
        case FO_NOT_FOUND: return "Not found";
        case FO_ALREADY_EXISTS: return "Already exists";
        case FO_INVALID_NAME: return "Invalid name";
        case FO_PERMISSION: return "Permission denied";
        case FO_TOO_LARGE: return "File too large";
        case FO_IS_DIR: return "Is a directory";
        case FO_NOT_DIR: return "Not a directory";
        default: return "Error";
    }
}

/* ---------- Core operations ---------- */

fo_result_t fo_create_file(const char *rel_path) {
    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return FO_ALREADY_EXISTS;
        if (errno == EACCES || errno == EPERM) return FO_PERMISSION;
        return FO_ERROR;
    }
    close(fd);
    return FO_OK;
}

fo_result_t fo_create_folder(const char *rel_path) {
    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST) return FO_ALREADY_EXISTS;
        if (errno == EACCES || errno == EPERM) return FO_PERMISSION;
        return FO_ERROR;
    }
    return FO_OK;
}

fo_result_t fo_delete(const char *rel_path) {
    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    /* Never allow deleting the storage root itself */
    char root_real[PATH_MAX], path_real[PATH_MAX];
    if (realpath(STORAGE_ROOT, root_real) && realpath(path, path_real) &&
        strcmp(root_real, path_real) == 0) {
        return FO_INVALID_NAME;
    }

    struct stat st;
    if (stat(path, &st) != 0) return FO_NOT_FOUND;

    if (S_ISDIR(st.st_mode)) {
        if (rmdir(path) != 0) return FO_ERROR; /* non-empty or other error */
    } else {
        if (unlink(path) != 0) return FO_ERROR;
    }
    return FO_OK;
}

fo_result_t fo_rename(const char *old_rel, const char *new_rel) {
    char old_path[MAX_PATH_LEN], new_path[MAX_PATH_LEN];
    if (fo_sanitize_path(old_rel, old_path, sizeof(old_path)) != 0 ||
        fo_sanitize_path(new_rel, new_path, sizeof(new_path)) != 0)
        return FO_INVALID_NAME;

    if (rename(old_path, new_path) != 0) {
        if (errno == ENOENT) return FO_NOT_FOUND;
        if (errno == EEXIST) return FO_ALREADY_EXISTS;
        return FO_ERROR;
    }
    return FO_OK;
}

fo_result_t fo_list_dir(const char *rel_path, fo_entry_t **entries, size_t *count) {
    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    DIR *dir = opendir(path);
    if (!dir) {
        if (errno == ENOENT) return FO_NOT_FOUND;
        return FO_ERROR;
    }

    size_t capacity = 64;
    size_t n = 0;
    fo_entry_t *list = calloc(capacity, sizeof(fo_entry_t));
    if (!list) {
        closedir(dir);
        return FO_ERROR;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        if (n >= capacity) {
            capacity *= 2;
            fo_entry_t *tmp = realloc(list, capacity * sizeof(fo_entry_t));
            if (!tmp) {
                free(list);
                closedir(dir);
                return FO_ERROR;
            }
            list = tmp;
        }

        fo_entry_t *e = &list[n];
        strncpy(e->name, de->d_name, MAX_FILENAME_LEN - 1);
        e->name[MAX_FILENAME_LEN - 1] = '\0';

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);

        struct stat st;
        if (stat(full, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            e->size   = st.st_size;
            e->mtime  = st.st_mtime;
            e->mode   = st.st_mode;
        } else {
            e->is_dir = 0;
            e->size = 0;
            e->mtime = 0;
            e->mode = 0;
        }
        n++;
    }
    closedir(dir);

    *entries = list;
    *count = n;
    return FO_OK;
}

void fo_free_entries(fo_entry_t *entries) {
    free(entries);
}

fo_result_t fo_get_info(const char *rel_path, fo_entry_t *info) {
    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    struct stat st;
    if (stat(path, &st) != 0) return FO_NOT_FOUND;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(info->name, base, MAX_FILENAME_LEN - 1);
    info->name[MAX_FILENAME_LEN - 1] = '\0';
    info->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    info->size   = st.st_size;
    info->mtime  = st.st_mtime;
    info->mode   = st.st_mode;
    return FO_OK;
}

static off_t calc_usage(const char *dirpath) {
    off_t total = 0;
    DIR *dir = opendir(dirpath);
    if (!dir) return 0;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", dirpath, de->d_name);

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            total += calc_usage(full);
        } else if (S_ISREG(st.st_mode)) {
            total += st.st_size;
        }
    }
    closedir(dir);
    return total;
}

off_t fo_get_storage_usage(void) {
    return calc_usage(STORAGE_ROOT);
}

fo_result_t fo_read_file(const char *rel_path, char **data, size_t *len) {
    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    struct stat st;
    if (stat(path, &st) != 0) return FO_NOT_FOUND;
    if (S_ISDIR(st.st_mode)) return FO_IS_DIR;
    if (st.st_size > MAX_UPLOAD_SIZE) return FO_TOO_LARGE;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return FO_ERROR;

    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) {
        close(fd);
        return FO_ERROR;
    }

    ssize_t n = read(fd, buf, (size_t)st.st_size);
    close(fd);

    if (n < 0) {
        free(buf);
        return FO_ERROR;
    }

    buf[n] = '\0';
    *data = buf;
    *len = (size_t)n;
    return FO_OK;
}

fo_result_t fo_write_file(const char *rel_path, const char *data, size_t len) {
    if (len > MAX_UPLOAD_SIZE) return FO_TOO_LARGE;

    char path[MAX_PATH_LEN];
    if (fo_sanitize_path(rel_path, path, sizeof(path)) != 0)
        return FO_INVALID_NAME;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) return FO_PERMISSION;
        return FO_ERROR;
    }

    ssize_t written = 0;
    while (written < (ssize_t)len) {
        ssize_t w = write(fd, data + written, len - written);
        if (w <= 0) {
            close(fd);
            return FO_ERROR;
        }
        written += w;
    }
    close(fd);
    return FO_OK;
}

/* Recursive name search (case-insensitive) */
static void search_recursive(const char *dirpath, const char *query,
                             fo_entry_t **list, size_t *n, size_t *cap) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", dirpath, de->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        /* simple case-insensitive substring match */
        if (find_case_insensitive(de->d_name, query) != NULL) {
            if (*n >= *cap) {
                *cap *= 2;
                fo_entry_t *tmp = realloc(*list, (*cap) * sizeof(fo_entry_t));
                if (!tmp) {
                    closedir(dir);
                    return;
                }
                *list = tmp;
            }
            fo_entry_t *e = &(*list)[*n];
            strncpy(e->name, de->d_name, MAX_FILENAME_LEN - 1);
            e->name[MAX_FILENAME_LEN - 1] = '\0';
            e->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            e->size   = st.st_size;
            e->mtime  = st.st_mtime;
            e->mode   = st.st_mode;
            (*n)++;
        }

        if (S_ISDIR(st.st_mode)) {
            search_recursive(full, query, list, n, cap);
        }
    }
    closedir(dir);
}

fo_result_t fo_search(const char *query, fo_entry_t **results, size_t *count) {
    if (!query || !*query) {
        *results = NULL;
        *count = 0;
        return FO_OK;
    }

    size_t capacity = 32;
    size_t n = 0;
    fo_entry_t *list = calloc(capacity, sizeof(fo_entry_t));
    if (!list) return FO_ERROR;

    search_recursive(STORAGE_ROOT, query, &list, &n, &capacity);

    *results = list;
    *count = n;
    return FO_OK;
}
