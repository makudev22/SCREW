#include <screw/loader.h>
#include <screw/log.h>
#include <screw/teb.h>

#include <stdlib.h>
#include <string.h>

typedef void WINAPI (*tls_callback)(void *base, DWORD reason, void *reserved);

struct tls_image {
    uint32_t index;
    const uint8_t *data;
    size_t data_size;
    size_t zero_size;
    tls_callback *callbacks;
};

static struct tls_image *images[TLS_IMPLICIT_MAX];
static uint32_t image_count;

static void *instantiate(const struct tls_image *img)
{
    uint8_t *block = calloc(1, img->data_size + img->zero_size + 16);
    memcpy(block, img->data, img->data_size);
    return block;
}

void tls_register(struct module *m)
{
    struct pe_tls *dir = module_dir(m, PE_DIR_TLS, NULL);
    if (!dir)
        return;
    if (image_count == TLS_IMPLICIT_MAX)
        die("too many modules with thread-local data");

    struct tls_image *img = calloc(1, sizeof *img);
    img->index = image_count;
    img->data = (const uint8_t *)dir->raw_start;
    img->data_size = dir->raw_end - dir->raw_start;
    img->zero_size = dir->zero_fill;
    img->callbacks = (tls_callback *)dir->callbacks;
    if (dir->index_address)
        *(uint32_t *)dir->index_address = img->index;

    images[image_count++] = img;
    m->tls = img;

    struct teb *t = teb_current();
    if (t)
        t->implicit_tls[img->index] = instantiate(img);
    trace("tls slot %u for %s (%zu bytes)", img->index, m->name, img->data_size + img->zero_size);
}

void tls_thread_init(void)
{
    struct teb *t = teb_current();
    for (uint32_t i = 0; i < image_count; i++)
        t->implicit_tls[i] = instantiate(images[i]);
}

void tls_callbacks(struct module *m, uint32_t reason)
{
    if (!m->tls || !m->tls->callbacks)
        return;
    for (tls_callback *cb = m->tls->callbacks; *cb; cb++)
        (*cb)(m->base, reason, NULL);
}
