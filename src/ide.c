// IDE/ATA hard disk backed by a raw image file. The first sectors (the
// game's settings and audits) are kept in memory so save states cover them,
// and written back by ide_flush(); everything else is read through a cache.
#include "ide.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DSC  0x10
#define ST_DRDY 0x40
#define ST_BSY  0x80

static FILE *disk;
static uint8_t tracked[IDE_TRACK_BYTES];   // sectors 0-63, the authoritative copy
static bool tracked_unsaved;

#define CACHE_BYTES (64 * 1024)
static uint8_t cache[CACHE_BYTES];
static uint64_t cache_at = ~0ull;

static struct {
    uint8_t *img;                          // unused, kept for the save state layout
    uint64_t size;
    uint32_t cyls, heads, secs, total;

    uint8_t features, error, count, sector, cyl_lo, cyl_hi, drvhead, status, devctl;
    uint8_t cmd;
    uint16_t buf[256];
    int buf_pos;          // words transferred within buf
    bool reading, writing;
    uint32_t lba;         // current transfer LBA
    int remaining;        // sectors remaining in transfer
    int multiple;         // sectors per block for READ/WRITE MULTIPLE
    int64_t irq_delay;    // cycles until IRQ asserts (<0: none pending)
    bool irq;
    uint16_t identify[256];
} d;

uint8_t ide_dirty[IDE_TRACK_PAGES];
static __typeof__(d) d_power_on;     // controller state right after ide_open

void ide_power_on(void)
{
    uint64_t size = d.size;
    d = d_power_on;
    d.size = size;
}

// Cheap identity of the disk image: its size plus 4 KB from every 4 MB.
uint64_t ide_image_id(void)
{
    uint64_t h = 0xcbf29ce484222325ull ^ d.size;
    static uint8_t buf[4096];
    for (uint64_t off = 0; off + sizeof buf <= d.size; off += 4u << 20) {
        if (off < IDE_TRACK_BYTES) continue;              // settings: synced separately
        if (fseek64(disk, (long long)off, SEEK_SET) || fread(buf, 1, sizeof buf, disk) != sizeof buf) break;
        for (size_t i = 0; i < sizeof buf; i++) { h ^= buf[i]; h *= 0x100000001b3ull; }
    }
    cache_at = ~0ull;
    return h;
}

uint8_t *ide_tracked(void) { return tracked; }

static void read_sector(uint32_t lba, void *dst)
{
    uint64_t off = (uint64_t)lba * 512;
    if (off < IDE_TRACK_BYTES) { memcpy(dst, tracked + off, 512); return; }
    uint64_t blk = off & ~(uint64_t)(CACHE_BYTES - 1);
    if (blk != cache_at) {
        memset(cache, 0, CACHE_BYTES);
        if (fseek64(disk, (long long)blk, SEEK_SET) == 0 && fread(cache, 1, CACHE_BYTES, disk) > 0) {}
        cache_at = blk;
    }
    memcpy(dst, cache + (off - blk), 512);
}

static void write_sector(uint32_t lba, const void *src)
{
    uint64_t off = (uint64_t)lba * 512;
    if (off < IDE_TRACK_BYTES) {
        memcpy(tracked + off, src, 512);
        ide_dirty[off >> 12] = 1;
        tracked_unsaved = true;
        return;
    }
    static bool warned;
    if (!warned) fprintf(stderr, "ide: write to sector %u is not covered by save states\n", lba);
    warned = true;
    if (off >> 16 == cache_at >> 16) cache_at = ~0ull;
    if (fseek64(disk, (long long)off, SEEK_SET) == 0) fwrite(src, 1, 512, disk);
    fflush(disk);
}

void ide_flush(void)
{
    if (!disk || !tracked_unsaved) return;
    tracked_unsaved = false;
    if (fseek64(disk, 0, SEEK_SET) == 0) fwrite(tracked, 1, IDE_TRACK_BYTES, disk);
    fflush(disk);
}

void ide_tracked_changed(void) { tracked_unsaved = true; }

size_t ide_state(uint8_t *buf, bool load)
{
    if (buf && load) {
        uint64_t size = d.size;
        memcpy(&d, buf, sizeof d);
        d.img = NULL; d.size = size;
    } else if (buf) {
        memcpy(buf, &d, sizeof d);
        memset(buf + offsetof(__typeof__(d), img), 0, sizeof d.img);    // keep states pointer-free
    }
    return sizeof d;
}

static void set_irq(bool s)
{
    d.irq = s;
    cpu_set_irq(1, s && !(d.devctl & 2));
}

static void ident_str(int word, int nwords, const char *s)
{
    int len = (int)strlen(s);
    for (int i = 0; i < nwords; i++) {
        char a = (2 * i < len) ? s[2 * i] : ' ';
        char b = (2 * i + 1 < len) ? s[2 * i + 1] : ' ';
        d.identify[word + i] = (uint16_t)((uint8_t)a << 8 | (uint8_t)b);
    }
}

bool ide_open(const char *path, uint32_t cyls, uint32_t heads, uint32_t secs, int serial_offset)
{
    disk = fopen(path, "r+b");
    if (!disk) { perror(path); return false; }
    fseek64(disk, 0, SEEK_END);
    d.size = (uint64_t)ftell64(disk);
    fseek64(disk, 0, SEEK_SET);
    if (fread(tracked, 1, IDE_TRACK_BYTES, disk) != IDE_TRACK_BYTES) { fprintf(stderr, "%s: too small\n", path); return false; }
    atexit(ide_flush);

    d.cyls = cyls; d.heads = heads; d.secs = secs;
    d.total = (uint32_t)(d.size / 512);

    memset(d.identify, 0, sizeof(d.identify));
    d.identify[0] = 0x045a;
    d.identify[1] = (uint16_t)cyls;
    d.identify[3] = (uint16_t)heads;
    d.identify[4] = (uint16_t)(512 * secs);
    d.identify[5] = 512;
    d.identify[6] = (uint16_t)secs;
    ident_str(10, 10, "");
    ident_str(23, 4, "1.0");
    ident_str(27, 20, "");
    d.identify[47] = 0x8010;
    d.identify[49] = 0x0200;
    d.identify[51] = 0x0200;
    d.identify[53] = 0x0001;
    d.identify[54] = (uint16_t)cyls;
    d.identify[55] = (uint16_t)heads;
    d.identify[56] = (uint16_t)secs;
    uint32_t chs = cyls * heads * secs;
    d.identify[57] = (uint16_t)chs;
    d.identify[58] = (uint16_t)(chs >> 16);
    d.identify[60] = (uint16_t)d.total;
    d.identify[61] = (uint16_t)(d.total >> 16);

    // The game checks the drive model; mirror what the original hardware reports.
    for (int i = 10; i < 20; i++) d.identify[i] = ('0' << 8) | '0';
    for (int i = 27; i < 47; i++) d.identify[i] = (' ' << 8) | ' ';
    ident_str(serial_offset, 4, "ST9150AG");

    d.status = ST_DRDY | ST_DSC;
    d.count = 1; d.sector = 1;
    d.irq_delay = -1;
    d_power_on = d;
    return true;
}

static uint32_t cur_lba(void)
{
    if (d.drvhead & 0x40)
        return ((uint32_t)(d.drvhead & 0x0f) << 24) | ((uint32_t)d.cyl_hi << 16) | ((uint32_t)d.cyl_lo << 8) | d.sector;
    uint32_t cyl = ((uint32_t)d.cyl_hi << 8) | d.cyl_lo;
    uint32_t head = d.drvhead & 0x0f;
    return (cyl * d.heads + head) * d.secs + (d.sector - 1);
}

static void set_lba_regs(uint32_t lba)
{
    if (d.drvhead & 0x40) {
        d.sector = (uint8_t)lba; d.cyl_lo = (uint8_t)(lba >> 8); d.cyl_hi = (uint8_t)(lba >> 16);
        d.drvhead = (d.drvhead & 0xf0) | ((lba >> 24) & 0x0f);
    } else {
        uint32_t cyl = lba / (d.heads * d.secs);
        uint32_t rem = lba % (d.heads * d.secs);
        d.cyl_lo = (uint8_t)cyl; d.cyl_hi = (uint8_t)(cyl >> 8);
        d.drvhead = (d.drvhead & 0xf0) | (uint8_t)(rem / d.secs);
        d.sector = (uint8_t)(rem % d.secs + 1);
    }
}

static void schedule_irq(void) { d.irq_delay = 200; }

static void fill_read_buf(void)
{
    if (d.lba >= d.total) {
        d.status = ST_DRDY | ST_ERR; d.error = 0x10; // IDNF
        d.reading = false;
        schedule_irq();
        return;
    }
    read_sector(d.lba, d.buf);
    d.buf_pos = 0;
    d.status = ST_DRDY | ST_DSC | ST_DRQ;
    schedule_irq();
}

static void command(uint8_t c)
{
    static int log = -1;
    if (log < 0) log = getenv("KI2_LOG_IDE") != NULL;
    if (log) fprintf(stderr, "ide: cmd %02x count=%d sec=%d cyl=%d dh=%02x lba=%u\n", c, d.count, d.sector,
                     d.cyl_hi << 8 | d.cyl_lo, d.drvhead, cur_lba());
    d.cmd = c;
    d.error = 0;
    set_irq(false);
    d.reading = d.writing = false;
    // only the master drive exists
    if (d.drvhead & 0x10) { d.status = 0; return; }
    switch (c) {
    case 0xec: // IDENTIFY DEVICE
        memcpy(d.buf, d.identify, 512);
        d.buf_pos = 0; d.remaining = 1; d.reading = true; d.lba = ~0u;
        d.status = ST_DRDY | ST_DSC | ST_DRQ;
        schedule_irq();
        return;
    case 0x20: case 0x21: case 0xc4: // READ SECTORS / READ MULTIPLE
        d.lba = cur_lba();
        d.remaining = d.count ? d.count : 256;
        d.reading = true;
        fill_read_buf();
        return;
    case 0x30: case 0x31: case 0xc5: // WRITE SECTORS / WRITE MULTIPLE
        d.lba = cur_lba();
        d.remaining = d.count ? d.count : 256;
        d.writing = true;
        d.buf_pos = 0;
        d.status = ST_DRDY | ST_DSC | ST_DRQ;
        return;
    case 0xc6: d.multiple = d.count; break;          // SET MULTIPLE MODE
    case 0x91:                                       // INITIALIZE DEVICE PARAMETERS
        if (d.count) { d.secs = d.count; d.heads = (d.drvhead & 0x0f) + 1; }
        break;
    case 0xef: break;                                // SET FEATURES
    case 0x40: case 0x41: break;                     // READ VERIFY
    case 0x70: break;                                // SEEK
    case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe5: case 0xe6: break; // power mgmt
    default:
        if ((c & 0xf0) == 0x10) break;               // RECALIBRATE
        fprintf(stderr, "ide: unknown command %02x\n", c);
        d.status = ST_DRDY | ST_ERR; d.error = 0x04;
        schedule_irq();
        return;
    }
    d.status = ST_DRDY | ST_DSC;
    schedule_irq();
}

static uint16_t data_read(void)
{
    if (!d.reading || !(d.status & ST_DRQ)) return 0xffff;
    uint16_t v = d.buf[d.buf_pos++];
    if (d.buf_pos >= 256) {
        d.status &= ~ST_DRQ;
        if (d.lba != ~0u) {
            d.lba++;
            set_lba_regs(d.lba);
            d.count--;
        }
        if (--d.remaining > 0) fill_read_buf();
        else { d.reading = false; d.status = ST_DRDY | ST_DSC; }
    }
    return v;
}

static void data_write(uint16_t v)
{
    if (!d.writing || !(d.status & ST_DRQ)) return;
    d.buf[d.buf_pos++] = v;
    if (d.buf_pos >= 256) {
        if (d.lba < d.total) write_sector(d.lba, d.buf);
        d.lba++;
        set_lba_regs(d.lba);
        d.count--;
        d.buf_pos = 0;
        if (--d.remaining > 0) d.status = ST_DRDY | ST_DSC | ST_DRQ;
        else { d.writing = false; d.status = ST_DRDY | ST_DSC; }
        schedule_irq();
    }
}

uint32_t ide_read(int reg)
{
    switch (reg) {
    case 0: return data_read();
    case 1: return d.error;
    case 2: return d.count;
    case 3: return d.sector;
    case 4: return d.cyl_lo;
    case 5: return d.cyl_hi;
    case 6: return d.drvhead | 0xa0;
    case 7:
        set_irq(false);
        if (d.drvhead & 0x10) return 0;
        return d.status;
    }
    return 0xff;
}

void ide_write(int reg, uint32_t v)
{
    switch (reg) {
    case 0: data_write((uint16_t)v); break;
    case 1: d.features = (uint8_t)v; break;
    case 2: d.count = (uint8_t)v; break;
    case 3: d.sector = (uint8_t)v; break;
    case 4: d.cyl_lo = (uint8_t)v; break;
    case 5: d.cyl_hi = (uint8_t)v; break;
    case 6: d.drvhead = (uint8_t)v; break;
    case 7: command((uint8_t)v); break;
    }
}

uint32_t ide_alt_status(void) { return (d.drvhead & 0x10) ? 0 : d.status; }

void ide_devctl(uint32_t v)
{
    uint8_t prev = d.devctl;
    d.devctl = (uint8_t)v;
    if ((v & 4) && !(prev & 4)) { // software reset
        d.reading = d.writing = false;
        d.status = ST_DRDY | ST_DSC;
        d.error = 1; d.count = 1; d.sector = 1; d.cyl_lo = d.cyl_hi = 0; d.drvhead = 0;
    }
    set_irq(d.irq);
}

void ide_tick(int cycles)
{
    if (d.irq_delay >= 0) {
        d.irq_delay -= cycles;
        if (d.irq_delay < 0) set_irq(true);
    }
}
