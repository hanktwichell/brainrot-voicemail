/*
 * Brainrot Voice Memo — native macOS app.
 * Native shell: NSWindow + WKWebView (via the Objective-C runtime).
 * Backend: in-process HTTP server on 127.0.0.1 that reads iMessage voice
 * messages from chat.db (sqlite3), converts them with the built-in afconvert,
 * and finds gameplay videos with the built-in curl.
 * Only uses libraries and tools that ship with every Mac.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ libc */
typedef long ssize_t; typedef int pid_t; typedef uint32_t socklen_t; typedef int64_t off_t;
typedef struct FILE FILE;
void *malloc(size_t); void *realloc(void *, size_t); void free(void *); void *calloc(size_t, size_t);
void *memcpy(void *, const void *, size_t); void *memset(void *, int, size_t);
size_t strlen(const char *); int strcmp(const char *, const char *); int strncmp(const char *, const char *, size_t);
char *strstr(const char *, const char *); char *strchr(const char *, int); char *strrchr(const char *, int);
char *strdup(const char *);
int snprintf(char *, size_t, const char *, ...);
char *getenv(const char *);
FILE *fopen(const char *, const char *); size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *); int fclose(FILE *);
int fseeko(FILE *, off_t, int); off_t ftello(FILE *);
int access(const char *, int); int mkdir(const char *, unsigned short); int rename(const char *, const char *); int unlink(const char *);
int socket(int, int, int); int setsockopt(int, int, int, const void *, socklen_t);
int bind(int, const void *, socklen_t); int listen(int, int); int accept(int, void *, socklen_t *);
ssize_t read(int, void *, size_t); ssize_t write(int, const void *, size_t); int close(int);
int pipe(int[2]); pid_t waitpid(pid_t, int *, int);
int posix_spawn(pid_t *, const char *, void **, const void *, char *const[], char *const[]);
int posix_spawn_file_actions_init(void **); int posix_spawn_file_actions_destroy(void **);
int posix_spawn_file_actions_adddup2(void **, int, int); int posix_spawn_file_actions_addclose(void **, int);
int posix_spawn_file_actions_addopen(void **, int, const char *, int, unsigned short);
char ***_NSGetEnviron(void); int _NSGetExecutablePath(char *, uint32_t *);
int *__error(void);
void (*signal(int, void (*)(int)))(int);
int pthread_create(void **, const void *, void *(*)(void *), void *); int pthread_detach(void *);
void os_unfair_lock_lock(uint32_t *); void os_unfair_lock_unlock(uint32_t *);
void exit(int); uint32_t arc4random(void); int chmod(const char *, unsigned short); int fcntl(int, int, ...); int connect(int, const void *, socklen_t); int usleep(unsigned int); int kill(pid_t, int); long time(long *);

#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 0x0004
#define SO_NOSIGPIPE 0x1022
#define O_WRONLY 1
#define R_OK 4
#define X_OK 1
#define F_SETFD 2
#define FD_CLOEXEC 1
static void cloexec(int fd) { if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC); }
#define EINTR 4
#define SIGPIPE 13
#define SIG_IGN ((void (*)(int))1)

#ifdef BV_LINUX_TEST /* host-side test build only; the shipped app is the #else branch */
#undef SOL_SOCKET
#define SOL_SOCKET 1
#define SO_RCVTIMEO_ 20
struct sockaddr_in { uint16_t sin_family; uint16_t sin_port; uint32_t sin_addr; char zero[8]; };
#define SIN_LEN(a)
#else
struct sockaddr_in { uint8_t sin_len, sin_family; uint16_t sin_port; uint32_t sin_addr; char zero[8]; };
#define SIN_LEN(a) (a).sin_len = sizeof (a);
#define SO_RCVTIMEO_ 0x1006
#endif
struct timeval_ { long tv_sec; int tv_usec; };

/* --------------------------------------------------------------- sqlite3 */
typedef struct sqlite3 sqlite3; typedef struct sqlite3_stmt sqlite3_stmt;
int sqlite3_open_v2(const char *, sqlite3 **, int, const char *); int sqlite3_close(sqlite3 *);
int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
int sqlite3_step(sqlite3_stmt *); int sqlite3_finalize(sqlite3_stmt *);
int64_t sqlite3_column_int64(sqlite3_stmt *, int); const unsigned char *sqlite3_column_text(sqlite3_stmt *, int);
const char *sqlite3_errmsg(sqlite3 *);
#define SQLITE_ROW 100
#define SQLITE_OPEN_READWRITE 2

/* ------------------------------------------------------------- ObjC */
typedef void *id; typedef void *SEL; typedef void *Class;
id objc_getClass(const char *); SEL sel_registerName(const char *); void objc_msgSend(void);
Class objc_allocateClassPair(Class, const char *, size_t); void objc_registerClassPair(Class);
bool class_addMethod(Class, SEL, void (*)(void), const char *);
typedef struct { double x, y, w, h; } CGRect;
typedef struct { double w, h; } CGSize;

#define C(n) objc_getClass(n)
#define S(n) sel_registerName(n)
static id m0(id o, const char *s) { return ((id (*)(id, SEL))objc_msgSend)(o, S(s)); }
static id m1(id o, const char *s, id a) { return ((id (*)(id, SEL, id))objc_msgSend)(o, S(s), a); }
static void mI(id o, const char *s, long v) { ((void (*)(id, SEL, long))objc_msgSend)(o, S(s), v); }
static void mB(id o, const char *s, bool v) { ((void (*)(id, SEL, bool))objc_msgSend)(o, S(s), v); }
static id NS(const char *s) { return ((id (*)(id, SEL, const char *))objc_msgSend)(C("NSString"), S("stringWithUTF8String:"), s); }
static id NEW(const char *cls) { return m0(m0(C(cls), "alloc"), "init"); }

/* ------------------------------------------------------- string buffer */
typedef struct { char *p; size_t n, cap; } Buf;
static void bput(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2 + 256; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void bs(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void bf(Buf *b, const char *fmt, long long v) { char t[64]; snprintf(t, sizeof t, fmt, v); bs(b, t); }
static void bjson(Buf *b, const char *s) {
    bs(b, "\"");
    for (const unsigned char *c = (const unsigned char *)(s ? s : ""); *c; c++) {
        if (*c == '"' || *c == '\\') { char t[2] = {'\\', (char)*c}; bput(b, t, 2); }
        else if (*c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", *c); bs(b, t); }
        else bput(b, (const char *)c, 1);
    }
    bs(b, "\"");
}
static char *cat3(const char *a, const char *b, const char *c) {
    Buf r = {0}; bs(&r, a); bs(&r, b); bs(&r, c ? c : ""); return r.p;
}

/* -------------------------------------------------------------- paths */
static const char *HOME;
static char *CACHE;      /* ~/Library/Caches/BrainrotVoicemail */
static char BUNDLE[2048];
static char *SUPPORT;    /* ~/Library/Application Support/BrainrotVoicemail */

static void mkdirs(void) {
    char *lib = cat3(HOME, "/Library/Caches", ""); mkdir(lib, 0755); free(lib);
    mkdir(CACHE, 0755);
    char *a = cat3(CACHE, "/audio", ""); mkdir(a, 0755); free(a);
    char *tr = cat3(CACHE, "/transcripts", ""); mkdir(tr, 0755); free(tr);
    char *as = cat3(HOME, "/Library/Application Support", ""); mkdir(as, 0755); free(as);
    mkdir(SUPPORT, 0755);
    char *g = cat3(SUPPORT, "/gameplay", ""); mkdir(g, 0755); free(g);
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb"); if (!in) return -1;
    FILE *out = fopen(dst, "wb"); if (!out) { fclose(in); return -1; }
    char *buf = malloc(1 << 16); size_t k;
    while ((k = fread(buf, 1, 1 << 16, in)) > 0) fwrite(buf, 1, k, out);
    free(buf); fclose(in); fclose(out); return 0;
}

/* copy db + -wal + -shm so we never lock or modify the live database */
static char *snapshot(const char *src, const char *name) {
    char *dst = cat3(CACHE, "/", name);
    const char *suf[] = {"-wal", "-shm"};
    for (int i = 0; i < 2; i++) { char *d = cat3(dst, suf[i], ""); unlink(d); free(d); }
    if (copy_file(src, dst) != 0) { free(dst); return NULL; }
    for (int i = 0; i < 2; i++) {
        char *s = cat3(src, suf[i], ""), *d = cat3(dst, suf[i], "");
        if (access(s, R_OK) == 0) copy_file(s, d);
        free(s); free(d);
    }
    return dst;
}

/* ------------------------------------------------------ run a process */
static int run(char *const argv[], Buf *out) {
    int fd[2] = {-1, -1}; void *fa = 0; pid_t pid; int st = -1;
    if (out && pipe(fd) != 0) return -1;
    if (out) { cloexec(fd[0]); cloexec(fd[1]); }
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", 0, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    if (out) { posix_spawn_file_actions_adddup2(&fa, fd[1], 1); posix_spawn_file_actions_addclose(&fa, fd[0]); }
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    int rc = posix_spawn(&pid, argv[0], &fa, 0, argv, *_NSGetEnviron());
    posix_spawn_file_actions_destroy(&fa);
    if (out) {
        close(fd[1]);
        if (rc == 0) { char t[16384]; ssize_t k; while ((k = read(fd[0], t, sizeof t)) > 0) bput(out, t, (size_t)k); }
        close(fd[0]);
    }
    if (rc != 0) return -1;
    while (waitpid(pid, &st, 0) < 0 && *__error() == EINTR) {}
    return (st & 0x7f) == 0 ? (st >> 8) & 0xff : -1;
}

/* run a process, calling cb for each line of combined stdout/stderr.
   Watchdog: kill it if it prints nothing for `idle_limit` seconds or runs longer than `total_limit`. */
typedef struct { pid_t pid; volatile long last; volatile int done; long start; int idle_limit, total_limit; volatile int killed; } Watch;
static void *watchdog(void *arg) {
    Watch *w = arg;
    while (!w->done) {
        usleep(1000000);
        long now = time(0);
        if (!w->done && (now - w->last > w->idle_limit || now - w->start > w->total_limit)) { w->killed = 1; kill(w->pid, 9); break; }
    }
    return 0;
}
static int run_lines(char *const argv[], void (*cb)(const char *, void *), void *ctx, int idle_limit, int total_limit, int *timed_out) {
    int fd[2]; void *fa = 0; pid_t pid; int st = -1;
    if (timed_out) *timed_out = 0;
    if (pipe(fd) != 0) return -1;
    cloexec(fd[0]); cloexec(fd[1]);
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", 0, 0);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 2);
    int rc = posix_spawn(&pid, argv[0], &fa, 0, argv, *_NSGetEnviron());
    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    Watch *w = calloc(1, sizeof *w);   /* freed by nobody: tiny, and the thread may outlive us briefly */
    if (rc == 0) {
        w->pid = pid; w->start = w->last = time(0); w->idle_limit = idle_limit; w->total_limit = total_limit;
        void *th; pthread_create(&th, 0, watchdog, w); pthread_detach(th);
        char line[4096]; size_t n = 0; char t[4096]; ssize_t k;
        while ((k = read(fd[0], t, sizeof t)) > 0) {
            w->last = time(0);
            for (ssize_t i = 0; i < k; i++) {
                if (t[i] == '\n' || t[i] == '\r') { line[n] = 0; if (n) cb(line, ctx); n = 0; }
                else if (n < sizeof line - 1) line[n++] = t[i];
            }
        }
        if (n) { line[n] = 0; cb(line, ctx); }
    }
    close(fd[0]);
    if (rc != 0) return -1;
    while (waitpid(pid, &st, 0) < 0 && *__error() == EINTR) {}
    w->done = 1;
    if (timed_out) *timed_out = w->killed;
    return (st & 0x7f) == 0 ? (st >> 8) & 0xff : -1;
}

static long long file_size(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseeko(f, 0, 2); long long n = ftello(f); fclose(f); return n;
}

/* ------------------------------------------------------------ contacts */
typedef struct { char *key, *name; } Contact;
static Contact *contacts; static size_t ncontacts, capcontacts;

static void digits10(const char *s, char *out) {
    char d[64]; int n = 0;
    for (; s && *s && n < 63; s++) if (*s >= '0' && *s <= '9') d[n++] = *s;
    d[n] = 0;
    const char *t = n > 10 ? d + n - 10 : d;
    memcpy(out, t, strlen(t) + 1);
}
static void lower(const char *s, char *out, size_t cap) {
    size_t i = 0; for (; s[i] && i + 1 < cap; i++) out[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    out[i] = 0;
}
static void add_contact(const char *key, const char *name) {
    if (!key[0] || !name || !name[0]) return;
    if (ncontacts == capcontacts) { capcontacts = capcontacts * 2 + 256; contacts = realloc(contacts, capcontacts * sizeof *contacts); }
    contacts[ncontacts].key = strdup(key); contacts[ncontacts].name = strdup(name); ncontacts++;
}
static const char *lookup(const char *handle) {
    if (!handle || !handle[0]) return NULL;
    char key[300];
    if (strchr(handle, '@')) { key[0] = 'e'; key[1] = ':'; lower(handle, key + 2, sizeof key - 2); }
    else { key[0] = 'p'; key[1] = ':'; digits10(handle, key + 2); if (!key[2]) return handle; }
    for (size_t i = 0; i < ncontacts; i++) if (!strcmp(contacts[i].key, key)) return contacts[i].name;
    return handle;
}

static void load_contacts_db(const char *path, int idx) {
    char nm[64]; snprintf(nm, sizeof nm, "contacts%d.db", idx);
    char *copy = snapshot(path, nm); if (!copy) return;
    sqlite3 *db = 0;
    if (sqlite3_open_v2(copy, &db, SQLITE_OPEN_READWRITE, 0) == 0) {
        const char *q[2] = {
            "SELECT 'p', x.ZFULLNUMBER, r.ZFIRSTNAME, r.ZLASTNAME, r.ZORGANIZATION FROM ZABCDPHONENUMBER x JOIN ZABCDRECORD r ON r.Z_PK = x.ZOWNER",
            "SELECT 'e', x.ZADDRESS, r.ZFIRSTNAME, r.ZLASTNAME, r.ZORGANIZATION FROM ZABCDEMAILADDRESS x JOIN ZABCDRECORD r ON r.Z_PK = x.ZOWNER"};
        for (int k = 0; k < 2; k++) {
            sqlite3_stmt *st = 0;
            if (sqlite3_prepare_v2(db, q[k], -1, &st, 0) != 0) continue;
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *val = (const char *)sqlite3_column_text(st, 1);
                const char *f = (const char *)sqlite3_column_text(st, 2), *l = (const char *)sqlite3_column_text(st, 3);
                const char *o = (const char *)sqlite3_column_text(st, 4);
                if (!val) continue;
                Buf name = {0};
                if (f && f[0]) bs(&name, f);
                if (l && l[0]) { if (name.n) bs(&name, " "); bs(&name, l); }
                if (!name.n && o) bs(&name, o);
                char key[300];
                if (k == 0) { key[0] = 'p'; key[1] = ':'; digits10(val, key + 2); }
                else { key[0] = 'e'; key[1] = ':'; lower(val, key + 2, sizeof key - 2); }
                if (name.n && key[2]) add_contact(key, name.p);
                free(name.p);
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(db);
    }
    char *s1 = cat3(copy, "-wal", ""), *s2 = cat3(copy, "-shm", "");
    unlink(copy); unlink(s1); unlink(s2); free(s1); free(s2); free(copy);
}

static void load_contacts(void) {
    for (size_t i = 0; i < ncontacts; i++) { free(contacts[i].key); free(contacts[i].name); }
    ncontacts = 0;
    char *base = cat3(HOME, "/Library/Application Support/AddressBook", "");
    char *root = cat3(base, "/AddressBook-v22.abcddb", "");
    char *srcdir = cat3(base, "/Sources", "");
    Buf ls = {0};
    char *argv[] = {"/bin/ls", "-1", srcdir, 0};
    run(argv, &ls);
    int idx = 0;
    if (ls.p) {
        char *line = ls.p;
        while (line && *line) {
            char *nl = strchr(line, '\n'); if (nl) *nl = 0;
            if (line[0]) {
                Buf p = {0}; bs(&p, srcdir); bs(&p, "/"); bs(&p, line); bs(&p, "/AddressBook-v22.abcddb");
                if (access(p.p, R_OK) == 0) load_contacts_db(p.p, idx++);
                free(p.p);
            }
            line = nl ? nl + 1 : 0;
        }
    }
    if (access(root, R_OK) == 0) load_contacts_db(root, idx++);
    free(ls.p); free(base); free(root); free(srcdir);
}

/* ------------------------------------------------------- voice messages */
static uint32_t LOCK;
static char *MSG_JSON;                 /* cached response */
static int64_t *IDS; static char **PATHS; static size_t NPATHS;

static const char *QUERY_FMT =
    "SELECT a.ROWID, a.filename, m.date, m.is_from_me, h.id, c.display_name, c.chat_identifier,"
    " (SELECT COUNT(*) FROM chat_handle_join chj WHERE chj.chat_id = c.ROWID)"
    " FROM attachment a"
    " JOIN message_attachment_join maj ON maj.attachment_id = a.ROWID"
    " JOIN message m ON m.ROWID = maj.message_id"
    " LEFT JOIN handle h ON h.ROWID = m.handle_id"
    " LEFT JOIN chat_message_join cmj ON cmj.message_id = m.ROWID"
    " LEFT JOIN chat c ON c.ROWID = cmj.chat_id"
    " WHERE %s a.uti = 'com.apple.coreaudio-format' OR a.mime_type = 'audio/x-caf'"
    " OR lower(a.transfer_name) LIKE '%%.caf' OR lower(a.filename) LIKE '%%.caf'"
    " GROUP BY a.ROWID ORDER BY m.date DESC";

/* returns http status; *out gets JSON body */
static int build_messages(char **out) {
    char *src = cat3(HOME, "/Library/Messages/chat.db", "");
    FILE *t = fopen(src, "rb");
    if (!t) {
        int e = *__error(); free(src);
        if (e == 2) { *out = strdup("{\"error\":\"No Messages database found. Sign into iMessage in the Messages app first.\"}"); return 500; }
        *out = strdup("{\"error\":\"fda\"}"); return 403;
    }
    fclose(t);
    char *copy = snapshot(src, "chat.db"); free(src);
    if (!copy) { *out = strdup("{\"error\":\"fda\"}"); return 403; }

    load_contacts();

    sqlite3 *db = 0; sqlite3_stmt *st = 0;
    if (sqlite3_open_v2(copy, &db, SQLITE_OPEN_READWRITE, 0) != 0) {
        Buf e = {0}; bs(&e, "{\"error\":"); bjson(&e, sqlite3_errmsg(db)); bs(&e, "}"); *out = e.p; free(copy); return 500;
    }
    char q[2048];
    snprintf(q, sizeof q, QUERY_FMT, "m.is_audio_message = 1 OR");
    if (sqlite3_prepare_v2(db, q, -1, &st, 0) != 0) {
        snprintf(q, sizeof q, QUERY_FMT, "");
        if (sqlite3_prepare_v2(db, q, -1, &st, 0) != 0) {
            Buf e = {0}; bs(&e, "{\"error\":"); bjson(&e, sqlite3_errmsg(db)); bs(&e, "}"); *out = e.p;
            sqlite3_close(db); free(copy); return 500;
        }
    }
    Buf j = {0}; bs(&j, "[");
    int64_t *ids = 0; char **paths = 0; size_t n = 0, cap = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t rid = sqlite3_column_int64(st, 0);
        const char *fn = (const char *)sqlite3_column_text(st, 1);
        int64_t date = sqlite3_column_int64(st, 2);
        int me = (int)sqlite3_column_int64(st, 3);
        const char *handle = (const char *)sqlite3_column_text(st, 4);
        const char *dname = (const char *)sqlite3_column_text(st, 5);
        const char *cid = (const char *)sqlite3_column_text(st, 6);
        int64_t members = sqlite3_column_int64(st, 7);

        char *path = fn ? (fn[0] == '~' ? cat3(HOME, fn + 1, "") : strdup(fn)) : strdup("");
        if (n == cap) { cap = cap * 2 + 64; ids = realloc(ids, cap * sizeof *ids); paths = realloc(paths, cap * sizeof *paths); }
        ids[n] = rid; paths[n] = path; n++;

        int64_t secs = date > 1000000000000LL ? date / 1000000000LL : date;
        int64_t unix_ts = date ? secs + 978307200LL : 0;
        int group = members > 1;
        const char *sender = me ? "Me" : (handle ? lookup(handle) : "Unknown");
        const char *chat = group ? (dname && dname[0] ? dname : "Group chat")
                                 : (handle ? lookup(handle) : (cid ? lookup(cid) : ""));
        if (j.n > 1) bs(&j, ",");
        bf(&j, "{\"id\":\"%lld\"", rid);
        bs(&j, ",\"sender\":"); bjson(&j, sender);
        bs(&j, ",\"chat\":"); bjson(&j, chat);
        bs(&j, group ? ",\"group\":true" : ",\"group\":false");
        bs(&j, me ? ",\"from_me\":true" : ",\"from_me\":false");
        bf(&j, ",\"ts\":%lld", unix_ts);
        bs(&j, path[0] && access(path, R_OK) == 0 ? ",\"available\":true}" : ",\"available\":false}");
    }
    bs(&j, "]");
    sqlite3_finalize(st); sqlite3_close(db);
    { char *s1 = cat3(copy, "-wal", ""), *s2 = cat3(copy, "-shm", ""); unlink(copy); unlink(s1); unlink(s2); free(s1); free(s2); }
    free(copy);

    os_unfair_lock_lock(&LOCK);
    for (size_t i = 0; i < NPATHS; i++) free(PATHS[i]);
    free(IDS); free(PATHS);
    IDS = ids; PATHS = paths; NPATHS = n;
    os_unfair_lock_unlock(&LOCK);
    *out = j.p;
    return 200;
}

static char *path_for(int64_t id) {
    char *r = 0;
    os_unfair_lock_lock(&LOCK);
    for (size_t i = 0; i < NPATHS; i++) if (IDS[i] == id) { r = strdup(PATHS[i]); break; }
    os_unfair_lock_unlock(&LOCK);
    return r;
}

/* .caf (Opus) -> .m4a (AAC) so WebKit can play it; cached */
static char *converted(int64_t id, const char *src) {
    char name[96]; snprintf(name, sizeof name, "/audio/%lld.m4a", (long long)id);
    char *dst = cat3(CACHE, name, "");
    if (access(dst, R_OK) == 0) return dst;
    char tmp[2200]; snprintf(tmp, sizeof tmp, "%s.%u.m4a", dst, arc4random());
    char *argv[] = {"/usr/bin/afconvert", "-f", "m4af", "-d", "aac", (char *)src, tmp, 0};
    if (run(argv, 0) == 0 && access(tmp, R_OK) == 0) { rename(tmp, dst); return dst; }
    unlink(tmp); free(dst); return 0;
}

/* ------------------------------------------------------- transcription */
/* Word-level timestamps from Apple's Speech framework (on-device when available).
   Result JSON: {"words":[{"w":"hey","s":0.42,"d":0.21},...]}  or {"error":"..."} */
static void transcript_cache_path(int64_t id, char *out, size_t cap) {
    snprintf(out, cap, "%s/transcripts/%lld.json", CACHE, (long long)id);
}
static char *read_text(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    Buf b = {0}; char t[8192]; size_t k;
    while ((k = fread(t, 1, sizeof t, f)) > 0) bput(&b, t, k);
    fclose(f); return b.p ? b.p : strdup("");
}
static void write_text(const char *p, const char *s) {
    FILE *f = fopen(p, "wb"); if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
}

#ifdef BV_LINUX_TEST
/* test build: fake evenly spaced words so the caption renderer can be exercised */
static char *transcribe(int64_t id, const char *audio_path) {
    (void)audio_path;
    const char *words[] = {"yo", "bro", "you", "will", "not", "believe", "what", "happened", "at", "the", "absolutely", "ridiculous", "party"};
    Buf j = {0}; bs(&j, "{\"words\":[");
    for (int i = 0; i < 13; i++) {
        char t[160]; snprintf(t, sizeof t, "%s{\"w\":\"%s\",\"s\":%.3f,\"d\":%.3f}", i ? "," : "", words[i], 0.1 + i * 0.2 + (id % 2) * 0.0, 0.18);
        bs(&j, t);
    }
    bs(&j, "]}"); return j.p;
}
#else
void *objc_autoreleasePoolPush(void); void objc_autoreleasePoolPop(void *);
void *dispatch_semaphore_create(long); long dispatch_semaphore_wait(void *, uint64_t); long dispatch_semaphore_signal(void *);
uint64_t dispatch_time(uint64_t, int64_t);
extern void *_NSConcreteGlobalBlock[32];

/* Hand-built Objective-C blocks. Marked global so the frameworks never try to copy/free them;
   each carries a context pointer after the standard header. */
struct BlockDesc { unsigned long reserved, size; const char *signature; };
typedef struct Blk { void *isa; int flags, reserved; void *invoke; struct BlockDesc *desc; void *ctx; } Blk;
static struct BlockDesc BDESC = {0, sizeof(Blk), "v24@?0@8@16"};      /* (id, id) handlers */
static struct BlockDesc BDESC_AUTH = {0, sizeof(Blk), "v16@?0q8"};      /* (NSInteger) handler */
static Blk *mkblock(void *fn, void *ctx) {
    Blk *b = calloc(1, sizeof *b);
    b->isa = _NSConcreteGlobalBlock; b->flags = (1 << 28) | (1 << 30); b->invoke = fn; b->desc = &BDESC; b->ctx = ctx;
    return b;
}
static const char *utf8(id nsstring) { return nsstring ? ((const char *(*)(id, SEL))objc_msgSend)(nsstring, S("UTF8String")) : 0; }
static bool responds(id o, const char *sel) { return ((signed char (*)(id, SEL, SEL))objc_msgSend)(o, S("respondsToSelector:"), S(sel)) != 0; }

typedef struct { void *sema; long status; } AuthCtx;
static void on_auth(Blk *b, long status) { AuthCtx *c = b->ctx; c->status = status; dispatch_semaphore_signal(c->sema); }

static long speech_auth(void) {
    id cls = C("SFSpeechRecognizer"); if (!cls) return -1;
    long st = ((long (*)(id, SEL))objc_msgSend)(cls, S("authorizationStatus"));
    if (st == 0) {
        AuthCtx *c = calloc(1, sizeof *c); c->sema = dispatch_semaphore_create(0); c->status = 0;
        Blk *ab = mkblock((void *)on_auth, c); ab->desc = &BDESC_AUTH;
        ((void (*)(id, SEL, void *))objc_msgSend)(cls, S("requestAuthorization:"), ab);
        dispatch_semaphore_wait(c->sema, dispatch_time(0, 120LL * 1000000000LL));
        st = c->status;   /* ctx intentionally not freed: the callback may still arrive after a timeout */
    }
    return st;
}

typedef struct { void *sema; uint32_t lock; int done; char *json; } TCtx;
static void on_result(Blk *b, id result, id error) {
    TCtx *c = b->ctx;
    os_unfair_lock_lock(&c->lock);
    if (c->done) { os_unfair_lock_unlock(&c->lock); return; }
    Buf j = {0};
    if (result && ((signed char (*)(id, SEL))objc_msgSend)(result, S("isFinal"))) {
        id segs = m0(m0(result, "bestTranscription"), "segments");
        unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(segs, S("count"));
        bs(&j, "{\"words\":[");
        for (unsigned long i = 0; i < n; i++) {
            id seg = ((id (*)(id, SEL, unsigned long))objc_msgSend)(segs, S("objectAtIndex:"), i);
            double ts = ((double (*)(id, SEL))objc_msgSend)(seg, S("timestamp"));
            double du = ((double (*)(id, SEL))objc_msgSend)(seg, S("duration"));
            if (i) bs(&j, ",");
            bs(&j, "{\"w\":"); bjson(&j, utf8(m0(seg, "substring")));
            char t[64]; snprintf(t, sizeof t, ",\"s\":%.3f,\"d\":%.3f}", ts, du); bs(&j, t);
        }
        bs(&j, "]}");
    } else if (error) {
        bs(&j, "{\"error\":"); bjson(&j, utf8(m0(error, "localizedDescription"))); bs(&j, "}");
    } else { os_unfair_lock_unlock(&c->lock); return; }
    c->json = j.p; c->done = 1;
    os_unfair_lock_unlock(&c->lock);
    dispatch_semaphore_signal(c->sema);
}

static char *recognize(const char *audio_path, bool on_device) {
    id locale = m0(C("NSLocale"), "currentLocale");
    id rec = m1(m0(C("SFSpeechRecognizer"), "alloc"), "initWithLocale:", locale);
    if (!rec) rec = m1(m0(C("SFSpeechRecognizer"), "alloc"), "initWithLocale:",
                       m1(C("NSLocale"), "localeWithLocaleIdentifier:", NS("en-US")));
    if (!rec) return strdup("{\"error\":\"Speech recognition isn't supported for your language.\"}");
    if (!((signed char (*)(id, SEL))objc_msgSend)(rec, S("isAvailable")))
        return strdup("{\"error\":\"Speech recognition is unavailable. Turn on Dictation in System Settings > Keyboard, then try again.\"}");
    m1(rec, "setQueue:", NEW("NSOperationQueue"));
    id url = m1(C("NSURL"), "fileURLWithPath:", NS(audio_path));
    id req = m1(m0(C("SFSpeechURLRecognitionRequest"), "alloc"), "initWithURL:", url);
    mB(req, "setShouldReportPartialResults:", false);
    if (on_device) mB(req, "setRequiresOnDeviceRecognition:", true);
    if (responds(req, "setAddsPunctuation:")) mB(req, "setAddsPunctuation:", true);
    TCtx *c = calloc(1, sizeof *c); c->sema = dispatch_semaphore_create(0);
    ((id (*)(id, SEL, id, void *))objc_msgSend)(rec, S("recognitionTaskWithRequest:resultHandler:"), req, mkblock((void *)on_result, c));
    long timed_out = dispatch_semaphore_wait(c->sema, dispatch_time(0, 600LL * 1000000000LL));
    char *out = 0;
    os_unfair_lock_lock(&c->lock);
    if (!timed_out && c->json) out = strdup(c->json);
    c->done = 1;  /* ctx, recognizer and request are kept alive on purpose (late callbacks) */
    os_unfair_lock_unlock(&c->lock);
    return out ? out : strdup("{\"error\":\"Transcription timed out.\"}");
}

static uint32_t SPEECH_LOCK;   /* one recognition at a time */
static char *transcribe(int64_t mid, const char *audio_path) {
    void *pool = objc_autoreleasePoolPush();
    char *out;
    long st = speech_auth();
    if (st == 3) {
        os_unfair_lock_lock(&SPEECH_LOCK);
        id probe = m0(m0(C("SFSpeechRecognizer"), "alloc"), "init");
        bool od = probe && responds(probe, "supportsOnDeviceRecognition") &&
                  ((signed char (*)(id, SEL))objc_msgSend)(probe, S("supportsOnDeviceRecognition"));
        out = recognize(audio_path, od);
        if (od && strstr(out, "\"error\"")) { free(out); out = recognize(audio_path, false); }
        os_unfair_lock_unlock(&SPEECH_LOCK);
    } else if (st == -1) out = strdup("{\"error\":\"Captions need macOS 10.15 or later.\"}");
    else out = strdup("{\"error\":\"denied\"}");
    objc_autoreleasePoolPop(pool);
    (void)mid;
    return out;
}
#endif

static char *transcript_json(int64_t id) {
    char cp[2200]; transcript_cache_path(id, cp, sizeof cp);
    char *cached = read_text(cp); if (cached) return cached;
    char *src = path_for(id);
    if (!src || access(src, R_OK) != 0) { free(src); return strdup("{\"error\":\"missing\"}"); }
    char *m4a = converted(id, src); free(src);
    if (!m4a) return strdup("{\"error\":\"convert failed\"}");
    char *j = transcribe(id, m4a); free(m4a);
    if (!strstr(j, "\"error\"")) write_text(cp, j);
    return j;
}

/* --------------------------------------------------------- gameplay */
/* Gameplay is a local mp4 so it can be drawn into the export canvas.
   Presets are fetched from YouTube with yt-dlp (downloaded on first use);
   "custom" is any video file the user picks. */
typedef struct { int state; double pct; int version; char msg[300]; } Game; /* 0 none, 1 busy, 2 ready, 3 error */
static Game GAMES[2];
static const char *GAME_FILE[2] = {"/gameplay/subway.mp4", "/gameplay/minecraft.mp4"};
static const char *GAME_QUERY[2] = {"ytsearch25:subway surfers gameplay no copyright",
                                    "ytsearch25:minecraft parkour gameplay no copyright"};

static void set_game(int g, int state, double pct, const char *msg) {
    os_unfair_lock_lock(&LOCK);
    GAMES[g].state = state; GAMES[g].pct = pct;
    if (msg) snprintf(GAMES[g].msg, sizeof GAMES[g].msg, "%s", msg);
    if (state == 2) GAMES[g].version++;
    os_unfair_lock_unlock(&LOCK);
}

typedef struct { int g; int checked; char err[300]; char last[300]; } DlCtx;
static double parse_pct(const char *line) {   /* last "12.3%" in the line, or -1 */
    double best = -1;
    for (const char *p = line; *p; p++) {
        if (*p == '%') {
            const char *q = p; while (q > line && ((q[-1] >= '0' && q[-1] <= '9') || q[-1] == '.')) q--;
            if (q < p) { double v = 0, sc = 0; for (const char *r = q; r < p; r++) { if (*r == '.') sc = 1; else { v = v * 10 + (*r - '0'); if (sc) sc *= 10; } } best = sc ? v / sc : v; }
        }
    }
    return best;
}
static void on_curl_line(const char *line, void *ctx) {
    DlCtx *c = ctx; double v = parse_pct(line);
    if (v >= 0) { char m[120]; snprintf(m, sizeof m, "Setting up the video downloader (one time)… %d%%", (int)v); set_game(c->g, 1, v, m); }
}
static void on_ytdlp_line(const char *line, void *ctx) {
    DlCtx *c = ctx;
    snprintf(c->last, sizeof c->last, "%s", line);
    if (strstr(line, "PROGRESS")) {
        const char *bar = strchr(line, '|');
        char head[64] = {0}; if (bar) snprintf(head, sizeof head, "%.*s", (int)(bar - line), line);
        double v = parse_pct(bar ? head : line);
        if (v >= 0) {
            char m[200] = "Downloading gameplay…";
            if (bar) {   /* "|1.2MiB/s|00:42" */
                char speed[40] = {0}, eta[40] = {0}; const char *b2 = strchr(bar + 1, '|');
                if (b2) { snprintf(speed, sizeof speed, "%.*s", (int)(b2 - bar - 1), bar + 1); snprintf(eta, sizeof eta, "%s", b2 + 1); }
                char *sp = speed; while (*sp == ' ') sp++;
                char *et = eta; while (*et == ' ') et++;
                snprintf(m, sizeof m, "Downloading gameplay… · %s · %s left", sp, et);
            }
            set_game(c->g, 1, v, m);
        }
    } else if (!strncmp(line, "ERROR", 5)) {
        snprintf(c->err, sizeof c->err, "%s", line);
    } else if (strstr(line, "[youtube:search]")) {
        set_game(c->g, 1, 0, "Searching YouTube…");
    } else if (strstr(line, "Extracting URL")) {
        c->checked++;
        char m[120]; snprintf(m, sizeof m, "Checking videos (%d)…", c->checked); set_game(c->g, 1, 0, m);
    } else if (strstr(line, "does not pass filter")) {
        /* too short/long: keep looking */
    } else if (strstr(line, "[download] Destination")) {
        set_game(c->g, 1, 0, "Downloading gameplay…");
    }
}

static void *download_game(void *arg) {
    int g = (int)(intptr_t)arg;
    char *yt = cat3(SUPPORT, "/yt-dlp_macos", "");
    char *dst = cat3(SUPPORT, GAME_FILE[g], ""), *tmp = cat3(dst, ".download.mp4", "");
    DlCtx ctx = {g, 0, {0}, {0}};
    int to = 0;
    if (access(yt, X_OK) != 0) {
        set_game(g, 1, 0, "Setting up the video downloader (one time)…");
        char *part = cat3(yt, ".part", "");
        unlink(part);
        char *argv[] = {"/usr/bin/curl", "-fL", "--progress-bar", "--connect-timeout", "20", "-o", part,
                        "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos", 0};
        int rc = run_lines(argv, on_curl_line, &ctx, 60, 900, &to);
        if (rc != 0 || file_size(part) < 1000000) {
            unlink(part); free(part);
            set_game(g, 3, 0, to ? "The downloader setup stalled (no data for a minute). Check your internet and hit ↻ New, or use your own video file."
                                 : "Couldn't download the video downloader from GitHub. Check your internet and hit ↻ New, or use your own video file.");
            goto done;
        }
        chmod(part, 0755); rename(part, yt); free(part);
    }
    set_game(g, 1, 0, "Starting the downloader (first run can take ~20s)…");
    unlink(tmp);
    {
        /* YouTube throttles downloaders that can't solve its JS challenge; use a JS runtime if one is installed */
        const char *rt_paths[] = {"/opt/homebrew/bin/node", "/usr/local/bin/node", "/opt/homebrew/bin/deno", "/usr/local/bin/deno", 0};
        char rtarg[200] = {0};
        for (int i = 0; rt_paths[i]; i++) if (access(rt_paths[i], X_OK) == 0) {
            snprintf(rtarg, sizeof rtarg, "%s:%s", strstr(rt_paths[i], "deno") ? "deno" : "node", rt_paths[i]); break; }
        char *argv[40]; int a = 0;
        argv[a++] = yt; argv[a++] = (char *)GAME_QUERY[g];
        argv[a++] = "--no-config"; argv[a++] = "--ignore-errors"; argv[a++] = "--socket-timeout"; argv[a++] = "20";
        argv[a++] = "--playlist-random"; argv[a++] = "--max-downloads"; argv[a++] = "1";
        argv[a++] = "--match-filter"; argv[a++] = "duration>300 & duration<1500 & !is_live";
        argv[a++] = "-f"; argv[a++] = "bv*[ext=mp4][vcodec^=avc1][height<=720][height>=360]/b[ext=mp4][height<=720]/bv*[ext=mp4][height<=720]";
        argv[a++] = "--max-filesize"; argv[a++] = "400M";
        argv[a++] = "--http-chunk-size"; argv[a++] = "10M";
        if (rtarg[0]) { argv[a++] = "--js-runtimes"; argv[a++] = rtarg; argv[a++] = "--remote-components"; argv[a++] = "ejs:github"; }
        argv[a++] = "--no-part"; argv[a++] = "--force-overwrites"; argv[a++] = "--no-mtime"; argv[a++] = "--newline";
        argv[a++] = "--progress-template";
        argv[a++] = "download:PROGRESS %(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s";
        argv[a++] = "-o"; argv[a++] = tmp; argv[a] = 0;
        run_lines(argv, on_ytdlp_line, &ctx, 120, 900, &to);
        if (file_size(tmp) > 100000) { rename(tmp, dst); set_game(g, 2, 100, "Ready"); }
        else {
            unlink(tmp);
            Buf m = {0};
            bs(&m, to ? "YouTube download stalled, so I stopped it. " : "YouTube download failed. ");
            bs(&m, "Hit ↻ New to retry, or pick “My own video file”.");
            const char *why = ctx.err[0] ? ctx.err : ctx.last;
            if (why[0]) { bs(&m, " Last message: "); bput(&m, why, strlen(why) > 220 ? 220 : strlen(why)); }
            set_game(g, 3, 0, m.p); free(m.p);
        }
    }
done:
    free(yt); free(dst); free(tmp);
    return 0;
}

static char *custom_video_path(void) {
    char *cfg = cat3(SUPPORT, "/custom-video.txt", "");
    FILE *f = fopen(cfg, "rb"); free(cfg);
    if (!f) return 0;
    char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    if (!n || access(buf, R_OK) != 0) return 0;
    return strdup(buf);
}

/* {"state":"ready|busy|error|none","pct":..,"msg":..,"src":..} ; start=1 kicks off a download if needed, 2 forces a new one */
static char *gameplay_status(const char *q, int start) {
    Buf j = {0};
    if (!strcmp(q, "custom")) {
        char *p = custom_video_path();
        bs(&j, p ? "{\"state\":\"ready\",\"src\":\"/gameplay/custom\"," : "{\"state\":\"none\",");
        bs(&j, "\"name\":"); bjson(&j, p ? (strrchr(p, '/') ? strrchr(p, '/') + 1 : p) : ""); bs(&j, "}");
        free(p); return j.p;
    }
    int g = !strcmp(q, "minecraft") ? 1 : 0;
    char *dst = cat3(SUPPORT, GAME_FILE[g], "");
    int have = file_size(dst) > 100000; free(dst);
    os_unfair_lock_lock(&LOCK);
    int busy = GAMES[g].state == 1;
    int kick = !busy && ((start == 1 && !have && GAMES[g].state != 3) || start == 2 || (start == 1 && GAMES[g].state == 3 && !have));
    if (kick) { GAMES[g].state = 1; GAMES[g].pct = 0; snprintf(GAMES[g].msg, sizeof GAMES[g].msg, "Starting…"); }
    Game snap = GAMES[g];
    os_unfair_lock_unlock(&LOCK);
    if (kick) { void *th; pthread_create(&th, 0, download_game, (void *)(intptr_t)g); pthread_detach(th); snap.state = 1; }
    const char *st = snap.state == 1 ? "busy" : snap.state == 3 && !have ? "error" : have ? "ready" : "none";
    bs(&j, "{\"state\":\""); bs(&j, st); bs(&j, "\"");
    bf(&j, ",\"pct\":%lld", (long long)snap.pct);
    bs(&j, ",\"msg\":"); bjson(&j, snap.msg);
    bs(&j, ",\"have\":"); bs(&j, have ? "true" : "false");
    bs(&j, ",\"src\":\"/gameplay/"); bs(&j, g ? "minecraft" : "subway");
    bf(&j, "?v=%lld\"}", snap.version);
    return j.p;
}

static int pick_video(void) {
    Buf out = {0};
    char *argv[] = {"/usr/bin/osascript", "-e", "activate",
        "-e", "POSIX path of (choose file with prompt \"Pick a gameplay video\" of type {\"public.movie\"})", 0};
    int rc = run(argv, &out);
    if (rc != 0 || !out.p || !out.n) { free(out.p); return 0; }
    while (out.n && (out.p[out.n - 1] == '\n' || out.p[out.n - 1] == '\r')) out.p[--out.n] = 0;
    char *cfg = cat3(SUPPORT, "/custom-video.txt", "");
    FILE *f = fopen(cfg, "wb"); if (f) { fwrite(out.p, 1, out.n, f); fclose(f); }
    free(cfg); free(out.p);
    return 1;
}

/* --------------------------------------------------------- HTTP server */
extern const char INDEX_HTML[]; extern const size_t INDEX_HTML_LEN;

static void send_all(int fd, const char *p, size_t n) {
    while (n) { ssize_t k = write(fd, p, n); if (k < 0) { if (*__error() == EINTR) continue; return; } p += k; n -= (size_t)k; }
}
static void respond(int fd, int code, const char *type, const char *body, size_t len, const char *extra) {
    const char *msg = code == 200 ? "OK" : code == 206 ? "Partial Content" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
    char h[1024];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n",
                     code, msg, type, (unsigned long)len, extra ? extra : "");
    send_all(fd, h, (size_t)n);
    if (len && body) send_all(fd, body, len);
}
static void respond_json(int fd, int code, const char *j) { respond(fd, code, "application/json", j, strlen(j), 0); }

static void open_url(const char *a, const char *b) {
    char *argv[] = {"/usr/bin/open", (char *)a, (char *)b, 0};
    run(argv, 0);
}

/* stream a file with HTTP Range support */
static void serve_file(int fd, const char *path, const char *range, const char *type) {
    FILE *f = fopen(path, "rb");
    if (!f) return respond_json(fd, 404, "{\"error\":\"missing\"}");
    fseeko(f, 0, 2); long long size = ftello(f);
    if (size <= 0) { fclose(f); return respond_json(fd, 500, "{\"error\":\"empty file\"}"); }
    long long a = 0, b = size - 1; int partial = 0;
    if (range) {
        const char *r = range; partial = 1;
        if (*r >= '0' && *r <= '9') { a = 0; while (*r >= '0' && *r <= '9') a = a * 10 + (*r++ - '0'); }
        else a = -1;
        if (*r == '-') r++;
        if (*r >= '0' && *r <= '9') { long long e = 0; while (*r >= '0' && *r <= '9') e = e * 10 + (*r++ - '0');
            if (a < 0) { a = size - e; b = size - 1; } else b = e; }
        if (a < 0) a = 0;
        if (b >= size) b = size - 1;
        if (a > b) a = b;
    }
    char extra[200];
    if (partial) snprintf(extra, sizeof extra, "Accept-Ranges: bytes\r\nContent-Range: bytes %lld-%lld/%lld\r\n", a, b, size);
    else snprintf(extra, sizeof extra, "Accept-Ranges: bytes\r\n");
    respond(fd, partial ? 206 : 200, type, 0, (size_t)(b - a + 1), extra);
    fseeko(f, a, 0);
    char *buf = malloc(1 << 16); long long left = b - a + 1;
    while (left > 0) {
        size_t want = left > (1 << 16) ? (1 << 16) : (size_t)left;
        size_t k = fread(buf, 1, want, f); if (!k) break;
        send_all(fd, buf, k); left -= (long long)k;
    }
    free(buf); fclose(f);
}

static void serve_audio(int fd, const char *idstr, const char *range) {
    int64_t id = 0; for (const char *c = idstr; *c >= '0' && *c <= '9'; c++) id = id * 10 + (*c - '0');
    char *src = path_for(id);
    if (!src || access(src, R_OK) != 0) { free(src); return respond_json(fd, 404, "{\"error\":\"missing\"}"); }
    char *dst = converted(id, src); free(src);
    if (!dst) return respond_json(fd, 500, "{\"error\":\"convert failed\"}");
    serve_file(fd, dst, range, "audio/mp4"); free(dst);
}

static const char *video_type(const char *p) {
    const char *e = strrchr(p, '.');
    if (e && (!strcmp(e, ".mov") || !strcmp(e, ".MOV"))) return "video/quicktime";
    if (e && (!strcmp(e, ".m4v") || !strcmp(e, ".M4V"))) return "video/x-m4v";
    if (e && !strcmp(e, ".webm")) return "video/webm";
    return "video/mp4";
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
/* value of query param `key`, URL-decoded, into out */
static int qparam(const char *q, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (const char *p = q; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1; size_t n = 0;
            while (*p && *p != '&' && n + 1 < cap) {
                if (*p == '%' && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { out[n++] = (char)(hexv(p[1]) * 16 + hexv(p[2])); p += 3; }
                else if (*p == '+') { out[n++] = ' '; p++; }
                else out[n++] = *p++;
            }
            out[n] = 0; return 1;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    out[0] = 0; return 0;
}

static const char *header_value(char *headers, const char *name) {
    size_t nl = strlen(name);
    for (char *l = headers; (l = strchr(l, '\n')); ) {
        l++;
        size_t i = 0;
        for (; i < nl; i++) if ((l[i] | 32) != (name[i] | 32)) break;
        if (i == nl && l[nl] == ':') { char *v = l + nl + 1; while (*v == ' ') v++; return v; }
    }
    return 0;
}

/* POST /api/save?name=... : stream the request body to ~/Movies/Brainrot Voice Memo/<name> */
static void save_upload(int fd, const char *query, char *headers, const char *body, size_t have) {
    char raw[200], name[200]; qparam(query, "name", raw, sizeof raw);
    size_t n = 0;
    for (const char *c = raw; *c && n + 1 < sizeof name; c++)
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '-' || *c == '_' || *c == '.')
            name[n++] = *c;
    name[n] = 0;
    if (!n || name[0] == '.') snprintf(name, sizeof name, "Brainrot.mp4");
    const char *cl = header_value(headers, "Content-Length");
    long long total = 0; for (const char *c = cl; c && *c >= '0' && *c <= '9'; c++) total = total * 10 + (*c - '0');
    char *mov = cat3(HOME, "/Movies", ""); mkdir(mov, 0755);
    char *dir = cat3(mov, "/Brainrot Voice Memo", ""); mkdir(dir, 0755);
    char *full = cat3(dir, "/", name);
    FILE *f = fopen(full, "wb");
    if (!f) { free(mov); free(dir); free(full); return respond_json(fd, 500, "{\"error\":\"Couldn't write to Movies folder\"}"); }
    long long got = 0;
    if (have) { fwrite(body, 1, have, f); got = (long long)have; }
    char *buf = malloc(1 << 16);
    while (got < total) {
        ssize_t k = read(fd, buf, 1 << 16);
        if (k <= 0) { if (k < 0 && *__error() == EINTR) continue; break; }
        fwrite(buf, 1, (size_t)k, f); got += k;
    }
    free(buf); fclose(f);
    Buf j = {0};
    if (got < total) { unlink(full); bs(&j, "{\"error\":\"upload interrupted\"}"); respond_json(fd, 500, j.p); }
    else { bs(&j, "{\"path\":"); bjson(&j, full); bs(&j, "}"); respond_json(fd, 200, j.p); open_url("-R", full); }
    free(j.p); free(mov); free(dir); free(full);
}

/* ------------------------------------------------- compose (video + voice audio) */
typedef struct { int64_t id; double at, dur; } PlanItem;
static int parse_plan(const char *plan, PlanItem *out, int cap) {
    int n = 0; const char *p = plan;
    while (p && *p && n < cap) {
        long long id = 0; while (*p >= '0' && *p <= '9') id = id * 10 + (*p++ - '0');
        if (*p++ != ':') break;
        double v[2] = {0, 0};
        for (int k = 0; k < 2; k++) {
            double x = 0, sc = 0; while ((*p >= '0' && *p <= '9') || *p == '.') { if (*p == '.') sc = 1; else { x = x * 10 + (*p - '0'); if (sc) sc *= 10; } p++; }
            v[k] = sc ? x / sc : x;
            if (k == 0) { if (*p++ != ':') return n; }
        }
        out[n].id = id; out[n].at = v[0]; out[n].dur = v[1]; n++;
        if (*p == ',') p++; else break;
    }
    return n;
}

#ifdef BV_LINUX_TEST
static char *compose_video(const char *video, PlanItem *items, int n, double total, const char *out_path) {
    (void)total;
    Buf fc = {0}; char *argv[200]; int a = 0; char *own[100]; int no = 0;
    argv[a++] = "/usr/bin/ffmpeg"; argv[a++] = "-y"; argv[a++] = "-loglevel"; argv[a++] = "error"; argv[a++] = "-i"; argv[a++] = (char *)video;
    for (int i = 0; i < n; i++) {
        char *src = path_for(items[i].id); char *m4a = src ? converted(items[i].id, src) : 0; free(src);
        if (!m4a) return strdup("missing audio");
        argv[a++] = "-i"; argv[a++] = own[no++] = m4a;
        char t[96]; snprintf(t, sizeof t, "[%d]adelay=%d|%d[a%d];", i + 1, (int)(items[i].at * 1000), (int)(items[i].at * 1000), i); bs(&fc, t);
    }
    for (int i = 0; i < n; i++) { char t[16]; snprintf(t, sizeof t, "[a%d]", i); bs(&fc, t); }
    char t[64]; snprintf(t, sizeof t, "amix=inputs=%d:normalize=0[a]", n); bs(&fc, t);
    argv[a++] = "-filter_complex"; argv[a++] = fc.p; argv[a++] = "-map"; argv[a++] = "0:v"; argv[a++] = "-map"; argv[a++] = "[a]";
    argv[a++] = "-c:v"; argv[a++] = "copy"; argv[a++] = "-c:a"; argv[a++] = "aac"; argv[a++] = (char *)out_path; argv[a] = 0;
    int rc = run(argv, 0);
    for (int i = 0; i < no; i++) free(own[i]); free(fc.p);
    return rc == 0 ? 0 : strdup("ffmpeg failed");
}
#else
typedef struct { int64_t value; int32_t timescale; uint32_t flags; int64_t epoch; } CMTime;
typedef struct { CMTime start, duration; } CMTimeRange;
static CMTime cmt(double sec) { CMTime t = {(int64_t)(sec * 600.0), 600, 1, 0}; return t; }
static id first_track(id asset, const char *type) {
    id arr = m1(asset, "tracksWithMediaType:", NS(type));
    if (!arr || !((unsigned long (*)(id, SEL))objc_msgSend)(arr, S("count"))) return 0;
    return ((id (*)(id, SEL, unsigned long))objc_msgSend)(arr, S("objectAtIndex:"), 0);
}
static id file_asset(const char *path) {
    id url = m1(C("NSURL"), "fileURLWithPath:", NS(path));
    return ((id (*)(id, SEL, id, id))objc_msgSend)(C("AVURLAsset"), S("URLAssetWithURL:options:"), url, 0);
}
static bool insert_range(id dst, id src, double dur, double at, char *err, size_t cap) {
    CMTimeRange r = {cmt(0), cmt(dur)}; id e = 0;
    bool ok = ((signed char (*)(id, SEL, CMTimeRange, id, CMTime, id *))objc_msgSend)(dst, S("insertTimeRange:ofTrack:atTime:error:"), r, src, cmt(at), &e) != 0;
    if (!ok) snprintf(err, cap, "%s", e ? utf8(m0(e, "localizedDescription")) : "insert failed");
    return ok;
}
typedef struct { void *sema; } DoneCtx;
static void on_export_done(Blk *b) { DoneCtx *c = b->ctx; dispatch_semaphore_signal(c->sema); }
static struct BlockDesc BDESC_VOID = {0, sizeof(Blk), "v8@?0"};

static char *export_with(id comp, const char *preset, const char *out_path) {
    unlink(out_path);
    id ex = ((id (*)(id, SEL, id, id))objc_msgSend)(m0(C("AVAssetExportSession"), "alloc"), S("initWithAsset:presetName:"), comp, NS(preset));
    if (!ex) return strdup("export session unavailable");
    m1(ex, "setOutputURL:", m1(C("NSURL"), "fileURLWithPath:", NS(out_path)));
    m1(ex, "setOutputFileType:", NS("public.mpeg-4"));
    DoneCtx *c = calloc(1, sizeof *c); c->sema = dispatch_semaphore_create(0);
    Blk *blk = mkblock((void *)on_export_done, c); blk->desc = &BDESC_VOID;
    ((void (*)(id, SEL, void *))objc_msgSend)(ex, S("exportAsynchronouslyWithCompletionHandler:"), blk);
    dispatch_semaphore_wait(c->sema, dispatch_time(0, 900LL * 1000000000LL));
    long st = ((long (*)(id, SEL))objc_msgSend)(ex, S("status"));
    if (st == 3) return 0;   /* AVAssetExportSessionStatusCompleted */
    id e = m0(ex, "error");
    return strdup(e ? utf8(m0(e, "localizedDescription")) : "export did not finish");
}

static char *compose_video(const char *video, PlanItem *items, int n, double total, const char *out_path) {
    void *pool = objc_autoreleasePoolPush();
    char err[400] = {0}; char *res = 0;
    id comp = m0(C("AVMutableComposition"), "composition");
    id vsrc = first_track(file_asset(video), "vide");
    if (!vsrc) { res = strdup("rendered video unreadable"); goto out; }
    id vtrk = ((id (*)(id, SEL, id, int32_t))objc_msgSend)(comp, S("addMutableTrackWithMediaType:preferredTrackID:"), NS("vide"), 0);
    if (!insert_range(vtrk, vsrc, total - 0.02, 0, err, sizeof err)) { res = strdup(err); goto out; }
    id atrk = ((id (*)(id, SEL, id, int32_t))objc_msgSend)(comp, S("addMutableTrackWithMediaType:preferredTrackID:"), NS("soun"), 0);
    for (int i = 0; i < n; i++) {
        char *src = path_for(items[i].id); char *m4a = src ? converted(items[i].id, src) : 0; free(src);
        if (!m4a) continue;
        id asrc = first_track(file_asset(m4a), "soun"); free(m4a);
        if (!asrc) continue;
        double d = items[i].dur - 0.03; if (d < 0.05) continue;
        if (!insert_range(atrk, asrc, d, items[i].at, err, sizeof err)) { res = strdup(err); goto out; }
    }
    res = export_with(comp, "AVAssetExportPresetPassthrough", out_path);
    if (res) { free(res); res = export_with(comp, "AVAssetExportPresetHighestQuality", out_path); }
out:
    objc_autoreleasePoolPop(pool);
    return res;
}
#endif

/* POST /api/compose?name=..&total=..&plan=id:at:dur,... with the rendered (silent) mp4 as the body */
static void compose_upload(int fd, const char *query, char *headers, const char *body, size_t have) {
    char raw[200], name[200], tot[32]; qparam(query, "name", raw, sizeof raw); qparam(query, "total", tot, sizeof tot);
    char *plan = malloc(8192); qparam(query, "plan", plan, 8192);
    size_t n = 0;
    for (const char *c = raw; *c && n + 1 < sizeof name; c++)
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '-' || *c == '_' || *c == '.') name[n++] = *c;
    name[n] = 0;
    if (!n || name[0] == '.') snprintf(name, sizeof name, "Brainrot.mp4");
    double total = 0, sc = 0; for (const char *c = tot; *c; c++) { if (*c == '.') sc = 1; else if (*c >= '0' && *c <= '9') { total = total * 10 + (*c - '0'); if (sc) sc *= 10; } }
    if (sc) total /= sc;
    PlanItem items[200]; int ni = parse_plan(plan, items, 200); free(plan);

    const char *cl = header_value(headers, "Content-Length");
    long long want = 0; for (const char *c = cl; c && *c >= '0' && *c <= '9'; c++) want = want * 10 + (*c - '0');
    char *video = cat3(CACHE, "/render.mp4", "");
    FILE *f = fopen(video, "wb");
    if (!f) { free(video); return respond_json(fd, 500, "{\"error\":\"can't write temp file\"}"); }
    long long got = 0; if (have) { fwrite(body, 1, have, f); got = (long long)have; }
    char *buf = malloc(1 << 16);
    while (got < want) { ssize_t k = read(fd, buf, 1 << 16); if (k <= 0) { if (k < 0 && *__error() == EINTR) continue; break; } fwrite(buf, 1, (size_t)k, f); got += k; }
    free(buf); fclose(f);
    if (got < want) { unlink(video); free(video); return respond_json(fd, 500, "{\"error\":\"upload interrupted\"}"); }

    char *mov = cat3(HOME, "/Movies", ""); mkdir(mov, 0755);
    char *dir = cat3(mov, "/Brainrot Voice Memo", ""); mkdir(dir, 0755);
    char *full = cat3(dir, "/", name);
    char *err = compose_video(video, items, ni, total, full);
    Buf j = {0};
    if (err) { bs(&j, "{\"error\":"); bjson(&j, err); bs(&j, "}"); respond_json(fd, 500, j.p); free(err); }
    else { bs(&j, "{\"path\":"); bjson(&j, full); bs(&j, "}"); respond_json(fd, 200, j.p); open_url("-R", full); }
    unlink(video);
    free(j.p); free(video); free(mov); free(dir); free(full);
}

static void *handle_conn(void *arg) {
    int fd = (int)(intptr_t)arg;
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    char req[16384]; size_t n = 0; char *hend = 0;
    while (n < sizeof req - 1) {
        ssize_t k = read(fd, req + n, sizeof req - 1 - n);
        if (k <= 0) break;
        n += (size_t)k; req[n] = 0;
        if ((hend = strstr(req, "\r\n\r\n"))) break;
    }
    req[n] = 0;
    if (!hend) { close(fd); return 0; }
    *hend = 0;                                   /* req = request line + headers */
    const char *body = hend + 4; size_t body_have = n - (size_t)(body - req);
    int is_post = !strncmp(req, "POST ", 5);
    if (!is_post && strncmp(req, "GET ", 4)) { respond_json(fd, 404, "{}"); close(fd); return 0; }
    char *path = req + (is_post ? 5 : 4), *sp = strchr(path, ' '); if (sp) *sp = 0;
    char *headers = sp ? sp + 1 : req + n;
    const char *rv = header_value(headers, "Range");
    const char *range = rv && !strncmp(rv, "bytes=", 6) ? rv + 6 : 0;
    char *query = strchr(path, '?'); if (query) *query++ = 0;
    char q[64]; qparam(query, "q", q, sizeof q);

    if (is_post) {
        if (!strcmp(path, "/api/save")) save_upload(fd, query, headers, body, body_have);
        else if (!strcmp(path, "/api/compose")) compose_upload(fd, query, headers, body, body_have);
        else respond_json(fd, 404, "{}");
    }
    else if (!strcmp(path, "/")) respond(fd, 200, "text/html; charset=utf-8", INDEX_HTML, INDEX_HTML_LEN, 0);
    else if (!strcmp(path, "/api/ping")) respond_json(fd, 200, "{\"brainrot-ok\":1}");
    else if (!strcmp(path, "/api/messages")) {
        int refresh = query && strstr(query, "refresh");
        char *body = 0; int code = 200;
        os_unfair_lock_lock(&LOCK); if (!refresh && MSG_JSON) body = strdup(MSG_JSON); os_unfair_lock_unlock(&LOCK);
        if (!body) {
            code = build_messages(&body);
            if (code == 200) { os_unfair_lock_lock(&LOCK); free(MSG_JSON); MSG_JSON = strdup(body); os_unfair_lock_unlock(&LOCK); }
        }
        respond_json(fd, code, body); free(body);
    }
    else if (!strncmp(path, "/audio/", 7)) serve_audio(fd, path + 7, range);
    else if (!strcmp(path, "/api/gameplay")) {
        int start = query && strstr(query, "new=1") ? 2 : query && strstr(query, "start=1") ? 1 : 0;
        char *j = gameplay_status(q[0] ? q : "subway", start); respond_json(fd, 200, j); free(j);
    }
    else if (!strncmp(path, "/api/transcript/", 16)) {
        int64_t id = 0; for (const char *c = path + 16; *c >= '0' && *c <= '9'; c++) id = id * 10 + (*c - '0');
        char *j = transcript_json(id); respond_json(fd, 200, j); free(j);
    }
    else if (!strcmp(path, "/api/pick-video")) respond_json(fd, 200, pick_video() ? "{\"ok\":true}" : "{\"ok\":false}");
    else if (!strncmp(path, "/gameplay/", 10)) {
        const char *which = path + 10;
        if (!strcmp(which, "custom")) { char *p = custom_video_path(); if (p) serve_file(fd, p, range, video_type(p)); else respond_json(fd, 404, "{}"); free(p); }
        else { char *p = cat3(SUPPORT, GAME_FILE[!strcmp(which, "minecraft") ? 1 : 0], ""); serve_file(fd, p, range, "video/mp4"); free(p); }
    }
    else if (!strcmp(path, "/api/open-fda")) {
        respond_json(fd, 200, "{}");
        open_url("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles", 0);
    }
    else if (!strcmp(path, "/api/reveal")) { respond_json(fd, 200, "{}"); open_url("-R", BUNDLE); }
    else if (!strcmp(path, "/api/relaunch")) {
        respond_json(fd, 200, "{}"); close(fd);
        char *argv[] = {"/bin/sh", "-c", "sleep 1; /usr/bin/open \"$0\"", BUNDLE, 0};
        pid_t pid; void *fa = 0; posix_spawn_file_actions_init(&fa);
        posix_spawn(&pid, "/bin/sh", &fa, 0, argv, *_NSGetEnviron());
        exit(0);
    }
    else respond_json(fd, 404, "{}");
    close(fd);
    return 0;
}

static void *accept_loop(void *arg) {
    int lfd = (int)(intptr_t)arg;
    for (;;) {
        int c = accept(lfd, 0, 0);
        if (c < 0) { if (*__error() == EINTR) continue; return 0; }   /* socket closed: stop */
        cloexec(c);
        void *th; if (pthread_create(&th, 0, handle_conn, (void *)(intptr_t)c) == 0) pthread_detach(th); else close(c);
    }
    return 0;
}

static struct sockaddr_in loopback(int port) {
    struct sockaddr_in a = {0}; SIN_LEN(a) a.sin_family = AF_INET;
    a.sin_port = (uint16_t)((port >> 8) | ((port & 0xff) << 8));
    a.sin_addr = 0x0100007f; /* 127.0.0.1 */
    return a;
}

/* Ask our own server for /api/ping. Catches a port that some other process is also holding. */
static int self_test(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
    cloexec(s);
    struct timeval_ tv = {2, 0}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO_, &tv, sizeof tv);
    struct sockaddr_in a = loopback(port);
    int ok = 0;
    if (connect(s, &a, sizeof a) == 0) {
        const char *req = "GET /api/ping HTTP/1.0\r\n\r\n";
        send_all(s, req, strlen(req));
        char buf[256]; size_t n = 0; ssize_t k;
        while (n < sizeof buf - 1 && (k = read(s, buf + n, sizeof buf - 1 - n)) > 0) n += (size_t)k;
        buf[n] = 0;
        ok = strstr(buf, "brainrot-ok") != 0;
    }
    close(s);
    return ok;
}

static int start_server(void) {
    /* Stop helper downloads (curl / yt-dlp) left behind by an earlier run that crashed or was
       force-quit; older builds let them inherit the server socket, which blocks the port. */
    char *argv[] = {"/usr/bin/pkill", "-f", "Application Support/BrainrotVoicemail/", 0};
    if (run(argv, 0) == 0) usleep(400000);

    int ports[] = {47811, 47812, 47813, 47814, 47815, 0, 0};
    for (int i = 0; i < 7; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) continue;
        cloexec(fd);
        struct sockaddr_in a = loopback(ports[i]);
        if (bind(fd, &a, sizeof a) != 0 || listen(fd, 64) != 0) { close(fd); continue; }
        int port = ports[i];
        if (!port) {   /* ephemeral: find out which one we got */
            struct sockaddr_in got = {0}; socklen_t len = sizeof got;
            int getsockname(int, void *, socklen_t *);
            getsockname(fd, &got, &len);
            port = ((got.sin_port & 0xff) << 8) | (got.sin_port >> 8);
        }
        void *th; pthread_create(&th, 0, accept_loop, (void *)(intptr_t)fd); pthread_detach(th);
        if (self_test(port)) return port;
        close(fd);   /* someone else answers here: try another port */
    }
    return 0;
}

#ifdef BV_LINUX_TEST
unsigned int sleep(unsigned int);
int bv_test_main(void) {
    signal(SIGPIPE, SIG_IGN);
    HOME = getenv("HOME");
    CACHE = cat3(HOME, "/Library/Caches/BrainrotVoicemail", "");
    SUPPORT = cat3(HOME, "/Library/Application Support/BrainrotVoicemail", "");
    mkdirs();
    snprintf(BUNDLE, sizeof BUNDLE, "/tmp/Brainrot Voice Memo.app");
    int port = start_server();
    snprintf(BUNDLE, sizeof BUNDLE, "%d", port); write(1, BUNDLE, strlen(BUNDLE)); write(1, "\n", 1);
    for (;;) sleep(1000);
}
#else
/* ------------------------------------------------------------ AppKit UI */
static int PORT;

static id menu_item(const char *title, const char *action, const char *key) {
    id it = m0(C("NSMenuItem"), "alloc");
    return ((id (*)(id, SEL, id, SEL, id))objc_msgSend)(it, S("initWithTitle:action:keyEquivalent:"), NS(title), action ? S(action) : 0, NS(key));
}
static id submenu(id bar, const char *title) {
    id item = NEW("NSMenuItem"); m1(bar, "addItem:", item);
    id menu = m1(m0(C("NSMenu"), "alloc"), "initWithTitle:", NS(title));
    m1(item, "setSubmenu:", menu);
    return menu;
}

static void did_finish(id self, SEL _cmd, id note) {
    (void)self; (void)_cmd; (void)note;
    id app = m0(C("NSApplication"), "sharedApplication");

    id bar = NEW("NSMenu");
    id am = submenu(bar, "Brainrot Voice Memo");
    m1(am, "addItem:", menu_item("Hide Brainrot Voice Memo", "hide:", "h"));
    m1(am, "addItem:", m0(C("NSMenuItem"), "separatorItem"));
    m1(am, "addItem:", menu_item("Quit Brainrot Voice Memo", "terminate:", "q"));
    id em = submenu(bar, "Edit");
    m1(em, "addItem:", menu_item("Cut", "cut:", "x"));
    m1(em, "addItem:", menu_item("Copy", "copy:", "c"));
    m1(em, "addItem:", menu_item("Paste", "paste:", "v"));
    m1(em, "addItem:", menu_item("Select All", "selectAll:", "a"));
    id vm = submenu(bar, "View");
    m1(vm, "addItem:", menu_item("Reload", "reload:", "r"));
    id wm = submenu(bar, "Window");
    m1(wm, "addItem:", menu_item("Minimize", "performMiniaturize:", "m"));
    m1(wm, "addItem:", menu_item("Close", "performClose:", "w"));
    m1(app, "setMainMenu:", bar);

    CGRect r = {0, 0, 1240, 780};
    id win = ((id (*)(id, SEL, CGRect, unsigned long, unsigned long, bool))objc_msgSend)(
        m0(C("NSWindow"), "alloc"), S("initWithContentRect:styleMask:backing:defer:"), r, 1 | 2 | 4 | 8, 2, false);
    m1(win, "setTitle:", NS("Brainrot Voice Memo"));
    ((void (*)(id, SEL, CGSize))objc_msgSend)(win, S("setMinSize:"), (CGSize){900, 560});
    m1(win, "setAppearance:", m1(C("NSAppearance"), "appearanceNamed:", NS("NSAppearanceNameDarkAqua")));
    m1(win, "setBackgroundColor:", m0(C("NSColor"), "blackColor"));
    m0(win, "center");
    m1(win, "setFrameAutosaveName:", NS("BrainrotMain"));

    id cfg = NEW("WKWebViewConfiguration");
    mI(cfg, "setMediaTypesRequiringUserActionForPlayback:", 0);
    id wv = ((id (*)(id, SEL, CGRect, id))objc_msgSend)(m0(C("WKWebView"), "alloc"), S("initWithFrame:configuration:"), r, cfg);
    m1(wv, "setCustomUserAgent:", NS("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15"));
    mI(wv, "setAutoresizingMask:", 2 | 16);
    m1(win, "setContentView:", wv);

    /* Load the UI straight from memory with the local server as its origin, so the window
       is never blank even if the server can't be reached (the page then shows an error). */
    char url[64]; snprintf(url, sizeof url, "http://127.0.0.1:%d/", PORT ? PORT : 47811);
    id nsurl = m1(C("NSURL"), "URLWithString:", NS(url));
    char *html = malloc(INDEX_HTML_LEN + 1); memcpy(html, INDEX_HTML, INDEX_HTML_LEN); html[INDEX_HTML_LEN] = 0;
    ((id (*)(id, SEL, id, id))objc_msgSend)(wv, S("loadHTMLString:baseURL:"), NS(html), nsurl);
    free(html);

    m1(win, "makeKeyAndOrderFront:", 0);
    m1(win, "makeFirstResponder:", wv);
    mB(app, "activateIgnoringOtherApps:", true);
}
static bool should_quit(id self, SEL _cmd, id app) { (void)self; (void)_cmd; (void)app; return true; }

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    HOME = getenv("HOME"); if (!HOME) HOME = "/tmp";
    CACHE = cat3(HOME, "/Library/Caches/BrainrotVoicemail", "");
    SUPPORT = cat3(HOME, "/Library/Application Support/BrainrotVoicemail", "");
    mkdirs();
    uint32_t sz = sizeof BUNDLE;
    if (_NSGetExecutablePath(BUNDLE, &sz) == 0) {
        for (int i = 0; i < 3; i++) { char *s = strrchr(BUNDLE, '/'); if (s) *s = 0; }
    }

    PORT = start_server();

    m0(m0(C("NSAutoreleasePool"), "alloc"), "init");
    id app = m0(C("NSApplication"), "sharedApplication");
    mI(app, "setActivationPolicy:", 0);

    Class D = objc_allocateClassPair(C("NSObject"), "BVAppDelegate", 0);
    class_addMethod(D, S("applicationDidFinishLaunching:"), (void (*)(void))did_finish, "v@:@");
    class_addMethod(D, S("applicationShouldTerminateAfterLastWindowClosed:"), (void (*)(void))should_quit, "B@:@");
    objc_registerClassPair(D);
    m1(app, "setDelegate:", NEW("BVAppDelegate"));
    m0(app, "run");
    return 0;
}
#endif
