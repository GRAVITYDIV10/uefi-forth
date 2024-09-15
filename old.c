/*
 * reference :
 * https://github.com/zevv/zForth
 * https://muforth.dev/threaded-code/
 * https://muforth.dev/threaded-code-literals-ifs-and-loops/
 * http://www.bradrodriguez.com/papers/moving1.htm
 * http://www.bradrodriguez.com/papers/moving3.htm
 * http://www.bradrodriguez.com/papers/tcjassem.txt
 */

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <efi.h>
#include <efilib.h>

typedef struct forth_regs forth_regs;
typedef struct forth_word forth_word;
typedef struct forth_ctx forth_ctx;

struct __attribute__((packed)) forth_regs {
	uintptr_t ss;
	uintptr_t ip;
	uintptr_t wp;
	uintptr_t xp;

	uintptr_t sp;
	uintptr_t rp;
	uintptr_t tp;

	uintptr_t sb;
	uintptr_t rb;
	uintptr_t tb;

	uintptr_t st;
	uintptr_t rt;
	uintptr_t tt;
};

struct __attribute__((packed)) forth_word {
	char *name;
	uintptr_t attr;
	forth_word *link;
	void (*entry)(forth_ctx *ctx);
	void *body;
};

struct __attribute__((packed)) forth_ctx {
	forth_regs *regs;
	forth_regs *next;
	uint8_t *here;
	uint8_t *latest;
};

size_t mbstowcs(wchar_t *__restrict pwcs, const char *__restrict s, size_t n)
{
	int count = 0;

	if (n != 0) {
		do {
			if ((*pwcs++ = (wchar_t)*s++) == 0)
				break;
			count++;
		} while (--n != 0);
	}

	return count;
}

EFI_SYSTEM_TABLE *GST = NULL;
EFI_HANDLE *GIH = NULL;
EFI_BOOT_SERVICES *GBS = NULL;
EFI_GRAPHICS_OUTPUT_PROTOCOL *GGOP = NULL;

void efi_clean_screen(void)
{
	GST->ConOut->ClearScreen(GST->ConOut);
}

void efi_cursor_visible(int en)
{
	GST->ConOut->EnableCursor(GST->ConOut, en);
}

void efi_puts16(wchar_t *msg)
{
	GST->ConOut->OutputString(GST->ConOut, (CHAR16 *)msg);
}

typedef struct {
	uint8_t *data;
	uint32_t capacity;
	uint32_t head;
	uint32_t num;
} Fifo8;

void fifo8_reset(Fifo8 *fifo)
{
	fifo->num = 0;
	fifo->head = 0;
}

void fifo8_push(Fifo8 *fifo, uint8_t data)
{
	if (fifo->num >= fifo->capacity) {
		return;
	}
	fifo->data[(fifo->head + fifo->num) % fifo->capacity] = data;
	fifo->num++;
}

uint8_t fifo8_pop(Fifo8 *fifo)
{
	uint8_t ret;

	if (fifo->num <= 0) {
		return 0xAA;
	}
	ret = fifo->data[fifo->head++];
	fifo->head %= fifo->capacity;
	fifo->num--;
	return ret;
}

bool fifo8_is_empty(Fifo8 *fifo)
{
	return (fifo->num == 0);
}

bool fifo8_is_full(Fifo8 *fifo)
{
	return (fifo->num == fifo->capacity);
}

uint32_t fifo8_num_free(Fifo8 *fifo)
{
	return fifo->capacity - fifo->num;
}

uint32_t fifo8_num_used(Fifo8 *fifo)
{
	return fifo->num;
}

Fifo8 efi_console_rxfifo;

int efi_tstc(void)
{
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	if (fifo8_num_free(&efi_console_rxfifo)) {
		if ((Status = GST->ConIn->ReadKeyStroke(
			     GST->ConIn, &Key)) == EFI_SUCCESS) {
			fifo8_push(&efi_console_rxfifo, (char)Key.UnicodeChar);
		}
	}
	return fifo8_num_used(&efi_console_rxfifo);
}

char efi_getc(void)
{
	while (efi_tstc() == 0)
		;
	return fifo8_pop(&efi_console_rxfifo);
}

void efi_puts(char *msg)
{
	wchar_t dst[512];
	mbstowcs(dst, msg, 512 - 1);
	efi_puts16(dst);
}

void efi_putc(char c)
{
	char s[2];
	s[0] = c;
	s[1] = '\0';
	efi_puts(s);
}

void efi_puthex(uintptr_t n)
{
	int ls = 0;
	int lsm = sizeof(uintptr_t) * 8;
	uintptr_t tmp;
	while (ls < lsm) {
		tmp = (n >> (lsm - ls - 4)) & 0xF;
		if (tmp > 9) {
			tmp -= 10;
			tmp += 'A';
		} else {
			tmp += '0';
		}
		efi_putc((char)tmp);
		ls += 4;
	}
}

UINTN GGOP_mode_nums = 0;
UINTN GGOP_mode_current = 0;

int efi_gop_init(void) {
	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_STATUS Status;

	Status = GBS->LocateProtocol(&gopGuid, NULL, (void**)&GGOP);
	if (Status != EFI_SUCCESS) {
		efi_puts("GOP: not found\n\r");
		GGOP = NULL;
		return 0;
	}
	efi_puts("GOP: found\n\r");

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *GGOP_mode_info = NULL;
	UINTN GGOP_mode_info_size = 0;

	Status = GGOP->QueryMode(GGOP,
			GGOP->Mode == NULL ? 0 : GGOP->Mode->Mode,
			&GGOP_mode_info_size, &GGOP_mode_info);
	// this is needed to get the current video mode
	if (Status == EFI_NOT_STARTED) {
		Status = GGOP->SetMode(GGOP, 0);
	}
	if(Status != EFI_SUCCESS) {
		efi_puts("GOP: can't get current mode\n\r");
		GGOP = NULL;
		return 0;
	} else {
		GGOP_mode_current = GGOP->Mode->Mode;
		GGOP_mode_nums = GGOP->Mode->MaxMode;
	}

	efi_puts("GOP: mode ");
	efi_puthex(GGOP_mode_current);
	efi_puts(" width ");
	efi_puthex(GGOP_mode_info->HorizontalResolution);
	efi_puts(" height ");
	efi_puthex(GGOP_mode_info->VerticalResolution);
	efi_puts(" pixelsperline ");
	efi_puthex(GGOP_mode_info->PixelsPerScanLine);
	efi_puts(" format ");
	efi_puthex(GGOP_mode_info->PixelFormat);
	efi_puts("\n\r");

	efi_puts("GOP: fbaddr ");
	efi_puthex(GGOP->Mode->FrameBufferBase);
	efi_puts(" fbsize ");
	efi_puthex(GGOP->Mode->FrameBufferSize);
	efi_puts("\n\r");

	if (GGOP != NULL) {
		return 1;
	}
	return 0;
}

// TODO get framebuffer bpp info

void efi_gop_draw_pixel(uint16_t x, uint16_t y, uint32_t color) {
	if (GGOP == NULL) {
		return;
	}
	if (x >= GGOP->Mode->Info->HorizontalResolution) {
		return;
	}
	if (y >= GGOP->Mode->Info->VerticalResolution) {
		return;
	}
	int bpp = 32;
	if (bpp == 32) {
		uint32_t *p;
		p = (uint32_t *)(GGOP->Mode->FrameBufferBase + 4 *
			GGOP->Mode->Info->PixelsPerScanLine * y +
			4 * x);
		*p = color;
	}
}

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

void efi_gop_drawxline(uint16_t x1, uint16_t x2, uint16_t y, uint32_t color) {
	if (GGOP == NULL) {
		return;
	}
	uint16_t minx;
	uint16_t maxx;
	minx = MIN(x1, x2);
	maxx = MAX(x1, x2);
	while(minx <= maxx) {
		efi_gop_draw_pixel(minx, y, color);
		minx++;
	}
}

void efi_gop_drawyline(uint16_t y1, uint16_t y2, uint16_t x, uint32_t color) {
	if (GGOP == NULL) {
		return;
	}
	uint16_t miny;
	uint16_t maxy;
	miny = MIN(y1, y2);
	maxy = MAX(y1, y2);
	while(miny <= maxy) {
		efi_gop_draw_pixel(x, miny, color);
		miny++;
	}
}

void efi_gop_drawrect(uint16_t x1, uint16_t x2,
		uint16_t y1, uint16_t y2,
		uint32_t color) {
	if (GGOP == NULL) {
		return;
	}
	efi_gop_drawxline(x1, x2, y1, color);
	efi_gop_drawxline(x1, x2, y2, color);
	efi_gop_drawyline(y1, y2, x1, color);
	efi_gop_drawyline(y1, y2, x2, color);
}

void efi_gop_drawsolid(uint16_t x1, uint16_t x2,
		uint16_t y1, uint16_t y2,
		uint32_t color) {
	if (GGOP == NULL) {
		return;
	}
	uint16_t miny, maxy;
	miny = MIN(y1, y2);
	maxy = MAX(y1, y2);
	while(miny <= maxy) {
		efi_gop_drawxline(x1, x2, miny, color);
		miny++;
	}
}



#define FORTH_SS(ctx) (ctx)->regs->ss

#define FORTH_SP(ctx) (ctx)->regs->sp
#define FORTH_RP(ctx) (ctx)->regs->rp
#define FORTH_TP(ctx) (ctx)->regs->tp

#define FORTH_SB(ctx) (ctx)->regs->sb
#define FORTH_RB(ctx) (ctx)->regs->rb
#define FORTH_TB(ctx) (ctx)->regs->tb

#define FORTH_ST(ctx) (ctx)->regs->st
#define FORTH_RT(ctx) (ctx)->regs->rt
#define FORTH_TT(ctx) (ctx)->regs->tt

#define FORTH_IP(ctx) (ctx)->regs->ip
#define FORTH_WP(ctx) (ctx)->regs->wp
#define FORTH_XP(ctx) (ctx)->regs->xp

#define FORTH_UP(ctx) (ctx)->regs

#define FORTH_HERE(ctx) (ctx)->here
#define FORTH_LATEST(ctx) (ctx)->latest

enum {
	FORTH_SS_DEBUG = (1 << 0),
	FORTH_SS_COMPERR = (1 << 1),
	FORTH_SS_COMPING = (1 << 2),
	FORTH_SS_ROVERFLOW = (1 << 3),
	FORTH_SS_RUNDERFLOW = (1 << 4),
	FORTH_SS_DOVERFLOW = (1 << 5),
	FORTH_SS_DUNDERFLOW = (1 << 6),
	FORTH_SS_WORDNF = (1 << 7),
	FORTH_SS_DIVZERO = (1 << 8),
};

#define FORTH_DEBUG_ENABLE(ctx) (FORTH_SS(ctx) & FORTH_SS_DEBUG)
#define FORTH_COMPING_ENABLE(ctx) (FORTH_SS(ctx) & FORTH_SS_COMPING)

#define FORTH_ADD_WORD(name, attr, link, entry, body) \
	const char _str_##name[] = #name;             \
	const forth_word name = {                     \
		_str_##name, attr, link, entry, body, \
	}

enum {
	FORTH_ATTR_IMMEDIATE = (1 << 0),
};

#define FORTH_WORD_IS_IMMEDIATE(word) (word->attr & (FORTH_ATTR_IMMEDIATE))

void forth_abort(forth_ctx *ctx);

int forth_run = 1;

void forth_bye(forth_ctx *ctx)
{
	(void)ctx;
	forth_run = 0;
}

FORTH_ADD_WORD(bye, 0, NULL, forth_bye, NULL);

#define REG_FMT "%016llX"

void forth_reg_dump(forth_ctx *ctx)
{
	efi_puts("REG DUMP:\n\r");
	efi_puts("SS: ");
	efi_puthex(FORTH_SS(ctx));
	efi_puts(" ");
	efi_puts("IP: ");
	efi_puthex(FORTH_IP(ctx));
	efi_puts(" ");
	efi_puts("UP: ");
	efi_puthex((uintptr_t)FORTH_UP(ctx));
	efi_puts(" ");
	efi_puts("\n\r");
	efi_puts("SP: ");
	efi_puthex(FORTH_SP(ctx));
	efi_puts(" ");
	efi_puts("RP: ");
	efi_puthex(FORTH_RP(ctx));
	efi_puts(" ");
	efi_puts("TP: ");
	efi_puthex(FORTH_TP(ctx));
	efi_puts(" ");
	efi_puts("\n\r");
	efi_puts("SB: ");
	efi_puthex(FORTH_SB(ctx));
	efi_puts(" ");
	efi_puts("RB: ");
	efi_puthex(FORTH_RB(ctx));
	efi_puts(" ");
	efi_puts("TB: ");
	efi_puthex(FORTH_TB(ctx));
	efi_puts(" ");
	efi_puts("\n\r");
	efi_puts("ST: ");
	efi_puthex(FORTH_ST(ctx));
	efi_puts(" ");
	efi_puts("RT: ");
	efi_puthex(FORTH_RT(ctx));
	efi_puts(" ");
	efi_puts("TT: ");
	efi_puthex(FORTH_TT(ctx));
	efi_puts(" ");
	efi_puts("\n\r");
}

void forth_ds_dump(forth_ctx *ctx)
{
	efi_puts("(");
	efi_puthex(((FORTH_SP(ctx) - FORTH_SB(ctx)) / sizeof(uintptr_t)));
	efi_puts(") ");
	efi_puts("DSTACK DUMP: ");
	uintptr_t p;
	p = FORTH_SB(ctx);
	while (p < FORTH_SP(ctx)) {
		efi_puthex(*(uintptr_t *)p);
		efi_puts(" ");
		p += sizeof(uintptr_t);
	}
	efi_puts("\n\r");
}

void forth_rs_dump(forth_ctx *ctx)
{
	efi_puts("(");
	efi_puthex(((FORTH_RP(ctx) - FORTH_RB(ctx)) / sizeof(uintptr_t)));
	efi_puts(") ");
	efi_puts("RSTACK DUMP: ");
	uintptr_t p;
	p = FORTH_RB(ctx);
	while (p < FORTH_RP(ctx)) {
		efi_puthex(*(uintptr_t *)p);
		efi_puts(" ");
		p += sizeof(uintptr_t);
	}
	efi_puts("\n\r");
}

void forth_word_dump(forth_word *word)
{
	efi_puts("WORD DUMP: ");
	efi_puts(word->name);
	efi_puts("\n\r");
	efi_puts("ATTR: ");
	efi_puthex(word->attr);
	efi_puts(" ");
	efi_puts("LINK: ");
	efi_puthex((uintptr_t)word->link);
	efi_puts(" ");
	efi_puts("ENTRY: ");
	efi_puthex((uintptr_t)word->entry);
	efi_puts(" ");
	efi_puts("BODY: ");
	efi_puthex((uintptr_t)word->body);
	efi_puts("\n\r");
}

void forth_next(forth_ctx *ctx)
{
	if (FORTH_DEBUG_ENABLE(ctx)) {
		forth_reg_dump(ctx);
		forth_ds_dump(ctx);
		forth_rs_dump(ctx);
	}
	forth_word *word;
	FORTH_WP(ctx) = *(uintptr_t *)FORTH_IP(ctx);
	FORTH_IP(ctx) += sizeof(uintptr_t);
	word = (forth_word *)FORTH_WP(ctx);
	if (FORTH_DEBUG_ENABLE(ctx)) {
		forth_word_dump(word);
	}
	return word->entry(ctx);
}

FORTH_ADD_WORD(next, 0, &bye, forth_next, NULL);

void forth_rpush(forth_ctx *ctx, uintptr_t val)
{
	*(uintptr_t *)FORTH_RP(ctx) = val;
	FORTH_RP(ctx) += sizeof(uintptr_t);
	if (FORTH_RP(ctx) > FORTH_RT(ctx)) {
		FORTH_SS(ctx) |= FORTH_SS_ROVERFLOW;
		forth_abort(ctx);
		return;
	}
}

uintptr_t forth_rpop(forth_ctx *ctx)
{
	FORTH_RP(ctx) -= sizeof(uintptr_t);
	if (FORTH_RP(ctx) < FORTH_RB(ctx)) {
		FORTH_SS(ctx) |= FORTH_SS_RUNDERFLOW;
		forth_abort(ctx);
		return 0xBADBAD;
	}
	return *(uintptr_t *)FORTH_RP(ctx);
}

void forth_push(forth_ctx *ctx, uintptr_t val)
{
	*(uintptr_t *)FORTH_SP(ctx) = val;
	FORTH_SP(ctx) += sizeof(uintptr_t);
	if (FORTH_SP(ctx) > FORTH_ST(ctx)) {
		FORTH_SS(ctx) |= FORTH_SS_DOVERFLOW;
		forth_abort(ctx);
		return;
	}
}

uintptr_t forth_pop(forth_ctx *ctx)
{
	FORTH_SP(ctx) -= sizeof(uintptr_t);
	if (FORTH_SP(ctx) < FORTH_SB(ctx)) {
		FORTH_SS(ctx) |= FORTH_SS_DUNDERFLOW;
		forth_abort(ctx);
		return 0xBADBAD;
	}
	return *(uintptr_t *)FORTH_SP(ctx);
}

void forth_nest(forth_ctx *ctx)
{
	forth_rpush(ctx, FORTH_IP(ctx));
	forth_word *cur;
	cur = (forth_word *)FORTH_WP(ctx);
	FORTH_IP(ctx) = (uintptr_t)cur->body;
	forth_next(ctx);
}

FORTH_ADD_WORD(nest, 0, &next, forth_nest, NULL);

void forth_unnest(forth_ctx *ctx)
{
	FORTH_IP(ctx) = forth_rpop(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(unnest, 0, &next, forth_unnest, NULL);

// a simple word use for test nest & unnest

const forth_word *nop_ws[] = {
	&unnest,
};

FORTH_ADD_WORD(nop, 0, &unnest, forth_nest, &nop_ws[0]);

void forth_lit(forth_ctx *ctx)
{
	forth_push(ctx, *(uintptr_t *)FORTH_IP(ctx));
	FORTH_IP(ctx) += sizeof(uintptr_t);
	forth_next(ctx);
}

FORTH_ADD_WORD(lit, 0, &nop, forth_lit, NULL);

void forth_drop(forth_ctx *ctx)
{
	forth_pop(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(drop, 0, &lit, forth_drop, NULL);

void forth_branch(forth_ctx *ctx)
{
	FORTH_WP(ctx) = *(uintptr_t *)FORTH_IP(ctx);
	FORTH_IP(ctx) = FORTH_WP(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(branch, 0, &drop, forth_branch, NULL);

void forth_branch0(forth_ctx *ctx)
{
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_XP(ctx) == 0) {
		FORTH_WP(ctx) = *(uintptr_t *)FORTH_IP(ctx);
		FORTH_IP(ctx) = FORTH_WP(ctx);
	} else {
		FORTH_IP(ctx) += sizeof(uintptr_t);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(branch0, 0, &branch, forth_branch0, NULL);

void *memchr(const void *src_void, int c, size_t length)
{
	const unsigned char *src = (const unsigned char *)src_void;
	unsigned char d = c;
	while (length--) {
		if (*src == d)
			return (void *)src;
		src++;
	}

	return NULL;
}

char *strchr(const char *s1, int i)
{
	const unsigned char *s = (const unsigned char *)s1;
	unsigned char c = i;

	while (*s && *s != c)
		s++;
	if (*s == c)
		return (char *)s;
	return NULL;
}

void *memmove(void *dst_void, const void *src_void, size_t length)
{
	char *dst = dst_void;
	const char *src = src_void;

	if (src < dst && dst < src + length) {
		/* Have to copy backwards */
		src += length;
		dst += length;
		while (length--) {
			*--dst = *--src;
		}
	} else {
		while (length--) {
			*dst++ = *src++;
		}
	}

	return dst_void;
}

size_t strlen(const char *str)
{
	const char *start = str;
	while (*str)
		str++;
	return str - start;
}

void strtrim(char *str, char *delim)
{
	char *p = str;
	while (*p != '\0') {
		if (strchr(delim, *p) == NULL) {
			break;
		}
		p++;
	}
	memmove(str, p, strlen(p));
	p = str + strlen(str);
	while (*p != '\0') {
		if (strchr(delim, *p) == NULL) {
			break;
		}
		*p = '\0';
		p--;
	}
}

char *__strtok_r(register char *s, register const char *delim, char **lasts,
		 int skip_leading_delim)
{
	register char *spanp;
	register int c, sc;
	char *tok;

	if (s == NULL && (s = *lasts) == NULL)
		return (NULL);

	/*
   * Skip (span) leading delimiters (s += strspn(s, delim), sort of).
   */
cont:
	c = *s++;
	for (spanp = (char *)delim; (sc = *spanp++) != 0;) {
		if (c == sc) {
			if (skip_leading_delim) {
				goto cont;
			} else {
				*lasts = s;
				s[-1] = 0;
				return (s - 1);
			}
		}
	}

	if (c == 0) { /* no non-delimiter characters */
		*lasts = NULL;
		return (NULL);
	}
	tok = s - 1;

	/*
   * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
   * Note that delim must have one NUL; we stop if we see that, too.
   */
	for (;;) {
		c = *s++;
		spanp = (char *)delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*lasts = s;
				return (tok);
			}
		} while (sc != 0);
	}
	/* NOTREACHED */
}

char *strtok_r(register char *__restrict s,
	       register const char *__restrict delim, char **__restrict lasts)
{
	return __strtok_r(s, delim, lasts, 1);
}

char *forth_token(forth_ctx *ctx)
{
	const char *delim = " \t\r\n";
	char *token;
	char *saveptr = NULL;
	token = strtok_r((char *)FORTH_TP(ctx), delim, &saveptr);
	FORTH_TP(ctx) = (uintptr_t)saveptr;
	if (token != NULL) {
		strtrim(token, delim);
		if (FORTH_DEBUG_ENABLE(ctx)) {
			efi_puts("token: [");
			efi_puts(token);
			efi_puts("]\n\r");
		}
	}
	return token;
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 != '\0' && *s1 == *s2) {
		s1++;
		s2++;
	}

	return (*(unsigned char *)s1) - (*(unsigned char *)s2);
}

/// c compiler must enable tail call optim

forth_word *forth_find_recursive(char *name, forth_word *latest)
{
	if (name == NULL)
		return (void *)NULL;
	if (latest == NULL)
		return (void *)NULL;
	if (strcmp(name, latest->name) == 0) {
		return latest;
	}
	return forth_find_recursive(name, latest->link);
}

void forth_retc(forth_ctx *ctx)
{
	(void)ctx;
	return;
}

FORTH_ADD_WORD(retc, 0, &branch0, forth_retc, NULL);

void *memset(void *m, int c, size_t n)
{
	char *s = (char *)m;

	while (n--)
		*s++ = (char)c;

	return m;
}

char *strcpy(char *dst0, const char *src0)
{
	char *s = dst0;

	while ((*dst0++ = *src0++))
		;

	return s;
}

void forth_colon(forth_ctx *ctx)
{
	char *token;
	token = forth_token(ctx);
	if (token == NULL) {
		FORTH_SS(ctx) |= FORTH_SS_COMPERR;
		forth_abort(ctx);
		return;
	}
	forth_word *word;
	word = (forth_word *)FORTH_HERE(ctx);
	memset(word, 0, sizeof(forth_word));
	FORTH_HERE(ctx) += sizeof(forth_word);
	word->name = (char *)FORTH_HERE(ctx);
	strcpy(word->name, token);
	FORTH_HERE(ctx) += strlen(word->name) + sizeof('\0');
	word->attr = 0;
	word->link = (forth_word *)FORTH_LATEST(ctx);
	word->entry = forth_nest;
	word->body = (void *)FORTH_HERE(ctx);
	FORTH_LATEST(ctx) = (uint8_t *)word;
	FORTH_SS(ctx) |= FORTH_SS_COMPING;
	forth_next(ctx);
}

FORTH_ADD_WORD(colon, 0, &retc, forth_colon, NULL);

void forth_semcol(forth_ctx *ctx)
{
	FORTH_SS(ctx) &= ~(FORTH_SS_COMPING);
	forth_next(ctx);
}

FORTH_ADD_WORD(semcol, FORTH_ATTR_IMMEDIATE, &colon, forth_semcol, NULL);

void forth_here(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)&FORTH_HERE(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(here, 0, &semcol, forth_here, NULL);

void forth_latest(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)&FORTH_LATEST(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(latest, 0, &here, forth_latest, NULL);

void forth_load(forth_ctx *ctx)
{
	forth_push(ctx, *(uintptr_t *)forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(load, 0, &latest, forth_load, NULL);

void forth_store(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	*(uintptr_t *)FORTH_WP(ctx) = FORTH_XP(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(store, 0, &load, forth_store, NULL);

void forth_regss(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)&FORTH_SS(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(regss, 0, &store, forth_regss, NULL);

void forth_dict_push(forth_ctx *ctx, uintptr_t v)
{
	*(uintptr_t *)FORTH_HERE(ctx) = v;
	FORTH_HERE(ctx) += sizeof(v);
}

void forth_ifnz(forth_ctx *ctx)
{
	forth_dict_push(ctx, (uintptr_t)&branch0);
	// save address use for word 'then'
	forth_push(ctx, (uintptr_t)FORTH_HERE(ctx));
	// word 'then' will fill this
	forth_dict_push(ctx, 0);
	forth_next(ctx);
}

FORTH_ADD_WORD(ifnz, FORTH_ATTR_IMMEDIATE, &regss, forth_ifnz, NULL);

void forth_then(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	// fill branch dest address
	*(uintptr_t *)FORTH_WP(ctx) = (uintptr_t)FORTH_HERE(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(then, FORTH_ATTR_IMMEDIATE, &ifnz, forth_then, NULL);

void forth_begin(forth_ctx *ctx)
{
	// save current address, word 'until' will use this address for branch
	forth_push(ctx, (uintptr_t)FORTH_HERE(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(begin, FORTH_ATTR_IMMEDIATE, &then, forth_begin, NULL);

void forth_until(forth_ctx *ctx)
{
	forth_dict_push(ctx, (uintptr_t)&branch0);
	forth_dict_push(ctx, (uintptr_t)forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(until, FORTH_ATTR_IMMEDIATE, &begin, forth_until, NULL);

void forth_add(forth_ctx *ctx)
{
	forth_push(ctx, forth_pop(ctx) + forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(add, 0, &until, forth_add, NULL);

void forth_sub(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	forth_push(ctx, FORTH_XP(ctx) - FORTH_WP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(sub, 0, &add, forth_sub, NULL);

void forth_mul(forth_ctx *ctx)
{
	forth_push(ctx, forth_pop(ctx) * forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(mul, 0, &sub, forth_mul, NULL);

void forth_idiv(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_WP(ctx) == 0) {
		FORTH_SS(ctx) |= FORTH_SS_DIVZERO;
		forth_abort(ctx);
		return;
	}
	forth_push(ctx, FORTH_XP(ctx) / FORTH_WP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(idiv, 0, &mul, forth_idiv, NULL);

void forth_or(forth_ctx *ctx)
{
	forth_push(ctx, forth_pop(ctx) | forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(or, 0, &idiv, forth_or, NULL);

void forth_and(forth_ctx *ctx)
{
	forth_push(ctx, forth_pop(ctx) & forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(and, 0, & or, forth_and, NULL);

void forth_xor(forth_ctx *ctx)
{
	forth_push(ctx, forth_pop(ctx) ^ forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(xor, 0, &and, forth_xor, NULL);

void forth_shl(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	forth_push(ctx, FORTH_XP(ctx) << FORTH_WP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(shl, 0, &xor, forth_shl, NULL);

void forth_shr(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	forth_push(ctx, FORTH_XP(ctx) >> FORTH_WP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(shr, 0, &shl, forth_shr, NULL);

void forth_emit(forth_ctx *ctx)
{
	efi_putc((char)forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(emit, 0, &shr, forth_emit, NULL);

const forth_word *add1_ws[] = {
	&lit,
	(void *)1,
	&add,
	&unnest,
};

FORTH_ADD_WORD(add1, 0, &emit, forth_nest, &add1_ws[0]);

const forth_word *sub1_ws[] = {
	&lit,
	(void *)1,
	&sub,
	&unnest,
};

FORTH_ADD_WORD(sub1, 0, &add1, forth_nest, &sub1_ws[0]);

void forth_dup(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	forth_push(ctx, FORTH_WP(ctx));
	forth_push(ctx, FORTH_WP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(dup, 0, &sub1, forth_dup, NULL);

void forth_swap(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	forth_push(ctx, FORTH_WP(ctx));
	forth_push(ctx, FORTH_XP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(swap, 0, &dup, forth_swap, NULL);

void forth_over(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	forth_push(ctx, FORTH_XP(ctx));
	forth_push(ctx, FORTH_WP(ctx));
	forth_push(ctx, FORTH_XP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(over, 0, &swap, forth_over, NULL);

void forth_rot(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	uintptr_t tmp;
	tmp = forth_pop(ctx);

	forth_push(ctx, FORTH_XP(ctx));
	forth_push(ctx, FORTH_WP(ctx));
	forth_push(ctx, tmp);
	forth_next(ctx);
}

FORTH_ADD_WORD(rot, 0, &over, forth_rot, NULL);

void forth_eq(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_WP(ctx) == FORTH_XP(ctx)) {
		forth_push(ctx, -1);
	} else {
		forth_push(ctx, 0);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(eq, 0, &rot, forth_eq, NULL);

void forth_lt(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_XP(ctx) < FORTH_WP(ctx)) {
		forth_push(ctx, -1);
	} else {
		forth_push(ctx, 0);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(lt, 0, &eq, forth_lt, NULL);

void forth_gt(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_XP(ctx) > FORTH_WP(ctx)) {
		forth_push(ctx, -1);
	} else {
		forth_push(ctx, 0);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(gt, 0, &lt, forth_gt, NULL);

void forth_le(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_XP(ctx) <= FORTH_WP(ctx)) {
		forth_push(ctx, -1);
	} else {
		forth_push(ctx, 0);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(le, 0, &gt, forth_le, NULL);

void forth_ge(forth_ctx *ctx)
{
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	if (FORTH_XP(ctx) >= FORTH_WP(ctx)) {
		forth_push(ctx, -1);
	} else {
		forth_push(ctx, 0);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(ge, 0, &le, forth_ge, NULL);

void forth_negate(forth_ctx *ctx)
{
	forth_push(ctx, ~(forth_pop(ctx)));
	forth_next(ctx);
}

FORTH_ADD_WORD(negate, 0, &ge, forth_negate, NULL);

const forth_word *ne_ws[] = { &eq, &negate, &unnest };

FORTH_ADD_WORD(ne, 0, &negate, forth_nest, &ne_ws[0]);

const forth_word *eqz_ws[] = { &lit, (void *)0, &eq, &unnest };

FORTH_ADD_WORD(eqz, 0, &ne, forth_nest, &eqz_ws[0]);

const forth_word *nez_ws[] = { &eqz, &negate, &unnest };

FORTH_ADD_WORD(nez, 0, &eqz, forth_nest, &nez_ws[0]);

void forth_constant(forth_ctx *ctx)
{
	forth_word *cur;
	cur = (forth_word *)FORTH_WP(ctx);
	uintptr_t *p;
	p = (uintptr_t *)cur->body;
	forth_push(ctx, *p);
	forth_next(ctx);
}

FORTH_ADD_WORD(constant, 0, &nez, forth_constant, NULL);

#define FORTH_ADD_CONSTANT(name, link, value)  \
	const uintptr_t _const_##name = value; \
	FORTH_ADD_WORD(name, 0, link, forth_constant, &_const_##name);

FORTH_ADD_CONSTANT(SS_DEBUG, &constant, FORTH_SS_DEBUG);

const forth_word *dbgon_ws[] = { &SS_DEBUG, &regss, &load,  & or,
				 &regss,    &store, &unnest };

FORTH_ADD_WORD(dbgon, 0, &SS_DEBUG, forth_nest, &dbgon_ws[0]);

const forth_word *dbgoff_ws[] = { &SS_DEBUG, &negate, &regss, &load,
				  &and,	     &regss,  &store, &unnest };

FORTH_ADD_WORD(dbgoff, 0, &dbgon, forth_nest, &dbgoff_ws[0]);

void forth_dsinfo(forth_ctx *ctx)
{
	forth_ds_dump(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(dsinfo, 0, &dbgoff, forth_dsinfo, NULL);

void forth_rsinfo(forth_ctx *ctx)
{
	forth_rs_dump(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(rsinfo, 0, &dsinfo, forth_rsinfo, NULL);

void forth_reginfo(forth_ctx *ctx)
{
	forth_reg_dump(ctx);
	forth_next(ctx);
}

FORTH_ADD_WORD(reginfo, 0, &rsinfo, forth_reginfo, NULL);

void forth_depth(forth_ctx *ctx)
{
	forth_push(ctx,
		   ((FORTH_SP(ctx) - FORTH_SB(ctx)) / sizeof(uintptr_t) + 1));
	forth_next(ctx);
}

FORTH_ADD_WORD(depth, 0, &reginfo, forth_depth, NULL);

void forth_dtor(forth_ctx *ctx)
{
	forth_rpush(ctx, forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(dtor, 0, &depth, forth_dtor, NULL);

void forth_rtod(forth_ctx *ctx)
{
	forth_push(ctx, forth_rpop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(rtod, 0, &dtor, forth_rtod, NULL);

void forth_regtb(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)&FORTH_TB(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(regtb, 0, &rtod, forth_regtb, NULL);

void forth_regtp(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)&FORTH_TP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(regtp, 0, &regtb, forth_regtp, NULL);

void forth_regtt(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)&FORTH_TT(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(regtt, 0, &regtp, forth_regtt, NULL);

void forth_efisystab(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)GST);
	forth_next(ctx);
}

FORTH_ADD_WORD(efisystab, 0, &regtt, forth_efisystab, NULL);

void forth_efiimghandle(forth_ctx *ctx)
{
	forth_push(ctx, (uintptr_t)GIH);
	forth_next(ctx);
}

FORTH_ADD_WORD(efiimghandle, 0, &efisystab, forth_efiimghandle, NULL);

void forth_hex(forth_ctx *ctx)
{
	efi_puthex(forth_pop(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(hex, 0, &efiimghandle, forth_hex, NULL);

void forth_tstc(forth_ctx *ctx)
{
	forth_push(ctx, efi_tstc());
	forth_next(ctx);
}

FORTH_ADD_WORD(tstc, 0, &hex, forth_tstc, NULL);

void forth_getc(forth_ctx *ctx)
{
	forth_push(ctx, efi_getc());
	forth_next(ctx);
}

FORTH_ADD_WORD(getc, 0, &tstc, forth_getc, NULL);

void forth_clr(forth_ctx *ctx)
{
	efi_clean_screen();
	forth_next(ctx);
}

FORTH_ADD_WORD(clr, 0, &getc, forth_clr, NULL);

void forth_fwver(forth_ctx *ctx) {
	forth_push(ctx, GST->FirmwareRevision);
	forth_next(ctx);
}

FORTH_ADD_WORD(fwver, 0, &clr, forth_fwver, NULL);

void forth_fbaddr(forth_ctx *ctx) {
	if (GGOP != NULL) {
		forth_push(ctx, GGOP->Mode->FrameBufferBase);
	} else {
		forth_push(ctx, -1);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(fbaddr, 0, &fwver, forth_fbaddr, NULL);

void forth_fbsize(forth_ctx *ctx) {
	if (GGOP != NULL) {
		forth_push(ctx, GGOP->Mode->FrameBufferSize);
	} else {
		forth_push(ctx, -1);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(fbsize, 0, &fbaddr, forth_fbsize, NULL);

void forth_memfill(forth_ctx *ctx) {
	uintptr_t c;
	c = forth_pop(ctx);
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	memset((void *)FORTH_XP(ctx), c, FORTH_WP(ctx));
	forth_next(ctx);
}

FORTH_ADD_WORD(memfill, 0, &fbsize, forth_memfill, NULL);

void forth_fbdrawp(forth_ctx *ctx) {
	uint16_t x, y;
	uint32_t color;
	color = forth_pop(ctx);
	y = forth_pop(ctx);
	x = forth_pop(ctx);
	efi_gop_draw_pixel(x, y, color);
	forth_next(ctx);
}

FORTH_ADD_WORD(fbdrawp, 0, &memfill, forth_fbdrawp, NULL);

void forth_fbxmax(forth_ctx *ctx) {
	if (GGOP == NULL) {
		forth_push(ctx, 0);
	} else {
		forth_push(ctx, GGOP->Mode->Info->HorizontalResolution);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(fbxmax, 0, &fbdrawp, forth_fbxmax, NULL);

void forth_fbymax(forth_ctx *ctx) {
	if (GGOP == NULL) {
		forth_push(ctx, 0);
	} else {
		forth_push(ctx, GGOP->Mode->Info->VerticalResolution);
	}
	forth_next(ctx);
}

FORTH_ADD_WORD(fbymax, 0, &fbxmax, forth_fbymax, NULL);

const forth_word *fbblank_ws[] = {
	 &fbaddr, &fbsize, &lit, (void *)0, &memfill, &unnest,
};

FORTH_ADD_WORD(fbblank, 0, &fbymax, forth_nest, &fbblank_ws[0]);

void forth_fbdrawxl(forth_ctx *ctx) {
	uint32_t color = forth_pop(ctx);
	uint32_t y = forth_pop(ctx);
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	efi_gop_drawxline(FORTH_WP(ctx), FORTH_XP(ctx), y, color);
	forth_next(ctx);

}

FORTH_ADD_WORD(fbdrawxl, 0, &fbblank, forth_fbdrawxl, NULL);

void forth_fbdrawyl(forth_ctx *ctx) {
	uint32_t color = forth_pop(ctx);
	uint32_t x = forth_pop(ctx);
	FORTH_WP(ctx) = forth_pop(ctx);
	FORTH_XP(ctx) = forth_pop(ctx);
	efi_gop_drawyline(FORTH_WP(ctx), FORTH_XP(ctx), x, color);
	forth_next(ctx);
}

FORTH_ADD_WORD(fbdrawyl, 0, &fbdrawxl, forth_fbdrawyl, NULL);

void forth_fbdrawrect(forth_ctx *ctx) {
	uint32_t color = forth_pop(ctx);
	uint32_t y2 = forth_pop(ctx);
	uint32_t y1 = forth_pop(ctx);
	uint32_t x2 = forth_pop(ctx);
	uint32_t x1 = forth_pop(ctx);
	efi_gop_drawrect(x1, x2, y1, y2, color);
	forth_next(ctx);
}

FORTH_ADD_WORD(fbdrawrect, 0, &fbdrawyl, forth_fbdrawrect, NULL);

void forth_fbdrawsolid(forth_ctx *ctx) {
	uint32_t color = forth_pop(ctx);
	uint32_t y2 = forth_pop(ctx);
	uint32_t y1 = forth_pop(ctx);
	uint32_t x2 = forth_pop(ctx);
	uint32_t x1 = forth_pop(ctx);
	efi_gop_drawsolid(x1, x2, y1, y2, color);
	forth_next(ctx);
}

FORTH_ADD_WORD(fbdrawsolid, 0, &fbdrawrect, forth_fbdrawsolid, NULL);

const forth_word *isodd_ws[] = {
	&lit, (void *)1, &and, &nez, &unnest,
};

FORTH_ADD_WORD(isodd, 0, &fbdrawsolid, forth_nest, &isodd_ws[0]);

const forth_word *iseven_ws[] = {
	&isodd, &negate, &unnest,
};

FORTH_ADD_WORD(iseven, 0, &isodd, forth_nest, &iseven_ws[0]);

// colon fbzebrax 0 begin 0 swap fbxmax swap dup dtor dup iseven fbdrawxl rtod add1 dup fbymax ge until drop unnest semcol

const forth_word *fbzebrax_ws_end[] = {
	&drop, &unnest,
};

const forth_word *fbzebrax_ws1[] = {
	&lit, (void *)0, &swap, &fbxmax, &swap, &dup, &dtor, &dup, &iseven, &fbdrawxl,
	&rtod, &add1, &dup, &fbymax, &ge, &branch0, (void *)&fbzebrax_ws1[0],
	&branch, (void *)&fbzebrax_ws_end[0],
};

const forth_word *fbzebrax_ws[] = {
	&lit, (void *)0, &branch, (void *)&fbzebrax_ws1[0],
};

FORTH_ADD_WORD(fbzebrax, 0, &iseven, forth_nest, &fbzebrax_ws[0]);


// colon fbzebrax 0 begin 0 swap fbxmax swap dup dtor dup iseven fbdrawxl rtod add1 dup fbymax ge until drop unnest semcol

const forth_word *fbzebray_ws_end[] = {
	&drop, &unnest,
};

const forth_word *fbzebray_ws1[] = {
	&lit, (void *)0, &swap, &fbymax, &swap, &dup, &dtor, &dup, &iseven, &fbdrawyl,
	&rtod, &add1, &dup, &fbxmax, &ge, &branch0, (void *)&fbzebray_ws1[0],
	&branch, (void *)&fbzebray_ws_end[0],
};

const forth_word *fbzebray_ws[] = {
	&lit, (void *)0, &branch, (void *)&fbzebray_ws1[0],
};

FORTH_ADD_WORD(fbzebray, 0, &fbzebrax, forth_nest, &fbzebray_ws[0]);

void forth_abort(forth_ctx *ctx)
{
	(void)ctx;
	// TODO: setjmp
}

void forth_stack_reset(forth_ctx *ctx)
{
	FORTH_SP(ctx) = FORTH_SB(ctx);
	FORTH_RP(ctx) = FORTH_RB(ctx);
}

void forth_single_word_execute(forth_ctx *ctx, forth_word *word)
{
	forth_word *ws[2];
	ws[0] = word;
	ws[1] = &retc;
	FORTH_IP(ctx) = (uintptr_t)&ws[0];
	forth_next(ctx);
}

int isdigit(int c)
{
	return (unsigned)c - '0' < 10;
}

int strisnum(char *s)
{
	if (isdigit(s[0])) {
		return 1;
	}
	if ((s[0] == '-') && isdigit(s[1])) {
		return 1;
	}
	if ((s[0] == '0') && (s[1] == 'x')) {
		return 1;
	}
	return 0;
}

int isspace(int c)
{
	return c == ' ' || ('\t' <= c && c <= '\r');
}

/*
 * Convert a string to a long integer.
 */
long strtol(const char *__restrict nptr, char **__restrict endptr, int base)
{
	register const unsigned char *s = (const unsigned char *)nptr;
	register unsigned long acc;
	register int c;
	register unsigned long cutoff;
	register int neg = 0, any, cutlim;

	if (base < 0 || base == 1 || base > 36) {
		// errno = EINVAL;
		if (endptr != 0)
			*endptr = (char *)nptr;
		return 0;
	}

	/*
   * Skip white space and pick up leading +/- sign if any.
   * If base is 0, allow 0x for hex and 0 for octal, else
   * assume decimal; if base is already 16, allow 0x.
   */
	do {
		c = *s++;
	} while (isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X') &&
	    (('0' <= s[1] && s[1] <= '9') || ('a' <= s[1] && s[1] <= 'f') ||
	     ('A' <= s[1] && s[1] <= 'F'))) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
   * Compute the cutoff value between legal numbers and illegal
   * numbers.  That is the largest legal value, divided by the
   * base.  An input number that is greater than this value, if
   * followed by a legal input character, is too big.  One that
   * is equal to this value may be valid or not; the limit
   * between valid and invalid numbers is then based on the last
   * digit.  For instance, if the range for longs is
   * [-2147483648..2147483647] and the input base is 10,
   * cutoff will be set to 214748364 and cutlim to either
   * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
   * a value > 214748364, or equal but the next digit is > 7 (or 8),
   * the number is too big, and we will return a range error.
   *
   * Set any if any `digits' consumed; make it negative to indicate
   * overflow.
   */
	cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
	cutlim = cutoff % (unsigned long)base;
	cutoff /= (unsigned long)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'A' && c <= 'Z')
			c -= 'A' - 10;
		else if (c >= 'a' && c <= 'z')
			c -= 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
			any = -1;
		} else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = neg ? LONG_MIN : LONG_MAX;
		//_REENT_ERRNO(rptr) = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *)(any ? (char *)s - 1 : nptr);
	return (acc);
}

void forth_eval(forth_ctx *ctx)
{
	char *token;
	token = forth_token(ctx);
	if (token == NULL) {
		return;
	}
	forth_word *word;
	word = forth_find_recursive(token, (forth_word *)FORTH_LATEST(ctx));
	if ((word == NULL) && !strisnum(token)) {
		if (FORTH_DEBUG_ENABLE(ctx)) {
			efi_puts(token);
			efi_puts(" not found\n\r");
		}
		FORTH_SS(ctx) |= FORTH_SS_WORDNF;
		forth_abort(ctx);
		return;
	}
	uintptr_t num;
	if ((word == NULL) && strisnum(token)) {
		num = strtol(token, NULL, 0);
		if (FORTH_COMPING_ENABLE(ctx)) {
			forth_dict_push(ctx, (uintptr_t)&lit);
			forth_dict_push(ctx, num);
		} else {
			forth_push(ctx, num);
		}
		return forth_eval(ctx);
	}
	if (FORTH_COMPING_ENABLE(ctx) && (FORTH_WORD_IS_IMMEDIATE(word))) {
		forth_single_word_execute(ctx, word);
		return forth_eval(ctx);
	}
	if (FORTH_COMPING_ENABLE(ctx)) {
		forth_dict_push(ctx, (uintptr_t)word);
	} else {
		forth_single_word_execute(ctx, word);
	}
	return forth_eval(ctx);
}

void readline(char *line, int size)
{
	char *newline_chs = "\n\r";
	char *delete_chs = "\b\x7F";
	char *start = line;
	while (size > 0) {
		*line = efi_getc();
		if (strchr(newline_chs, *line) != NULL) {
			efi_puts("\n\r");
			*line = '\0';
			break;
		}
		if (strchr(delete_chs, *line) != NULL) {
			if (line > start) {
				efi_putc(0x08); // BS
				line--;
			} else {
				;
			}
		} else {
			efi_putc(*line);
			line++;
		}
		size--;
	}
	*line = '\0';
}


EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
			   EFI_SYSTEM_TABLE *SystemTable)
{
	GST = SystemTable;
	GIH = ImageHandle;
	GBS = GST->BootServices;

	forth_ctx ctx0;
	forth_regs ctx0_regs;
	uintptr_t ctx0_ds[8 + 128 + 8];
	uintptr_t ctx0_rs[8 + 128 + 8];
	char ctx0_ts[8 + 128 + 8];
	uint8_t ctx0_dict_ram[256 * 1024];

	memset(&ctx0, 0, sizeof(ctx0));
	memset(&ctx0_regs, 0, sizeof(ctx0_regs));

	ctx0.regs = &ctx0_regs;
	FORTH_HERE(&ctx0) = &ctx0_dict_ram[0];
	FORTH_LATEST(&ctx0) = (uint8_t *)&fbzebray;

	FORTH_SS(&ctx0) = 0;

	FORTH_SP(&ctx0) = (uintptr_t)&ctx0_ds[8];
	FORTH_RP(&ctx0) = (uintptr_t)&ctx0_rs[8];
	FORTH_TP(&ctx0) = (uintptr_t)&ctx0_ts[8];

	FORTH_SB(&ctx0) = (uintptr_t)&ctx0_ds[8];
	FORTH_RB(&ctx0) = (uintptr_t)&ctx0_rs[8];
	FORTH_TB(&ctx0) = (uintptr_t)&ctx0_ts[8];

	FORTH_ST(&ctx0) = (uintptr_t)&ctx0_ds[8 + 127];
	FORTH_RT(&ctx0) = (uintptr_t)&ctx0_rs[8 + 127];
	FORTH_TT(&ctx0) = (uintptr_t)&ctx0_ts[8 + 127];

	efi_cursor_visible(1);
	efi_gop_init();

	uint8_t console_rxfifo[8 + 4096 + 8];

	efi_console_rxfifo.data = &console_rxfifo[8];
	efi_console_rxfifo.capacity = 4095;
	fifo8_reset(&efi_console_rxfifo);

	efi_puts("FORTH on UEFI\n\r");
	efi_puthex(0x0123456789ABCDEF);
	efi_puts("\n\r");
	efi_puthex(0xFEDCBA9876543210);
	efi_puts("\n\r");
	forth_ds_dump(&ctx0);
	forth_rs_dump(&ctx0);
	forth_reg_dump(&ctx0);

	while (forth_run) {
		FORTH_TP(&ctx0) = FORTH_TB(&ctx0);
		readline((char *)FORTH_TB(&ctx0),
			 (FORTH_TT(&ctx0) - FORTH_TB(&ctx0) - 1));
		forth_eval(&ctx0);
		if (FORTH_SS(&ctx0) & FORTH_SS_COMPERR) {
			efi_puts("\tforth compile error");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_COMPERR);
		}
		if (FORTH_SS(&ctx0) & FORTH_SS_DOVERFLOW) {
			efi_puts("\tforth data stack overflow");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_DOVERFLOW);
		}
		if (FORTH_SS(&ctx0) & FORTH_SS_DUNDERFLOW) {
			efi_puts("\tforth data stack underflow");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_DUNDERFLOW);
		}
		if (FORTH_SS(&ctx0) & FORTH_SS_ROVERFLOW) {
			efi_puts("\tforth return stack overflow");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_ROVERFLOW);
		}
		if (FORTH_SS(&ctx0) & FORTH_SS_RUNDERFLOW) {
			efi_puts("\tforth return stack underflow");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_RUNDERFLOW);
		}
		if (FORTH_SS(&ctx0) & FORTH_SS_WORDNF) {
			efi_puts("\tforth word not found");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_WORDNF);
		}
		if (FORTH_SS(&ctx0) & FORTH_SS_DIVZERO) {
			efi_puts("\tforth div zero");
			forth_stack_reset(&ctx0);
			FORTH_SS(&ctx0) &= ~(FORTH_SS_DIVZERO);
		}
		efi_puts("\n\r");
	}
	return 0;
}
