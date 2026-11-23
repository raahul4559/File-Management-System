#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strncasecmp, strcasecmp */
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#include "file_operations.h"

#define PORT 8080
#define BACKLOG 16
#define BUF_SIZE 65536
#define MAX_HEADER 8192

static volatile int running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static const char *find_case_insensitive(const char *haystack, const char *needle) {
    if (!*needle) return haystack;
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, strlen(needle)) == 0) return p;
    }
    return NULL;
}

/* ---------- HTTP helpers ---------- */

static void send_response(int client, int status, const char *status_text,
                          const char *content_type, const char *body, size_t body_len) {
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS, DELETE, PUT\r\n"
        "Access-Control-Allow-Headers: Content-Type, X-Filename, X-Path\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);

    write(client, header, n);
    if (body && body_len > 0) {
        write(client, body, body_len);
    }
}

static void send_json(int client, int status, const char *json) {
    send_response(client, status, status == 200 ? "OK" : "Error",
                  "application/json", json, strlen(json));
}

static void send_error(int client, int status, const char *msg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    send_json(client, status, buf);
}

/* Very simple header value extractor */
static int get_header(const char *req, const char *name, char *out, size_t out_sz) {
    char search[128];
    snprintf(search, sizeof(search), "\r\n%s:", name);
    const char *p = find_case_insensitive(req, search);
    if (!p) {
        /* try at beginning */
        snprintf(search, sizeof(search), "%s:", name);
        if (strncasecmp(req, search, strlen(search)) == 0)
            p = req;
        else
            return 0;
    } else {
        p += 2; /* skip \r\n */
    }
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    size_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i + 1 < out_sz) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* ---------- Static file serving ---------- */

static const char *mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css")  == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js")   == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".txt")  == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void serve_static(int client, const char *url_path) {
    char filepath[MAX_PATH_LEN];
    if (strcmp(url_path, "/") == 0)
        snprintf(filepath, sizeof(filepath), "frontend/index.html");
    else
        snprintf(filepath, sizeof(filepath), "frontend%s", url_path);

    /* Basic safety */
    if (strstr(filepath, "..")) {
        send_error(client, 403, "Forbidden");
        return;
    }

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        send_error(client, 404, "File not found");
        return;
    }

    struct stat st;
    fstat(fd, &st);
    size_t len = (size_t)st.st_size;

    char *body = malloc(len);
    if (!body) {
        close(fd);
        send_error(client, 500, "Out of memory");
        return;
    }
    read(fd, body, len);
    close(fd);

    send_response(client, 200, "OK", mime_type(filepath), body, len);
    free(body);
}

/* ---------- API handlers ---------- */

static void api_list(int client, const char *query) {
    char path[256] = "";
    if (query) {
        const char *p = strstr(query, "path=");
        if (p) {
            p += 5;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < sizeof(path)) {
                if (*p == '+') path[i++] = ' ';
                else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                    char hex[3] = {p[1], p[2], 0};
                    path[i++] = (char)strtol(hex, NULL, 16);
                    p += 2;
                } else path[i++] = *p;
                p++;
            }
            path[i] = '\0';
        }
    }

    fo_entry_t *entries = NULL;
    size_t count = 0;
    fo_result_t r = fo_list_dir(path, &entries, &count);
    if (r != FO_OK) {
        send_error(client, 400, fo_result_str(r));
        return;
    }

    /* Build simple JSON array */
    size_t cap = 4096;
    char *json = malloc(cap);
    if (!json) {
        fo_free_entries(entries);
        send_error(client, 500, "Out of memory");
        return;
    }
    size_t pos = 0;
    pos += snprintf(json + pos, cap - pos, "{\"ok\":true,\"path\":\"%s\",\"entries\":[", path);

    for (size_t i = 0; i < count; i++) {
        if (pos + 512 > cap) {
            cap *= 2;
            char *tmp = realloc(json, cap);
            if (!tmp) break;
            json = tmp;
        }
        char timestr[64];
        struct tm *tm = localtime(&entries[i].mtime);
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", tm);

        pos += snprintf(json + pos, cap - pos,
            "%s{\"name\":\"%s\",\"is_dir\":%s,\"size\":%lld,\"mtime\":\"%s\"}",
            i ? "," : "",
            entries[i].name,
            entries[i].is_dir ? "true" : "false",
            (long long)entries[i].size,
            timestr);
    }
    pos += snprintf(json + pos, cap - pos, "]}");

    send_json(client, 200, json);
    free(json);
    fo_free_entries(entries);
}

static void api_usage(int client) {
    off_t usage = fo_get_storage_usage();
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"bytes\":%lld}", (long long)usage);
    send_json(client, 200, buf);
}

static void api_create(int client, const char *body) {
    /* Expect JSON-like: {"path":"...","type":"file"|"folder"}  (very simple parser) */
    char path[256] = {0};
    char type[16] = {0};

    const char *p = strstr(body, "\"path\"");
    if (p) {
        p = strchr(p + 6, '"');
        if (p) {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i + 1 < sizeof(path)) path[i++] = *p++;
            path[i] = '\0';
        }
    }
    p = strstr(body, "\"type\"");
    if (p) {
        p = strchr(p + 6, '"');
        if (p) {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i + 1 < sizeof(type)) type[i++] = *p++;
            type[i] = '\0';
        }
    }

    if (!path[0]) {
        send_error(client, 400, "Missing path");
        return;
    }

    fo_result_t r;
    if (strcmp(type, "folder") == 0)
        r = fo_create_folder(path);
    else
        r = fo_create_file(path);

    if (r == FO_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"message\":\"Created\"}");
        send_json(client, 200, buf);
    } else {
        send_error(client, 400, fo_result_str(r));
    }
}

static void api_delete(int client, const char *body) {
    char path[256] = {0};
    const char *p = strstr(body, "\"path\"");
    if (p) {
        p = strchr(p + 6, '"');
        if (p) {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i + 1 < sizeof(path)) path[i++] = *p++;
            path[i] = '\0';
        }
    }
    if (!path[0]) {
        send_error(client, 400, "Missing path");
        return;
    }

    fo_result_t r = fo_delete(path);
    if (r == FO_OK) {
        send_json(client, 200, "{\"ok\":true,\"message\":\"Deleted\"}");
    } else {
        send_error(client, 400, fo_result_str(r));
    }
}

static void api_rename(int client, const char *body) {
    char oldp[256] = {0}, newp[256] = {0};
    const char *p = strstr(body, "\"old\"");
    if (p) {
        p = strchr(p + 5, '"');
        if (p) {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i + 1 < sizeof(oldp)) oldp[i++] = *p++;
            oldp[i] = '\0';
        }
    }
    p = strstr(body, "\"new\"");
    if (p) {
        p = strchr(p + 5, '"');
        if (p) {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i + 1 < sizeof(newp)) newp[i++] = *p++;
            newp[i] = '\0';
        }
    }
    if (!oldp[0] || !newp[0]) {
        send_error(client, 400, "Missing old or new path");
        return;
    }

    fo_result_t r = fo_rename(oldp, newp);
    if (r == FO_OK)
        send_json(client, 200, "{\"ok\":true,\"message\":\"Renamed\"}");
    else
        send_error(client, 400, fo_result_str(r));
}

static void api_read(int client, const char *query) {
    char path[256] = {0};
    if (query) {
        const char *p = strstr(query, "path=");
        if (p) {
            p += 5;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < sizeof(path)) {
                if (*p == '%') {
                    char hex[3] = {p[1], p[2], 0};
                    path[i++] = (char)strtol(hex, NULL, 16);
                    p += 2;
                } else path[i++] = *p;
                p++;
            }
            path[i] = '\0';
        }
    }
    if (!path[0]) {
        send_error(client, 400, "Missing path");
        return;
    }

    char *data = NULL;
    size_t len = 0;
    fo_result_t r = fo_read_file(path, &data, &len);
    if (r != FO_OK) {
        send_error(client, 400, fo_result_str(r));
        return;
    }

    int binary = 0;
    size_t control_bytes = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t'))
            control_bytes++;
    }
    if (len > 0 && (control_bytes * 10 > len))
        binary = 1;

    if (binary) {
        send_json(client, 200, "{\"ok\":true,\"is_text\":false,\"content\":\"\"}");
        free(data);
        return;
    }

    /* Escape for JSON (very basic) */
    size_t cap = len * 2 + 128;
    char *json = malloc(cap);
    if (!json) {
        free(data);
        send_error(client, 500, "Out of memory");
        return;
    }
    size_t pos = 0;
    pos += snprintf(json + pos, cap - pos, "{\"ok\":true,\"content\":\"");
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '"' || c == '\\') {
            json[pos++] = '\\';
            json[pos++] = c;
        } else if (c == '\n') {
            json[pos++] = '\\';
            json[pos++] = 'n';
        } else if (c == '\r') {
            json[pos++] = '\\';
            json[pos++] = 'r';
        } else if (c == '\t') {
            json[pos++] = '\\';
            json[pos++] = 't';
        } else if ((unsigned char)c < 32) {
            /* skip control */
        } else {
            json[pos++] = c;
        }
        if (pos + 8 >= cap) {
            cap *= 2;
            char *tmp = realloc(json, cap);
            if (!tmp) break;
            json = tmp;
        }
    }
    pos += snprintf(json + pos, cap - pos, "\",\"is_text\":true}");
    send_json(client, 200, json);
    free(json);
    free(data);
}

static void api_search(int client, const char *query) {
    char q[128] = {0};
    if (query) {
        const char *p = strstr(query, "q=");
        if (p) {
            p += 2;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < sizeof(q)) {
                if (*p == '+') q[i++] = ' ';
                else if (*p == '%') {
                    char hex[3] = {p[1], p[2], 0};
                    q[i++] = (char)strtol(hex, NULL, 16);
                    p += 2;
                } else q[i++] = *p;
                p++;
            }
            q[i] = '\0';
        }
    }

    fo_entry_t *results = NULL;
    size_t count = 0;
    fo_result_t r = fo_search(q, &results, &count);
    if (r != FO_OK) {
        send_error(client, 400, fo_result_str(r));
        return;
    }

    size_t cap = 4096;
    char *json = malloc(cap);
    size_t pos = 0;
    pos += snprintf(json + pos, cap - pos, "{\"ok\":true,\"query\":\"%s\",\"results\":[", q);
    for (size_t i = 0; i < count; i++) {
        if (pos + 256 > cap) {
            cap *= 2;
            json = realloc(json, cap);
        }
        pos += snprintf(json + pos, cap - pos,
            "%s{\"name\":\"%s\",\"is_dir\":%s,\"size\":%lld}",
            i ? "," : "", results[i].name,
            results[i].is_dir ? "true" : "false",
            (long long)results[i].size);
    }
    pos += snprintf(json + pos, cap - pos, "]}");
    send_json(client, 200, json);
    free(json);
    fo_free_entries(results);
}

/* Upload: expects raw body + headers X-Filename and optional X-Path */
static void api_upload(int client, const char *headers, const char *body, size_t body_len) {
    char filename[256] = {0};
    char path_prefix[256] = {0};

    get_header(headers, "X-Filename", filename, sizeof(filename));
    get_header(headers, "X-Path", path_prefix, sizeof(path_prefix));

    if (!filename[0]) {
        send_error(client, 400, "Missing X-Filename header");
        return;
    }

    /* sanitize filename */
    for (char *p = filename; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') *p = '_';
    }

    char full_rel[512];
    if (path_prefix[0])
        snprintf(full_rel, sizeof(full_rel), "%s/%s", path_prefix, filename);
    else
        snprintf(full_rel, sizeof(full_rel), "%s", filename);

    fo_result_t r = fo_write_file(full_rel, body, body_len);
    if (r == FO_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"message\":\"Uploaded\",\"path\":\"%s\",\"size\":%zu}",
                 full_rel, body_len);
        send_json(client, 200, buf);
    } else {
        send_error(client, 400, fo_result_str(r));
    }
}

/* Download */
static void api_download(int client, const char *query) {
    char path[256] = {0};
    if (query) {
        const char *p = strstr(query, "path=");
        if (p) {
            p += 5;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < sizeof(path)) {
                if (*p == '%') {
                    char hex[3] = {p[1], p[2], 0};
                    path[i++] = (char)strtol(hex, NULL, 16);
                    p += 2;
                } else path[i++] = *p;
                p++;
            }
            path[i] = '\0';
        }
    }
    if (!path[0]) {
        send_error(client, 400, "Missing path");
        return;
    }

    char *data = NULL;
    size_t len = 0;
    fo_result_t r = fo_read_file(path, &data, &len);
    if (r != FO_OK) {
        send_error(client, 404, fo_result_str(r));
        return;
    }

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        base, len);
    write(client, header, n);
    write(client, data, len);
    free(data);
}

/* ---------- Request router ---------- */

static void handle_client(int client) {
    size_t buf_size = MAX_HEADER + MAX_UPLOAD_SIZE + 1;
    char *buf = malloc(buf_size);
    if (!buf) {
        close(client);
        return;
    }

    ssize_t n = read(client, buf, buf_size - 1);
    if (n <= 0) {
        free(buf);
        close(client);
        return;
    }
    buf[n] = '\0';

    char *body_marker = strstr(buf, "\r\n\r\n");
    if (body_marker) {
        body_marker += 4;
        size_t content_length = 0;
        const char *headers_end = body_marker;
        char length_header[64] = {0};
        char *header_copy = strndup(buf, (size_t)(headers_end - buf));
        if (header_copy) {
            if (get_header(header_copy, "Content-Length", length_header, sizeof(length_header))) {
                content_length = (size_t)strtoull(length_header, NULL, 10);
            }
            free(header_copy);
        }

        size_t target = (size_t)(body_marker - buf) + content_length;
        if (content_length > MAX_UPLOAD_SIZE) target = buf_size - 1;

        while ((size_t)n < target && (size_t)n < buf_size - 1) {
            ssize_t received = read(client, buf + n, buf_size - 1 - (size_t)n);
            if (received <= 0) break;
            n += received;
            buf[n] = '\0';
        }
    }

    /* Parse request line */
    char method[16] = {0}, url[512] = {0}, version[16] = {0};
    sscanf(buf, "%15s %511s %15s", method, url, version);

    /* Separate path and query */
    char *query = strchr(url, '?');
    if (query) {
        *query = '\0';
        query++;
    }

    /* Find body (after \r\n\r\n) */
    char *body = strstr(buf, "\r\n\r\n");
    size_t body_len = 0;
    if (body) {
        body += 4;
        body_len = n - (body - buf);
    }

    /* CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client, 204, "No Content", "text/plain", NULL, 0);
        free(buf);
        close(client);
        return;
    }

    /* API routes */
    if (strncmp(url, "/api/", 5) == 0) {
        if (strcmp(url, "/api/list") == 0 && strcmp(method, "GET") == 0) {
            api_list(client, query);
        } else if (strcmp(url, "/api/usage") == 0 && strcmp(method, "GET") == 0) {
            api_usage(client);
        } else if (strcmp(url, "/api/create") == 0 && strcmp(method, "POST") == 0) {
            api_create(client, body ? body : "");
        } else if (strcmp(url, "/api/delete") == 0 && strcmp(method, "POST") == 0) {
            api_delete(client, body ? body : "");
        } else if (strcmp(url, "/api/rename") == 0 && strcmp(method, "POST") == 0) {
            api_rename(client, body ? body : "");
        } else if (strcmp(url, "/api/read") == 0 && strcmp(method, "GET") == 0) {
            api_read(client, query);
        } else if (strcmp(url, "/api/search") == 0 && strcmp(method, "GET") == 0) {
            api_search(client, query);
        } else if (strcmp(url, "/api/upload") == 0 && strcmp(method, "POST") == 0) {
            api_upload(client, buf, body ? body : "", body_len);
        } else if (strcmp(url, "/api/download") == 0 && strcmp(method, "GET") == 0) {
            api_download(client, query);
        } else {
            send_error(client, 404, "Unknown API endpoint");
        }
    } else {
        /* Static frontend */
        serve_static(client, url);
    }

    free(buf);
    close(client);
}

/* ---------- Main ---------- */

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Ensure storage directory exists */
    mkdir(STORAGE_ROOT, 0755);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("========================================\n");
    printf("  File Management System Server\n");
    printf("  Listening on http://127.0.0.1:%d\n", PORT);
    printf("  Storage root: %s\n", STORAGE_ROOT);
    printf("  Press Ctrl+C to stop\n");
    printf("========================================\n");

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        handle_client(client);
    }

    close(server_fd);
    printf("\nServer stopped.\n");
    return 0;
}
