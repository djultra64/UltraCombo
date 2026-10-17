// IDE/ATA hard disk backed by a raw image file (memory mapped, writes persist).
#include "ide.h"
#include "cpu.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DSC  0x10
#define ST_DRDY 0x40
#define ST_BSY  0x80

static struct {
    uint8_t *img;
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
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); return false; }
    struct stat st;
    fstat(fd, &st);
    d.size = (uint64_t)st.st_size;
    d.img = mmap(0, d.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (d.img == MAP_FAILED) { perror("mmap"); return false; }

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
    memcpy(d.buf, d.img + (uint64_t)d.lba * 512, 512);
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
        if (d.lba < d.total) memcpy(d.img + (uint64_t)d.lba * 512, d.buf, 512);
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
