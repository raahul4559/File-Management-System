#ifndef FILE_OPERATIONS_H
#define FILE_OPERATIONS_H

#include <stddef.h>
#include <sys/stat.h>
#include <time.h>

#define STORAGE_ROOT "/Users/baldev/Downloads/FileManagementSystem/storage"
#define MAX_PATH_LEN 1024
#define MAX_FILENAME_LEN 256
#define MAX_UPLOAD_SIZE (10 * 1024 * 1024)  /* 10 MB */

typedef enum {
    FO_OK = 0,
    FO_ERROR = -1,
    FO_NOT_FOUND = -2,
    FO_ALREADY_EXISTS = -3,
    FO_INVALID_NAME = -4,
    FO_PERMISSION = -5,
    FO_TOO_LARGE = -6,
    FO_IS_DIR = -7,
    FO_NOT_DIR = -8
} fo_result_t;

typedef struct {
    char name[MAX_FILENAME_LEN];
    int is_dir;
    off_t size;
    time_t mtime;
    mode_t mode;
} fo_entry_t;

/* Path safety - prevents directory traversal */
int fo_sanitize_path(const char *user_path, char *out_path, size_t out_size);

/* Core operations */
fo_result_t fo_create_file(const char *rel_path);
fo_result_t fo_create_folder(const char *rel_path);
fo_result_t fo_delete(const char *rel_path);
fo_result_t fo_rename(const char *old_rel, const char *new_rel);

/* Listing & metadata */
fo_result_t fo_list_dir(const char *rel_path, fo_entry_t **entries, size_t *count);
void        fo_free_entries(fo_entry_t *entries);
fo_result_t fo_get_info(const char *rel_path, fo_entry_t *info);
off_t       fo_get_storage_usage(void);

/* Content */
fo_result_t fo_read_file(const char *rel_path, char **data, size_t *len);
fo_result_t fo_write_file(const char *rel_path, const char *data, size_t len);

/* Search */
fo_result_t fo_search(const char *query, fo_entry_t **results, size_t *count);

/* Helper for JSON-friendly status */
const char *fo_result_str(fo_result_t r);

#endif /* FILE_OPERATIONS_H */
