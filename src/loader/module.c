#include <screw/loader.h>
#include <screw/log.h>

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef int WINAPI (*dll_entry)(void *base, uint32_t reason, void *reserved);

static struct module *head, *tail, *main_module;
static struct module **order;
static size_t order_count, order_cap;

void module_normalize(const char *in, char out[MODULE_NAME_MAX])
{
    size_t n = 0;
    for (; in[n] && n < MODULE_NAME_MAX - 5; n++)
        out[n] = in[n] == '\\' ? '/' : tolower((unsigned char)in[n]);
    out[n] = 0;
    char *slash = strrchr(out, '/');
    if (slash)
        memmove(out, slash + 1, strlen(slash));
    if (!strchr(out, '.'))
        strcat(out, ".dll");
}

struct module *module_list(void) { return head; }
struct module *module_main(void) { return main_module; }

static void link(struct module *m)
{
    if (tail)
        tail->next = m;
    else
        head = m;
    tail = m;
}

static void ordered(struct module *m)
{
    if (order_count == order_cap) {
        order_cap = order_cap ? order_cap * 2 : 16;
        order = realloc(order, order_cap * sizeof *order);
    }
    order[order_count++] = m;
}

struct module *module_find(const char *name)
{
    char norm[MODULE_NAME_MAX];
    module_normalize(name, norm);
    for (struct module *m = head; m; m = m->next)
        if (!strcmp(m->name, norm))
            return m;
    return NULL;
}

struct module *module_at(const void *addr)
{
    const uint8_t *p = addr;
    for (struct module *m = head; m; m = m->next)
        if (p >= m->base && p < m->base + m->size)
            return m;
    return NULL;
}

void *module_dir(struct module *m, int index, uint32_t *size)
{
    if (!m->nt || index >= (int)m->nt->opt.dir_count)
        return NULL;
    struct pe_dir *d = &m->nt->opt.dirs[index];
    if (!d->rva)
        return NULL;
    if (size)
        *size = d->size;
    return m->base + d->rva;
}

static struct module *load_bit(const struct bit *b, const char *norm)
{
    struct module *m = calloc(1, sizeof *m);
    strcpy(m->name, norm);
    m->bit = b;
    m->size = 4096;
    m->base = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    m->base[0] = 'M';
    m->base[1] = 'Z';
    mprotect(m->base, m->size, PROT_READ);
    m->is_dll = true;
    m->attached = true;
    link(m);
    trace("bit %s ready (%zu functions)", norm, b->count);
    return m;
}

static bool find_in(const char *dir, const char *norm, char out[PATH_MAX])
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)))
        if (!strcasecmp(e->d_name, norm)) {
            snprintf(out, PATH_MAX, "%s/%s", dir, e->d_name);
            found = true;
        }
    closedir(d);
    return found;
}

static bool search(const char *norm, char out[PATH_MAX])
{
    if (main_module) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", main_module->path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            if (find_in(dir, norm, out))
                return true;
        }
    }
    const char *extra = getenv("SCREW_DLL_PATH");
    if (extra) {
        char *copy = strdup(extra), *save, *tok;
        bool found = false;
        for (tok = strtok_r(copy, ":", &save); tok && !found; tok = strtok_r(NULL, ":", &save))
            found = find_in(tok, norm, out);
        free(copy);
        if (found)
            return true;
    }
    return find_in(".", norm, out);
}

static struct module *link_image(struct module *m)
{
    link(m);
    imports_resolve(m);
    image_protect(m);
    tls_register(m);
    ordered(m);
    return m;
}

struct module *module_load(const char *name)
{
    char norm[MODULE_NAME_MAX];
    module_normalize(name, norm);
    struct module *m = module_find(norm);
    if (m)
        return m;

    char path[PATH_MAX];
    if (search(norm, path)) {
        const struct bit *b = bit_find(norm);
        if (!b || getenv("SCREW_NATIVE_FIRST")) {
            m = image_map(path);
            if (m)
                return link_image(m);
        }
    }
    const struct bit *b = bit_find(norm);
    return b ? load_bit(b, norm) : NULL;
}

struct module *module_load_main(const char *host_path)
{
    struct module *m = image_map(host_path);
    if (!m)
        die("cannot open %s", host_path);
    main_module = m;
    return link_image(m);
}

void *module_symbol(struct module *m, const char *name)
{
    return m->bit ? bit_symbol(m->bit, name) : exports_lookup(m, name);
}

void *module_ordinal(struct module *m, uint32_t ordinal)
{
    return m->bit ? NULL : exports_ordinal(m, ordinal);
}

static void notify(struct module *m, uint32_t reason)
{
    tls_callbacks(m, reason);
    if (m->is_dll && m->nt->opt.entry) {
        dll_entry entry = (dll_entry)(m->base + m->nt->opt.entry);
        if (!entry(m->base, reason, NULL) && reason == DLL_PROCESS_ATTACH)
            die("%s refused to initialize", m->name);
    }
}

void modules_attach(void)
{
    for (size_t i = 0; i < order_count; i++) {
        if (order[i]->attached)
            continue;
        order[i]->attached = true;
        trace("attach %s", order[i]->name);
        notify(order[i], DLL_PROCESS_ATTACH);
    }
}

void modules_thread_event(uint32_t reason)
{
    for (size_t i = 0; i < order_count; i++)
        if (order[i]->attached)
            notify(order[i], reason);
}
