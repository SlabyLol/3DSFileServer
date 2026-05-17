#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define PORT        3760
#define BUFFER_SIZE 8192
#define MAX_PATH    512
#define MAX_PENDING 5

// ---- Console colors ----
#define COL_RED   "\x1b[31m"
#define COL_GREEN "\x1b[32m"
#define COL_CYAN  "\x1b[36m"
#define COL_WHITE "\x1b[37m"
#define COL_RESET "\x1b[0m"

static int  server_fd      = -1;
static char client_ip[32]  = {0};
static char last_action[128] = {0};
static int  confirm_pending  = 0;
static char pending_method[16]      = {0};
static char pending_path[MAX_PATH]  = {0};
static char pending_body[BUFFER_SIZE] = {0};
static int  pending_client = -1;

// Upload staging buffer (held in RAM until confirmed)
static char  *upload_data            = NULL;
static size_t upload_data_len        = 0;
static char   upload_savepath[MAX_PATH] = {0};
static char   upload_redir[MAX_PATH]    = {0};

// Edit/save staging buffer (held in RAM until confirmed)
static char  *edit_data     = NULL;
static size_t edit_data_len = 0;

// ---- Helpers ----
static void url_decode(char *dst, const char *src, size_t dstlen) {
    size_t i = 0;
    while (*src && i < dstlen - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = 0;
}

static void get_param(const char *body, const char *key, char *out, size_t outlen) {
    char searchkey[64];
    snprintf(searchkey, sizeof(searchkey), "%s=", key);
    const char *p = strstr(body, searchkey);
    if (!p) { out[0] = 0; return; }
    p += strlen(searchkey);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= outlen) len = outlen - 1;
    char tmp[MAX_PATH];
    memcpy(tmp, p, len);
    tmp[len] = 0;
    url_decode(out, tmp, outlen);
}

// ---- HTTP helpers ----
static void send_response(int client, int code, const char *ctype,
                          const char *body, size_t bodylen) {
    char header[512];
    const char *status = (code == 200) ? "OK" :
                         (code == 302) ? "Found" :
                         (code == 404) ? "Not Found" : "Internal Server Error";
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", code, status, ctype, bodylen);
    send(client, header, hlen, 0);
    if (body && bodylen > 0)
        send(client, body, bodylen, 0);
}

static void send_redirect(int client, const char *location) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        location);
    send(client, buf, n, 0);
}

// ---- File listing page ----
static void serve_file_list(int client, const char *path) {
    char fullpath[MAX_PATH];
    snprintf(fullpath, sizeof(fullpath), "sdmc:%s", path);

    DIR *dir = opendir(fullpath);
    if (!dir) {
        send_response(client, 404, "text/plain", "Directory not found", 19);
        return;
    }

    char *html = (char *)malloc(65536);
    if (!html) { closedir(dir); return; }

    int pos = 0;
    pos += snprintf(html + pos, 65536 - pos,
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>3DS File Server</title>"
        "<style>"
        "body{background:#1a1a2e;color:#e0e0e0;font-family:'Courier New',monospace;margin:0;padding:10px;}"
        "h1{color:#00d4ff;text-align:center;font-size:1.4em;margin-bottom:5px;}"
        ".path{color:#888;font-size:.85em;text-align:center;margin-bottom:15px;word-break:break-all;}"
        "table{width:100%%;border-collapse:collapse;}"
        "tr:nth-child(even){background:#16213e;}"
        "td,th{padding:7px 10px;border-bottom:1px solid #0f3460;text-align:left;font-size:.9em;}"
        "th{color:#00d4ff;background:#0f3460;}"
        "a{color:#00d4ff;text-decoration:none;}"
        "a:hover{color:#fff;}"
        ".btn{display:inline-block;padding:5px 12px;border-radius:4px;font-size:.8em;cursor:pointer;border:none;}"
        ".del{background:#c0392b;color:#fff;}"
        ".ren{background:#2980b9;color:#fff;}"
        ".dl{background:#27ae60;color:#fff;}"
        ".ed{background:#8e44ad;color:#fff;}"
        "form.inline{display:inline;}"
        ".toolbar{margin:10px 0;display:flex;gap:8px;flex-wrap:wrap;}"
        "input[type=text]{background:#0f3460;border:1px solid #00d4ff;color:#e0e0e0;padding:5px 8px;border-radius:4px;font-size:.85em;}"
        "input[type=submit]{background:#00d4ff;color:#000;font-weight:bold;padding:5px 12px;border:none;border-radius:4px;cursor:pointer;font-size:.85em;}"
        ".dir{color:#f39c12;}"
        ".info{text-align:center;font-size:.75em;color:#555;margin-top:20px;}"
        "</style></head><body>"
        "<h1>&#x1F4BE; 3DS File Server</h1>"
        "<div class='path'>%s</div>", path);

    // Toolbar: upload, new folder, new file
    pos += snprintf(html + pos, 65536 - pos,
        "<div class='toolbar'>"
        "<form method='POST' enctype='multipart/form-data' action='/upload?path=%s'>"
        "<input type='file' name='file'>"
        "<input type='submit' value='&#x2B06; Upload'>"
        "</form>"
        "<form method='POST' action='/mkdir'>"
        "<input type='hidden' name='path' value='%s'>"
        "<input type='text' name='name' placeholder='New folder'>"
        "<input type='submit' value='&#x1F4C1; Create'>"
        "</form>"
        "<form method='POST' action='/newfile'>"
        "<input type='hidden' name='path' value='%s'>"
        "<input type='text' name='name' placeholder='NewFile.txt'>"
        "<input type='submit' value='&#x1F4C4; New File'>"
        "</form>"
        "</div>", path, path, path);

    // Table header
    pos += snprintf(html + pos, 65536 - pos,
        "<table><tr><th>Name</th><th>Type</th><th>Actions</th></tr>");

    // Parent directory link
    if (strcmp(path, "/") != 0) {
        char parent[MAX_PATH];
        strncpy(parent, path, sizeof(parent));
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent) *slash = 0;
        else strcpy(parent, "/");
        pos += snprintf(html + pos, 65536 - pos,
            "<tr><td><a href='/browse?path=%s'>&#x1F519; ..</a></td><td>DIR</td><td></td></tr>",
            parent);
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char entpath[MAX_PATH];
        if (strcmp(path, "/") == 0)
            snprintf(entpath, sizeof(entpath), "/%s", ent->d_name);
        else
            snprintf(entpath, sizeof(entpath), "%s/%s", path, ent->d_name);

        char fullent[MAX_PATH];
        snprintf(fullent, sizeof(fullent), "sdmc:%s", entpath);
        struct stat st;
        int is_dir = 0;
        if (stat(fullent, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = 1;

        if (is_dir) {
            pos += snprintf(html + pos, 65536 - pos,
                "<tr><td class='dir'><a href='/browse?path=%s'>&#x1F4C1; %s</a></td><td>DIR</td>"
                "<td>"
                "<form class='inline' method='POST' action='/delete'>"
                "<input type='hidden' name='path' value='%s'>"
                "<button class='btn del' type='submit'>&#x1F5D1;</button>"
                "</form> "
                "<form class='inline' method='POST' action='/rename'>"
                "<input type='hidden' name='oldpath' value='%s'>"
                "<input type='text' name='newname' placeholder='New name' style='width:100px'>"
                "<button class='btn ren' type='submit'>&#x270F;</button>"
                "</form>"
                "</td></tr>",
                entpath, ent->d_name, entpath, entpath);
        } else {
            pos += snprintf(html + pos, 65536 - pos,
                "<tr><td><a href='/download?path=%s'>&#x1F4C4; %s</a></td><td>FILE</td>"
                "<td>"
                "<a href='/download?path=%s' class='btn dl'>&#x2B07;</a> "
                "<a href='/edit?path=%s' class='btn ed'>&#x270F;</a> "
                "<form class='inline' method='POST' action='/delete'>"
                "<input type='hidden' name='path' value='%s'>"
                "<button class='btn del' type='submit'>&#x1F5D1;</button>"
                "</form> "
                "<form class='inline' method='POST' action='/rename'>"
                "<input type='hidden' name='oldpath' value='%s'>"
                "<input type='text' name='newname' placeholder='New name' style='width:100px'>"
                "<button class='btn ren' type='submit'>&#x270F;</button>"
                "</form>"
                "</td></tr>",
                entpath, ent->d_name, entpath, entpath, entpath, entpath);
        }
    }
    closedir(dir);

    pos += snprintf(html + pos, 65536 - pos,
        "</table><div class='info'>3DS File Server &bull; Port %d</div></body></html>", PORT);

    send_response(client, 200, "text/html; charset=UTF-8", html, pos);
    free(html);
}

// ---- Text editor page ----
static void serve_editor(int client, const char *path) {
    char fullpath[MAX_PATH];
    snprintf(fullpath, sizeof(fullpath), "sdmc:%s", path);

    // Read file into buffer (up to 64 KB displayed)
    FILE *f = fopen(fullpath, "rb");
    if (!f) {
        send_response(client, 404, "text/plain", "File not found", 14);
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t readlen = (fsize > 65535) ? 65535 : (size_t)fsize;
    char *content = (char *)malloc(readlen + 1);
    if (!content) { fclose(f); return; }
    fread(content, 1, readlen, f);
    fclose(f);
    content[readlen] = 0;

    // HTML-escape content for <textarea>
    char *escaped = (char *)malloc(readlen * 6 + 1);
    if (!escaped) { free(content); return; }
    size_t ei = 0;
    for (size_t i = 0; i < readlen && ei < readlen * 6; i++) {
        unsigned char c = (unsigned char)content[i];
        if      (c == '<')  { memcpy(escaped + ei, "&lt;",   4); ei += 4; }
        else if (c == '>')  { memcpy(escaped + ei, "&gt;",   4); ei += 4; }
        else if (c == '&')  { memcpy(escaped + ei, "&amp;",  5); ei += 5; }
        else if (c == '"')  { memcpy(escaped + ei, "&quot;", 6); ei += 6; }
        else                { escaped[ei++] = c; }
    }
    escaped[ei] = 0;
    free(content);

    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;

    // Parent path for back link
    char parent[MAX_PATH];
    strncpy(parent, path, sizeof(parent));
    char *sl = strrchr(parent, '/');
    if (sl && sl != parent) *sl = 0; else strcpy(parent, "/");

    char *html = (char *)malloc(131072);
    if (!html) { free(escaped); return; }

    int pos = 0;
    pos += snprintf(html + pos, 131072 - pos,
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Edit: %s</title>"
        "<style>"
        "body{background:#1a1a2e;color:#e0e0e0;font-family:'Courier New',monospace;"
        "margin:0;padding:10px;display:flex;flex-direction:column;height:100vh;box-sizing:border-box;}"
        "h1{color:#8e44ad;font-size:1.2em;margin:0 0 4px;}"
        ".path{color:#888;font-size:.8em;margin-bottom:8px;word-break:break-all;}"
        "textarea{flex:1;width:100%%;background:#0d0d1a;color:#e0e0e0;border:1px solid #8e44ad;"
        "border-radius:4px;padding:10px;font-family:'Courier New',monospace;font-size:.9em;"
        "resize:none;box-sizing:border-box;tab-size:4;}"
        ".bar{display:flex;gap:8px;margin-top:8px;align-items:center;}"
        ".save{background:#8e44ad;color:#fff;font-weight:bold;padding:8px 20px;border:none;"
        "border-radius:4px;cursor:pointer;font-size:.9em;}"
        ".save:hover{background:#9b59b6;}"
        ".back{background:#0f3460;color:#00d4ff;padding:8px 16px;border:none;border-radius:4px;"
        "cursor:pointer;font-size:.9em;text-decoration:none;display:inline-block;}"
        ".info{color:#555;font-size:.75em;margin-left:auto;}"
        ".warn{color:#e67e22;font-size:.75em;display:none;}"
        "</style></head><body>"
        "<h1>&#x270F; Edit File</h1>"
        "<div class='path'>%s</div>"
        "<form method='POST' action='/save' style='display:contents'>"
        "<input type='hidden' name='path' value='%s'>"
        "<textarea name='content' id='ta' spellcheck='false'>%s</textarea>"
        "<div class='bar'>"
        "<button class='save' type='submit'>&#x1F4BE; Save</button>"
        "<a class='back' href='/browse?path=%s'>&#x2190; Back</a>"
        "<span class='info' id='info'>%zu bytes loaded</span>"
        "<span class='warn' id='warn'>&#x26A0; File exceeds 64 KB — only first 64 KB shown</span>"
        "</div>"
        "</form>"
        "<script>"
        "var ta=document.getElementById('ta');"
        "var info=document.getElementById('info');"
        "ta.addEventListener('input',function(){"
        "info.textContent=new Blob([ta.value]).size+' bytes';});"
        "%s"
        "ta.addEventListener('keydown',function(e){"
        "if(e.key==='Tab'){e.preventDefault();"
        "var s=ta.selectionStart,end=ta.selectionEnd;"
        "ta.value=ta.value.substring(0,s)+'\\t'+ta.value.substring(end);"
        "ta.selectionStart=ta.selectionEnd=s+1;}});"
        "</script>"
        "</body></html>",
        fname, path, path, escaped, parent, readlen,
        (fsize > 65535) ? "document.getElementById('warn').style.display='inline';" : "");

    free(escaped);
    send_response(client, 200, "text/html; charset=UTF-8", html, pos);
    free(html);
}

// ---- Console confirmation screen ----
static void show_confirm(const char *action_desc) {
    consoleClear();
    printf(COL_CYAN "\n  3DS File Server\n\n" COL_RESET);
    printf("  Client: " COL_GREEN "%s\n\n" COL_RESET, client_ip);
    printf("  Action:\n");
    printf("  " COL_WHITE "%s\n\n" COL_RESET, action_desc);
    printf("  " COL_GREEN "[A]" COL_RESET " Yes    " COL_RED "[B]" COL_RESET " No\n");
}

static void show_main_screen(u32 ip) {
    consoleClear();
    printf(COL_CYAN "\n  3DS File Server\n\n" COL_RESET);
    printf("  IP:   " COL_GREEN "%lu.%lu.%lu.%lu" COL_RESET "\n",
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    printf("  Port: " COL_GREEN "%d\n\n" COL_RESET, PORT);
    printf("  Open in browser:\n");
    printf("  " COL_WHITE "http://%lu.%lu.%lu.%lu:%d\n\n" COL_RESET,
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, PORT);
    if (last_action[0])
        printf("  Last confirmed: " COL_GREEN "%s\n" COL_RESET, last_action);
    printf("\n  " COL_RED "[START]" COL_RESET " Quit\n");
}

// ---- Read one HTTP line ----
static void read_line(int fd, char *buf, size_t sz) {
    size_t i = 0;
    while (i < sz - 1) {
        char c;
        int r = recv(fd, &c, 1, 0);
        if (r <= 0) break;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = 0;
}

// ---- Handle one HTTP request ----
static void handle_client(int client) {
    char line[1024];
    char method[16], url[MAX_PATH], version[16];

    read_line(client, line, sizeof(line));
    sscanf(line, "%15s %511s %15s", method, url, version);

    // Read headers
    int  content_length = 0;
    char content_type[128] = {0};
    while (1) {
        read_line(client, line, sizeof(line));
        if (line[0] == 0) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0)
            content_length = atoi(line + 16);
        if (strncasecmp(line, "Content-Type:", 13) == 0)
            strncpy(content_type, line + 14, sizeof(content_type) - 1);
    }

    // Read body
    char body[BUFFER_SIZE] = {0};
    if (content_length > 0 && content_length < (int)sizeof(body) - 1) {
        int total = 0;
        while (total < content_length) {
            int r = recv(client, body + total, content_length - total, 0);
            if (r <= 0) break;
            total += r;
        }
        body[total] = 0;
    }

    // Parse URL and query string
    char path_param[MAX_PATH] = "/";
    char *qmark = strchr(url, '?');
    char urlpath[MAX_PATH];
    if (qmark) {
        size_t plen = qmark - url;
        memcpy(urlpath, url, plen); urlpath[plen] = 0;
        get_param(qmark + 1, "path", path_param, sizeof(path_param));
    } else {
        strncpy(urlpath, url, sizeof(urlpath) - 1);
    }
    if (path_param[0] == 0) strcpy(path_param, "/");

    // ---- Routes ----
    if (strcmp(urlpath, "/") == 0 || strcmp(urlpath, "/browse") == 0) {
        serve_file_list(client, path_param);

    } else if (strcmp(urlpath, "/download") == 0) {
        char fullpath[MAX_PATH];
        snprintf(fullpath, sizeof(fullpath), "sdmc:%s", path_param);
        FILE *f = fopen(fullpath, "rb");
        if (!f) { send_response(client, 404, "text/plain", "File not found", 14); return; }
        fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
        char header[512];
        const char *fname = strrchr(path_param, '/');
        fname = fname ? fname + 1 : path_param;
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Disposition: attachment; filename=\"%s\"\r\n"
            "Content-Type: application/octet-stream\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
            fname, fsize);
        send(client, header, hlen, 0);
        char *fbuf = (char *)malloc(4096);
        if (fbuf) {
            size_t n;
            while ((n = fread(fbuf, 1, 4096, f)) > 0)
                send(client, fbuf, n, 0);
            free(fbuf);
        }
        fclose(f);

    } else if (strcmp(urlpath, "/delete") == 0 && strcmp(method, "POST") == 0) {
        char dpath[MAX_PATH];
        get_param(body, "path", dpath, sizeof(dpath));
        snprintf(pending_method, sizeof(pending_method), "DELETE");
        strncpy(pending_path, dpath, sizeof(pending_path) - 1);
        pending_client  = client;
        confirm_pending = 1;
        snprintf(last_action, sizeof(last_action), "Delete: %s", dpath);
        show_confirm(last_action);
        return; // keep client open

    } else if (strcmp(urlpath, "/rename") == 0 && strcmp(method, "POST") == 0) {
        char oldpath[MAX_PATH], newname[MAX_PATH];
        get_param(body, "oldpath", oldpath, sizeof(oldpath));
        get_param(body, "newname", newname, sizeof(newname));
        snprintf(pending_method, sizeof(pending_method), "RENAME");
        strncpy(pending_path, oldpath, sizeof(pending_path) - 1);
        strncpy(pending_body, newname, sizeof(pending_body) - 1);
        pending_client  = client;
        confirm_pending = 1;
        snprintf(last_action, sizeof(last_action), "Rename: %s -> %s", oldpath, newname);
        show_confirm(last_action);
        return;

    } else if (strcmp(urlpath, "/mkdir") == 0 && strcmp(method, "POST") == 0) {
        char base[MAX_PATH], name[MAX_PATH];
        get_param(body, "path", base, sizeof(base));
        get_param(body, "name", name, sizeof(name));
        char newdir[MAX_PATH];
        if (strcmp(base, "/") == 0)
            snprintf(newdir, sizeof(newdir), "/%s", name);
        else
            snprintf(newdir, sizeof(newdir), "%s/%s", base, name);
        snprintf(pending_method, sizeof(pending_method), "MKDIR");
        strncpy(pending_path, newdir, sizeof(pending_path) - 1);
        strncpy(pending_body, base,   sizeof(pending_body) - 1);
        pending_client  = client;
        confirm_pending = 1;
        snprintf(last_action, sizeof(last_action), "Create folder: %s", newdir);
        show_confirm(last_action);
        return;

    } else if (strcmp(urlpath, "/newfile") == 0 && strcmp(method, "POST") == 0) {
        char base[MAX_PATH], name[MAX_PATH];
        get_param(body, "path", base, sizeof(base));
        get_param(body, "name", name, sizeof(name));
        char newf[MAX_PATH];
        if (strcmp(base, "/") == 0)
            snprintf(newf, sizeof(newf), "/%s", name);
        else
            snprintf(newf, sizeof(newf), "%s/%s", base, name);
        snprintf(pending_method, sizeof(pending_method), "NEWFILE");
        strncpy(pending_path, newf,  sizeof(pending_path) - 1);
        strncpy(pending_body, base,  sizeof(pending_body) - 1);
        pending_client  = client;
        confirm_pending = 1;
        snprintf(last_action, sizeof(last_action), "Create file: %s", newf);
        show_confirm(last_action);
        return;

    } else if (strcmp(urlpath, "/edit") == 0) {
        serve_editor(client, path_param);

    } else if (strcmp(urlpath, "/save") == 0 && strcmp(method, "POST") == 0) {
        char fpath[MAX_PATH], raw_content[BUFFER_SIZE];
        get_param(body, "path",    fpath,       sizeof(fpath));
        get_param(body, "content", raw_content, sizeof(raw_content));

        size_t clen = strlen(raw_content);

        // Stage in RAM until confirmed
        if (edit_data) { free(edit_data); edit_data = NULL; }
        edit_data = (char *)malloc(clen + 1);
        if (edit_data) {
            memcpy(edit_data, raw_content, clen);
            edit_data[clen] = 0;
            edit_data_len   = clen;

            snprintf(pending_method, sizeof(pending_method), "SAVE");
            strncpy(pending_path, fpath, sizeof(pending_path) - 1);
            pending_client  = client;
            confirm_pending = 1;
            const char *fname = strrchr(fpath, '/');
            fname = fname ? fname + 1 : fpath;
            snprintf(last_action, sizeof(last_action),
                     "Save: %s (%zu bytes)", fname, clen);
            show_confirm(last_action);
            return; // keep client open
        }
        // Fallback
        send_redirect(client, "/");

    } else if (strcmp(urlpath, "/upload") == 0 && strcmp(method, "POST") == 0) {
        // Find multipart boundary
        char boundary[128] = {0};
        char *bpos = strstr(content_type, "boundary=");
        if (bpos) strncpy(boundary, bpos + 9, sizeof(boundary) - 1);

        // Extract filename from Content-Disposition
        char filename[MAX_PATH] = "upload.bin";
        char *fnpos = strstr(body, "filename=\"");
        if (fnpos) {
            fnpos += 10;
            char *fnend = strchr(fnpos, '"');
            if (fnend) {
                size_t fnlen = fnend - fnpos;
                if (fnlen >= sizeof(filename)) fnlen = sizeof(filename) - 1;
                memcpy(filename, fnpos, fnlen);
                filename[fnlen] = 0;
            }
        }

        // Locate raw file data (after double CRLF) — do NOT write yet
        char *datastart = strstr(body, "\r\n\r\n");
        if (datastart && filename[0]) {
            datastart += 4;
            char *dataend = body + content_length;
            char boundarytail[140];
            snprintf(boundarytail, sizeof(boundarytail), "\r\n--%s--", boundary);
            char *bend = strstr(datastart, boundarytail);
            if (bend) dataend = bend;
            size_t dlen = (size_t)(dataend - datastart);

            // Stage data in RAM
            if (upload_data) { free(upload_data); upload_data = NULL; }
            upload_data = (char *)malloc(dlen);
            if (upload_data) {
                memcpy(upload_data, datastart, dlen);
                upload_data_len = dlen;

                if (path_param[0] && strcmp(path_param, "/") != 0)
                    snprintf(upload_savepath, sizeof(upload_savepath),
                             "sdmc:%s/%s", path_param, filename);
                else
                    snprintf(upload_savepath, sizeof(upload_savepath),
                             "sdmc:/%s", filename);

                snprintf(upload_redir, sizeof(upload_redir),
                         "/browse?path=%s", path_param);

                // Request confirmation on console
                snprintf(pending_method, sizeof(pending_method), "UPLOAD");
                pending_client  = client;
                confirm_pending = 1;
                snprintf(last_action, sizeof(last_action),
                         "Upload: %s (%zu bytes) to %s",
                         filename, dlen, path_param[0] ? path_param : "/");
                show_confirm(last_action);
                return; // keep client open
            }
        }
        // Fallback on allocation failure
        char redir[MAX_PATH];
        snprintf(redir, sizeof(redir), "/browse?path=%s", path_param);
        send_redirect(client, redir);

    } else {
        send_response(client, 404, "text/plain", "Not found", 9);
    }

    close(client);
}

// ---- Execute or cancel the pending action ----
static void execute_pending(int accepted) {
    char redir[MAX_PATH] = "/";

    if (accepted) {
        if (strcmp(pending_method, "DELETE") == 0) {
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "sdmc:%s", pending_path);
            if (remove(full) != 0) rmdir(full);
            char parent[MAX_PATH];
            strncpy(parent, pending_path, sizeof(parent));
            char *sl = strrchr(parent, '/');
            if (sl && sl != parent) *sl = 0; else strcpy(parent, "/");
            snprintf(redir, sizeof(redir), "/browse?path=%s", parent);

        } else if (strcmp(pending_method, "RENAME") == 0) {
            char parent[MAX_PATH];
            strncpy(parent, pending_path, sizeof(parent));
            char *sl = strrchr(parent, '/');
            char newpath[MAX_PATH];
            if (sl) {
                *sl = 0;
                snprintf(newpath, sizeof(newpath), "sdmc:%s/%s", parent, pending_body);
            } else {
                snprintf(newpath, sizeof(newpath), "sdmc:/%s", pending_body);
                strcpy(parent, "/");
            }
            char oldpath[MAX_PATH];
            snprintf(oldpath, sizeof(oldpath), "sdmc:%s", pending_path);
            rename(oldpath, newpath);
            snprintf(redir, sizeof(redir), "/browse?path=%s", parent);

        } else if (strcmp(pending_method, "MKDIR") == 0) {
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "sdmc:%s", pending_path);
            mkdir(full, 0777);
            snprintf(redir, sizeof(redir), "/browse?path=%s", pending_body);

        } else if (strcmp(pending_method, "NEWFILE") == 0) {
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "sdmc:%s", pending_path);
            FILE *f = fopen(full, "w");
            if (f) fclose(f);
            snprintf(redir, sizeof(redir), "/browse?path=%s", pending_body);

        } else if (strcmp(pending_method, "SAVE") == 0) {
            if (edit_data) {
                char full[MAX_PATH];
                snprintf(full, sizeof(full), "sdmc:%s", pending_path);
                FILE *f = fopen(full, "wb");
                if (f) {
                    fwrite(edit_data, 1, edit_data_len, f);
                    fclose(f);
                }
                free(edit_data);
                edit_data     = NULL;
                edit_data_len = 0;
            }
            snprintf(redir, sizeof(redir), "/edit?path=%s", pending_path);

        } else if (strcmp(pending_method, "UPLOAD") == 0) {
            if (upload_data && upload_data_len > 0) {
                FILE *f = fopen(upload_savepath, "wb");
                if (f) {
                    fwrite(upload_data, 1, upload_data_len, f);
                    fclose(f);
                }
                free(upload_data);
                upload_data     = NULL;
                upload_data_len = 0;
            }
            strncpy(redir, upload_redir, sizeof(redir) - 1);
        }

        send_redirect(pending_client, redir);
    } else {
        // Cancelled — discard staging buffers and redirect back
        if (strcmp(pending_method, "UPLOAD") == 0 && upload_data) {
            free(upload_data);
            upload_data     = NULL;
            upload_data_len = 0;
            send_redirect(pending_client, upload_redir);
        } else if (strcmp(pending_method, "SAVE") == 0) {
            if (edit_data) {
                free(edit_data);
                edit_data     = NULL;
                edit_data_len = 0;
            }
            char eredir[MAX_PATH];
            snprintf(eredir, sizeof(eredir), "/edit?path=%s", pending_path);
            send_redirect(pending_client, eredir);
        } else {
            send_redirect(pending_client, "/");
        }
        snprintf(last_action, sizeof(last_action), "Cancelled");
    }

    close(pending_client);
    pending_client  = -1;
    confirm_pending = 0;
    pending_method[0] = 0;
}

// ---- Entry point ----
int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    // On CFW, the ac:u service may hold the SOC service.
    // Initialize and immediately exit ac:u to release it before socInit.
    acInit();
    acExit();

    // Init network — larger buffer + retry
    u32 *soc_buf = NULL;
    Result rc = -1;
    u32 buf_sizes[] = {0x100000, 0x80000, 0x200000};
    for (int i = 0; i < 3 && R_FAILED(rc); i++) {
        if (soc_buf) { linearFree(soc_buf); soc_buf = NULL; }
        soc_buf = (u32 *)linearAlloc(buf_sizes[i]);
        if (!soc_buf) continue;
        rc = socInit(soc_buf, buf_sizes[i]);
        if (R_FAILED(rc))
            printf("socInit try %d failed: 0x%08lX\n", i+1, rc);
    }
    if (R_FAILED(rc)) {
        printf("socInit failed: 0x%08lX\n\n", rc);
        printf("Make sure:\n");
        printf("- WiFi is ON\n");
        printf("- No other network app\n");
        printf("  is running\n\n");
        printf("[START] to exit\n");
        gfxFlushBuffers(); gfxSwapBuffers();
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
        }
        if (soc_buf) linearFree(soc_buf);
        gfxExit();
        return 0;
    }

    u32 ip = gethostid();

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("Socket error!\n");
        goto cleanup;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Bind error!\n");
        goto cleanup;
    }
    if (listen(server_fd, MAX_PENDING) < 0) {
        printf("Listen error!\n");
        goto cleanup;
    }

    // Non-blocking accept
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    show_main_screen(ip);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_START) break;

        if (confirm_pending) {
            if (kdown & KEY_A) {
                execute_pending(1);
                show_main_screen(ip);
            } else if (kdown & KEY_B) {
                execute_pending(0);
                show_main_screen(ip);
            }
        } else {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int client = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
            if (client >= 0) {
                snprintf(client_ip, sizeof(client_ip), "%s",
                         inet_ntoa(cli_addr.sin_addr));
                handle_client(client);
                if (!confirm_pending)
                    show_main_screen(ip);
            }
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

cleanup:
    if (server_fd >= 0) close(server_fd);
    socExit();
    linearFree(soc_buf);
    gfxExit();
    return 0;
}
