#include <stdint.h>
#include <efi.h>
#include <efilib.h>

#define ADDRSIZE sizeof(intptr_t)

EFI_HANDLE GIH = NULL;
EFI_SYSTEM_TABLE *GST = NULL;

intptr_t up;
intptr_t wp;
intptr_t xp;
intptr_t yp;
intptr_t zp;
intptr_t ip;
intptr_t sp;
intptr_t sb;
intptr_t st;
intptr_t rp;
intptr_t rb;
intptr_t rt;
intptr_t tp;
intptr_t tb;
intptr_t tt;
intptr_t ss;

enum {
	SSCOMP = (1 << 8),
	SSDOVE = (1 << 9),
	SSROVE = (1 << 10),
	SSDUND = (1 << 11),
	SSRUND = (1 << 12),
	SSDIVZ = (1 << 13),
};

enum {
    RINO = 0,
    RISP = 1,
    RIRP = 2,
    RITP = 3,
    RIIP = 4,
    RISB = 5,
    RIST = 6,
    RIRB = 7,
    RIRT = 8,
    RITB = 9,
    RITT = 10,
    RISS = 11,
    RINP = 12,
};

enum {
    ROSP = RISP * ADDRSIZE,
    RORP = RIRP * ADDRSIZE,
    ROTP = RITP * ADDRSIZE,
    ROIP = RIIP * ADDRSIZE,
    ROSB = RISB * ADDRSIZE,
    ROST = RIST * ADDRSIZE,
    RORB = RIRB * ADDRSIZE,
    RORT = RIRT * ADDRSIZE,
    ROTB = RITB * ADDRSIZE,
    ROTT = RITT * ADDRSIZE,
    ROSS = RISS * ADDRSIZE,
    RONP = RINP * ADDRSIZE,
};

#define tasknew(label, gap, dsize, rsize, tsize, entry, link) \
	void *dstk_##label[gap + dsize + gap]; \
	void *rstk_##label[gap + rsize + gap]; \
	void *tstk_##label[gap + tsize + gap]; \
	void *task_##label[] = { \
		[RISP] = (void *)&dstk_##label[gap], \
		[RIRP] = (void *)&rstk_##label[gap], \
		[RITP] = (void *)&tstk_##label[gap], \
		[RIIP] = (void *)entry, \
		[RISB] = (void *)&dstk_##label[gap], \
		[RIST] = (void *)&dstk_##label[gap + dsize - 1], \
		[RIRB] = (void *)&rstk_##label[gap], \
		[RIRT] = (void *)&rstk_##label[gap + rsize - 1], \
		[RITB] = (void *)&tstk_##label[gap], \
		[RITT] = (void *)&tstk_##label[gap + tsize - 1], \
		[RISS] = (void *)0, \
		[RINP] = (void *)link, \
	}; 

#define taskload(addr) \
	sp = LOAD(addr + ROSP); \
	rp = LOAD(addr + RORP); \
	tp = LOAD(addr + ROTP); \
	ip = LOAD(addr + ROIP); \
	sb = LOAD(addr + ROSB); \
	st = LOAD(addr + ROST); \
	rb = LOAD(addr + RORB); \
	rt = LOAD(addr + RORT); \
	tb = LOAD(addr + ROTB); \
	tt = LOAD(addr + ROTT); \
	ss = LOAD(addr + ROSS);

#define tasksave(addr) \
	STORE(sp, addr + ROSP); \
	STORE(rp, addr + RORP); \
	STORE(tp, addr + ROTP); \
	STORE(ip, addr + ROIP); \
	STORE(sb, addr + ROSB); \
	STORE(st, addr + ROST); \
	STORE(rb, addr + RORB); \
	STORE(rt, addr + RORT); \
	STORE(tb, addr + ROTB); \
	STORE(tt, addr + ROTT); \
	STORE(ss, addr + ROSS);



enum {
	WILINK = 0,
	WIENTR = 1,
	WIBODY = 2,
	WINAME = 3,
	WIATTR = 4,
};

enum {
	WOLINK = WILINK * ADDRSIZE,
	WOENTR = WIENTR * ADDRSIZE,
	WOBODY = WIBODY * ADDRSIZE,
	WONAME = WINAME * ADDRSIZE,
	WOATTR = WIATTR * ADDRSIZE,
	WHSIZE = WOATTR + ADDRSIZE,
};

enum {
	WATTMASK = (0xFF << 8),
	WATTIMMD = (1 << 8),
	WLENMASK = (0xFF),
};

#define rodef(label, name, link, entry, body, attr) \
	const char _str_##label[] = name; \
	const void *f_##label[] = { \
		[WILINK] = (void *)link, \
		[WIENTR] = (void *)entry, \
		[WIBODY] = (void *)body, \
		[WINAME] = (void *)&_str_##label[0], \
		[WIATTR] = (void *)(((sizeof(_str_##label) -1) & WLENMASK) \
				| (attr & WATTMASK)) \
	};

rodef(dummy, "dummy", NULL, NULL, NULL, 0);

#define rodefcode(label, name, link, attr) \
	void c_##label(void); \
	rodef(label, name, link, c_##label, -1, attr); \
	void c_##label(void)

rodefcode(bye, "bye", f_dummy, 0) {
	GST->ConOut->OutputString(GST->ConOut, L"Bye UEFI\n\r");
	return;
}

#define LOAD(addr) (*(intptr_t *)(addr))
#define STORE(v, addr) (*(intptr_t *)(addr) = (intptr_t)(v))
#define CLOAD(addr) (*(uint8_t *)(addr))
#define CSTORE(v, addr) (*(uint8_t *)(addr) = (uint8_t)(v))

void (*next_jump)(void);

rodefcode(next, "next", f_bye, 0) {
	wp = LOAD(ip);
	ip += ADDRSIZE;
	next_jump = (void *)(LOAD(wp + WOENTR));
	return next_jump();
}

#define NEXT return c_next()

static inline void rpush(intptr_t v) {
	if (rp > rt) {
		ss |= SSROVE;
	}
	STORE(v, rp);
	rp += ADDRSIZE;
	if (rp > rt) {
		ss |= SSROVE;
	}
}

static inline intptr_t rpop(void) {
	rp -= ADDRSIZE;
	if (rp < rb) {
		ss |= SSRUND;
	}
	return LOAD(rp);
}

rodefcode(call, "call", f_next, 0) {
	rpush(ip);
	ip = LOAD(wp + WOBODY);
	NEXT;
}

rodefcode(exit, "exit", f_call, 0) {
	ip = rpop();
	NEXT;
}

#define rodefword(label, name, link, attr) \
const void *w_##label[]; \
rodef(label, name, link, c_call, w_##label, attr); \
const void *w_##label[] =

rodefword(nop, "nop", f_exit, 0) {
	f_exit,
};

static inline void dpush(intptr_t v) {
	if (sp > st) {
		ss |= SSDOVE;
	}
	STORE(v, sp);
	sp += ADDRSIZE;
	if (sp > st) {
		ss |= SSROVE;
	}
}

static inline intptr_t dpop(void) {
	sp -= ADDRSIZE;
	if (sp < sb) {
		ss |= SSDUND;
	}
	return LOAD(sp);
}

rodefcode(lit, "lit", f_nop, 0) {
	dpush(LOAD(ip));
	ip += ADDRSIZE;
	NEXT;
}

rodefcode(branch0, "branch0", f_lit, 0) {
	wp = dpop();
	xp = LOAD(ip);
	ip += ADDRSIZE;
	if (wp == 0) {
		ip = xp;
	}
	NEXT;
}

rodefcode(branch, "branch", f_branch0, 0) {
	ip = LOAD(ip);
	NEXT;
}

rodefcode(execute, "execute", f_branch, 0) {
	wp = dpop();
	next_jump = (void *)(LOAD(wp + WOENTR));
	return next_jump();
}

rodefcode(false, "false", f_execute, 0) {
	dpush(0);
	NEXT;
}

rodefcode(true, "true", f_false, 0) {
	dpush(-1);
	NEXT;
}

rodefcode(equ, "=", f_true, 0) {
	if (dpop() == dpop()) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(invert, "invert", f_equ, 0) {
	dpush(dpop() ^ -1);
	NEXT;
}

rodefcode(add, "+", f_invert, 0) {
	dpush(dpop() + dpop());
	NEXT;
}

rodefcode(inc, "1+", f_add, 0) {
	dpush(dpop() + 1);
	NEXT;
}

rodefcode(sub, "-", f_inc, 0) {
	wp = dpop();
	dpush(dpop() - wp);
	NEXT;
}

rodefcode(dec, "1-", f_sub, 0) {
	dpush(dpop() - 1);
	NEXT;
}

rodefcode(mul, "*", f_dec, 0) {
	dpush(dpop() * dpop());
	NEXT;
}

rodefcode(lshift, "lshift", f_mul, 0) {
	wp = dpop();
	dpush(dpop() << wp);
	NEXT;
}

rodefcode(rshift, "rshift", f_lshift, 0) {
	wp = dpop();
	dpush(dpop() >> wp);
	NEXT;
}

rodefcode(and, "and", f_rshift, 0) {
	dpush(dpop() & dpop());
	NEXT;
}

rodefcode(or, "or", f_and, 0) {
	dpush(dpop() | dpop());
	NEXT;
}

rodefcode(xor, "xor", f_or, 0) {
	dpush(dpop() ^ dpop());
	NEXT;
}

rodefcode(div, "/", f_xor, 0) {
	wp = dpop();
	if (wp == 0) {
		ss |= SSDIVZ;
		NEXT;
	}
	dpush(dpop() / wp);
	NEXT;
}

rodefcode(bic, "bic", f_div, 0) {
	wp = dpop();
	dpush(dpop() & (wp ^ -1));
	NEXT;
}

#define DS_DEPTH ((sp - sb) / ADDRSIZE)

rodefcode(depth, "depth", f_bic, 0) {
	dpush(DS_DEPTH);
	NEXT;
}

rodefcode(dchk, "dchk", f_depth, 0) {
	if (DS_DEPTH != 0) {
		while(1);
	}
	NEXT;
}

rodefcode(drop, "drop", f_dchk, 0) {
	dpop();
	NEXT;
}

rodefcode(dup, "dup", f_drop, 0) {
	wp = dpop();
	dpush(wp);
	dpush(wp);
	NEXT;
}

rodefcode(swap, "swap", f_dup, 0) {
	wp = dpop();
	xp = dpop();
	dpush(wp);
	dpush(xp);
	NEXT;
}

rodefcode(over, "over", f_swap, 0) {
	wp = dpop();
	xp = dpop();
	dpush(xp);
	dpush(wp);
	dpush(xp);
	NEXT;
}

rodefcode(rot, "rot", f_over, 0) {
	wp = dpop();
	xp = dpop();
	yp = dpop();
	dpush(xp);
	dpush(wp);
	dpush(yp);
	NEXT;
}

rodefcode(2lit, "2lit", f_rot, 0) {
	dpush(LOAD(ip));
	ip += ADDRSIZE;
	dpush(LOAD(ip));
	ip += ADDRSIZE;
	NEXT;
}

rodefcode(2drop, "2drop", f_2lit, 0) {
	dpop();
	dpop();
	NEXT;
}

rodefcode(2dup, "2dup", f_2drop, 0) {
	wp = dpop();
	xp = dpop();
	dpush(xp);
	dpush(wp);
	dpush(xp);
	dpush(wp);
	NEXT;
}

rodefcode(2swap, "2swap", f_2dup, 0) {
	wp = dpop();
	xp = dpop();
	yp = dpop();
	zp = dpop();
	dpush(xp);
	dpush(wp);
	dpush(zp);
	dpush(yp);
	NEXT;
}

rodefcode(2over, "2over", f_2swap, 0) {
	wp = dpop();
	xp = dpop();
	yp = dpop();
	zp = dpop();
	dpush(zp);
	dpush(yp);
	dpush(xp);
	dpush(wp);
	dpush(zp);
	dpush(yp);
	NEXT;
}

rodefcode(tor, ">r", f_2over, 0) {
	rpush(dpop());
	NEXT;
}

rodefcode(fromr, "r>", f_tor, 0) {
	dpush(rpop());
	NEXT;
}

rodefword(2rot, "2rot", f_fromr, 0) {
	f_tor, f_tor, f_2swap, f_fromr, f_fromr, f_2swap, f_exit,
};

rodefcode(eqz, "0=", f_2rot, 0) {
	if (dpop() == 0) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(nez, "0<>", f_eqz, 0) {
	if (dpop() != 0) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(neq, "<>", f_nez, 0) {
	if (dpop() != dpop()) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(lt, "<", f_neq, 0) {
	wp = dpop();
	if (dpop() < wp) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(gt, ">", f_lt, 0) {
	wp = dpop();
	if (dpop() > wp) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(le, "<=", f_gt, 0) {
	wp = dpop();
	if (dpop() <= wp) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(ge, ">=", f_le, 0) {
	wp = dpop();
	if (dpop() >= wp) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

rodefcode(within, "within", f_ge, 0) {
	wp = dpop(); // max
	xp = dpop(); // min
	yp = dpop(); // n

	zp = MAX(wp, xp);
	xp = MIN(wp, xp);

	if ((yp >= xp) && (yp < zp)) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

rodefcode(doconst, "doconst", f_within, 0) {
	dpush(LOAD(wp + WOBODY));
	NEXT;
}

#define rodefconst(label, name, value, link, attr) \
rodef(label, name, link, c_doconst, value, attr);

rodefconst(cell, "cell", ADDRSIZE, f_doconst, 0);

rodefcode(load, "@", f_cell, 0) {
	dpush(LOAD(dpop()));
	NEXT;
}

rodefcode(store, "!", f_load, 0) {
	wp = dpop();
	STORE(dpop(), wp);
	NEXT;
}

rodefcode(cload, "c@", f_store, 0) {
	dpush(CLOAD(dpop()));
	NEXT;
}

rodefcode(cstore, "c!", f_cload, 0) {
	wp = dpop();
	CSTORE(dpop(), wp);
	NEXT;
}

rodefcode(yield, "yield", f_cstore, 0) {
	tasksave(up);
	up = LOAD(up + RONP);
	taskload(up);
	NEXT;
}

EFI_INPUT_KEY Key;
intptr_t conrxbuf = 0;
#define CON_RX_DONE (1 << 16)

void efi_con_rx(void) {
	if (conrxbuf & CON_RX_DONE) {
		dpush(-1);
		return;
	}
	wp = GST->ConIn->ReadKeyStroke(GST->ConIn,&Key);
	if (wp == EFI_SUCCESS) {
		conrxbuf = Key.UnicodeChar | CON_RX_DONE;
		dpush(-1);
		return;
	}
	conrxbuf = 0;
	dpush(0);
	return;
}

rodefcode(conrxava, "conrx?", f_yield, 0) {
	efi_con_rx();
	NEXT;
}

rodefconst(conrxbuf, "conrxbuf", &conrxbuf, f_keyava, 0);
rodefconst(conrxdone, "conrxdone", CON_RX_DONE, f_conrxbuf, 0);

rodefword(waitconrx, "waitconrx", f_conrxdone, 0) {
	f_yield, f_conrxava, f_branch0, w_waitconrx, f_exit,
};

rodefword(conrx, "conrxc", f_waitconrx, 0) {
	f_waitconrx,
	f_conrxbuf, f_load, f_conrxdone, f_xor,
	f_dup, f_conrxbuf, f_store, f_exit,
};

CHAR16 emitbuf[2];

rodefcode(emit, "emit", f_conrx, 0) {
	emitbuf[0] = dpop();
	emitbuf[1] = '\0';
	GST->ConOut->OutputString(GST->ConOut, emitbuf);
	NEXT;
}

rodefcode(feedog, "feedog", f_emit, 0) {
	GST->BootServices->SetWatchdogTimer(10, 0, 0, NULL);
	NEXT;
}

char xdigits[] = "0123456789ABCDEF";

rodefword(hex4, "hex4", f_feedog, 0) {
	f_lit, 0xF, f_and,
	f_lit, xdigits, f_add, f_cload, f_emit, f_exit,
};

rodefword(hex8, "hex8", f_hex4, 0) {
	f_dup, f_lit, 4, f_rshift, f_hex4, f_hex4, f_exit,
};

rodefword(hex16, "hex16", f_hex8, 0) {
	f_dup, f_lit, 8, f_rshift, f_hex8, f_hex8, f_exit,
};

rodefword(hex32, "hex32", f_hex16, 0) {
	f_dup, f_lit, 16, f_rshift, f_hex16, f_hex16, f_exit,
};

rodefword(hex64, "hex64", f_hex32, 0) {
	f_dup, f_lit, 32, f_rshift, f_hex32, f_hex32, f_exit,
};

const void *w_hex_8[] = {
	f_hex8, f_exit,
};

const void *w_hex_16[] = {
	f_cell, f_lit, 2, f_equ, f_branch0, w_hex8,
	f_hex16, f_exit,
};

const void *w_hex_32[] = {
	f_cell, f_lit, 4, f_equ, f_branch0, w_hex16,
	f_hex32, f_exit,
};

rodefword(hex, "hex", f_hex64, 0) {
	f_cell, f_lit, 8, f_equ, f_branch0, w_hex_32,
	f_hex64, f_exit,
};

rodefcode(sbget, "sb@", f_hex, 0) {
	dpush(sb);
	NEXT;
}

const void *w_dsdump_end[] = {
	f_2lit, '\n', '\r', f_emit, f_emit,
	f_2drop, f_exit,
};

const void *w_dsdump_loop[] = {
	f_dup, f_branch0, w_dsdump_end,
	f_dec, f_swap, f_dup, f_load, f_hex,
	f_lit, ' ', f_emit, f_cell, f_add, f_swap,
	f_branch, w_dsdump_loop,
};

rodefword(dsdump, ".s", f_sbget, 0) {
	f_depth, f_dup,
	f_lit, '(', f_emit,
	f_hex16,
	f_lit, ')', f_emit,
	f_lit, ' ', f_emit,
	f_sbget, f_swap,
	f_branch, w_dsdump_loop,
};

const void *w_hexdump_end[] = {
	f_2lit, '\n', '\r', f_emit, f_emit,
	f_2drop, f_exit,
};

// addr count
rodefword(hex8dump, "hex8dump", f_dsdump, 0) {
	f_dup, f_branch0, w_hexdump_end,
	f_dec, f_swap, f_dup, f_hex,
	f_lit, ':', f_emit, f_lit, ' ', f_emit,
	f_dup, f_cload, f_hex8, f_inc, f_swap,
	f_2lit, '\n', '\r', f_emit, f_emit,
	f_branch, w_hex8dump,
};

// addr count
rodefword(hexdump, "hexdump", f_hex8dump, 0) {
	f_dup, f_branch0, w_hexdump_end,
	f_dec, f_swap, f_dup, f_hex,
	f_lit, ':', f_emit, f_lit, ' ', f_emit,
	f_dup, f_load, f_hex,
	f_cell, f_add, f_swap,
	f_2lit, '\n', '\r', f_emit, f_emit,
	f_branch, w_hexdump,
};

// addr u c
rodefcode(fill, "fill", f_hexdump, 0) {
	wp = dpop();
	xp = dpop();
	yp = dpop();
	GST->BootServices->SetMem(yp, xp, wp);
	NEXT;
}

void _compare(void) {
	wp = dpop();
	xp = dpop();
	yp = dpop();
	zp = dpop();
	wp = MIN(wp, yp);
	if (wp == 0) {
		dpush(0);
		return;
	}
	unsigned char *s1 = (unsigned char *)zp;
	unsigned char *s2 = (unsigned char *)xp;
	while(wp--) {
		if (*s1 != *s2) {
			dpush(0);
			return;
		}
		s1++;
		s2++;
	}
	dpush(-1);
	return;
}

// addr1 u1 addr2 u2
rodefcode(compare, "compare", f_fill, 0) {
	_compare();
	NEXT;
}

// addr1 addr2 u
rodefcode(cmove, "cmove", f_compare, 0) {
	wp = dpop();
	xp = dpop();
	yp = dpop();
	GST->BootServices->CopyMem(xp, yp, wp);
	NEXT;
}

const void *w_type_end[] = {
	f_2drop, f_exit,
};

// addr u
rodefword(type, "type", f_cmove, 0) {
	f_dup, f_branch0, w_type_end,
	f_dec, f_swap, f_dup, f_cload, f_emit, f_inc, f_swap,
	f_branch, w_type,
};

void *latest;
uint8_t dict[128 * 1024 * 1024];
void *here = dict;

rodefconst(latest, "latest", &latest, f_type, 0);
rodefconst(here, "here", &here, f_latest, 0);
rodefconst(wolink, "wolink", WOLINK, f_here, 0);
rodefconst(woentr, "woentr", WOENTR, f_wolink, 0);
rodefconst(wobody, "wobody", WOBODY, f_woentr, 0);
rodefconst(woname, "woname", WONAME, f_wobody, 0);
rodefconst(woattr, "woattr", WOATTR, f_woname, 0);
rodefconst(wattrmask, "wattrmask", WATTMASK, f_woattr, 0);
rodefconst(wnlenmask, "wnlenmask", WLENMASK, f_wattrmask, 0);
rodefconst(whsize, "whsize", WHSIZE, f_wnlenmask, 0);

rodefword(latestget, "latest@", f_whsize, 0) {
	f_latest, f_load, f_exit,
};

rodefword(latestset, "latest!", f_latestget, 0) {
	f_latest, f_store, f_exit,
};

rodefword(hereget, "here@", f_latestset, 0) {
	f_here, f_load, f_exit,
};

rodefword(hereset, "here!", f_hereget, 0) {
	f_here, f_store, f_exit,
};

rodefword(wlinkget, "wlink@", f_hereset, 0) {
    f_wolink, f_add, f_load, f_exit,
};

rodefword(wlinkset, "wlink!", f_wlinkget, 0) {
    f_wolink, f_add, f_store, f_exit,
};

rodefword(wentrget, "wentr@", f_wlinkset, 0) {
    f_woentr, f_add, f_load, f_exit,
};

rodefword(wentrset, "wentr!", f_wentrget, 0) {
    f_woentr, f_add, f_store, f_exit,
};

rodefword(wbodyget, "wbody@", f_wentrset, 0) {
    f_wobody, f_add, f_load, f_exit,
};

rodefword(wbodyset, "wbody!", f_wbodyget, 0) {
    f_wobody, f_add, f_store, f_exit,
};

rodefword(wnameget, "wname@", f_wbodyset, 0) {
    f_woname, f_add, f_load, f_exit,
};

rodefword(wnameset, "wname!", f_wnameget, 0) {
    f_woname, f_add, f_store, f_exit,
};

rodefword(wattrget, "wattr@", f_wnameset, 0) {
    f_woattr, f_add, f_load,
    f_wattrmask, &f_and, f_exit,
};

rodefword(wattrset, "wattr!", f_wattrget, 0) {
    f_dup, f_woattr, f_add, f_load,
    f_wattrmask, f_bic, f_rot, f_or,
    f_swap, f_woattr, f_add, f_store,
    f_exit,
};

rodefconst(wattimmd, "wattimmd", WATTIMMD, f_wattrset, 0);

const void *w_wisimmd_false[] = {
    f_false, f_exit,
};

rodefword(wisimmd, "wisimmd", f_wattimmd, 0) {
    f_wattrget, f_wattimmd, f_and, f_branch0, w_wisimmd_false,
    f_true, f_exit,
};

rodefword(wnlenget, "wnlen@", f_wisimmd, 0) {
    f_woattr, f_add, f_load,
    f_wnlenmask, f_and, f_exit,
};

rodefword(wnlenset, "wnlen!", f_wnlenget, 0) {
    f_dup, f_woattr, f_add, f_load,
    f_wnlenmask, f_bic, f_rot, f_or,
    f_swap, f_woattr, f_add, f_store,
    f_exit,
};

const void *w_words_end[] = {
	f_drop, f_exit,
};

const void *w_words_loop[] = {
	f_dup, f_branch0, w_words_end,
	f_dup, f_wnameget, f_over, f_wnlenget,
	f_type, f_lit, ' ', f_emit,
	f_wlinkget, f_branch, w_words_loop,
};

rodefword(words, "words", f_wnlenset, 0) {
	f_latestget,
	f_branch, w_words_loop,
};

const void *w_find_fail[] = {
    f_drop, f_2drop, f_false, f_exit,
};

const void *w_find_loop[];

const void *w_find_next[] = {
    f_wlinkget, f_branch, w_find_loop,
};

const void *w_find_loop[] = {
    f_dup, f_branch0, w_find_fail,
    // addr u waddr
    f_dup, f_wnlenget,
    f_tor, f_over, f_fromr, // addr u waddr u nlen
    f_equ, f_branch0, w_find_next,
    f_dup, f_wnameget, // addr u waddr wname
    f_2over, // addr u waddr wname addr u
    f_rot, f_over, // addr u waddr addr u wname u
    f_compare, f_branch0, w_find_next,
    f_tor, f_2drop, f_fromr, f_exit,
};

// addr u
rodefword(find, "find", f_words, 0) {
    f_latestget,
    f_branch, w_find_loop,
};

// addr u
rodefword(defword, "defword", f_find, 0) {
    f_latestget, f_hereget, f_wlinkset,
    f_2lit, "call", 4, f_find, f_wentrget, f_hereget, f_wentrset, f_hereget, f_wnlenset,
    f_hereget, f_whsize, f_add, f_hereget, f_wnameset,
    f_hereget, f_wnameget, f_hereget, f_wnlenget, f_add, f_hereget, f_wbodyset,
    f_hereget, f_wnameget, f_hereget, f_wnlenget, f_cmove,
    f_lit, 0, f_hereget, f_wattrset,
    f_hereget, f_latestset, f_hereget, f_wbodyget, f_hereset,
    f_exit,
};

rodefword(readline, "readline", f_defword, 0) {
    f_;
};

void *latest = f_defword;

const void *test_echo[] = {
	f_key, f_emit, f_dchk, f_branch, test_echo,
};

const void *test_nop_hex_bye[] = {
	f_dchk, f_nop,
	f_lit, 0x0, f_hex4, f_lit, 0x9, f_hex4, f_lit, 0xA, f_hex4,
	f_lit, 0xF, f_hex4, f_lit, 0x01, f_hex8, f_lit, 0x90, f_hex8,
	f_lit, 0x0A, f_hex8, f_lit, 0xF0, f_hex8, f_lit, 0x0123, f_hex16,
	f_lit, 0x4567, f_hex16, f_lit, 0x89AB, f_hex16, f_lit, 0xCDEF, f_hex16,
	f_lit, 0x01234567, f_hex32, f_lit, 0x89ABCDEF, f_hex32,
	f_lit, 0x0123456789ABCDEF, f_hex64,
	f_lit, 0xFEDCBA9876543210, f_hex,
	f_2lit, '\n', '\r', f_emit, f_emit,
	f_dsdump,
	f_lit, 0, f_dsdump,
	f_lit, 1, f_dsdump,
	f_lit, 2, f_dsdump,
	f_lit, 3, f_dsdump,
	f_2drop, f_2drop,
	f_2lit, f_nop, ADDRSIZE , f_hex8dump,
	f_2lit, f_nop, sizeof(f_nop) / ADDRSIZE, f_hexdump,
	f_dchk,
	f_words,
	f_branch, test_echo,
	f_bye,
};

const void *test_branch[] = {
	f_lit, 1, f_branch0, -1, f_dchk,
	f_lit, 0, f_branch0, test_nop_hex_bye,
};

const void *test_execute[] = {
	f_lit, f_nop, f_execute, f_dchk,
	f_branch, test_branch,
};

const void *test_false_true[] = {
	f_true, f_branch0, -1, f_dchk,
	f_false, f_branch0, test_execute,
};

const void *test_equ[] = {
	f_lit, 0, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 0, f_equ, f_branch0, test_false_true,
};

const void *test_invert[] = {
	f_lit, 0, f_invert, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_invert, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, -2, f_invert, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_equ,
};

const void *test_add[] = {
	f_lit, 0,  f_lit, 1,  f_add, f_lit, 1,  f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 1,  f_add, f_lit, 0,  f_equ, f_branch0, -1, f_dchk,
	f_lit, 1,  f_lit, 1,  f_add, f_lit, 2,  f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 0,  f_add, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_lit, -2, f_lit, -1, f_add, f_lit, -3, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_invert,
};

const void *test_inc[] = {
	f_lit, 0,  f_inc, f_lit, 1,  f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_inc, f_lit, 0,  f_equ, f_branch0, -1, f_dchk,
	f_lit, -2, f_inc, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1,  f_inc, f_lit, 2,  f_equ, f_branch0, -1, f_dchk,
	f_branch, test_add,
};

const void *test_sub[] = {
	f_lit, 0, f_lit, 1, f_sub, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_sub, f_lit, 0, f_equ, f_branch0, -1,  f_dchk,
	f_lit, -1, f_lit, 1, f_sub, f_lit, -2, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 0, f_sub, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_inc,
};

const void *test_dec[] = {
	f_lit, 0, f_dec, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_dec, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_dec, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_dec, f_lit, -2, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_sub,
};

const void *test_mul[] = {
	f_lit, 1, f_lit, 1, f_mul, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 0, f_mul, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_mul, f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 2, f_mul, f_lit, 4, f_equ, f_branch0, -1, f_dchk,
	f_lit, 4, f_lit, 0, f_mul, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_sub,
};

const void *test_lshift[] = {
	f_lit, 1, f_lit, 0, f_lshift, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_lshift, f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_lshift, f_lit, 4, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 3, f_lshift, f_lit, 8, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 4, f_lshift, f_lit, 16, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_mul
};

const void *test_rshift[] = {
	f_lit, 16, f_lit, 0, f_rshift, f_lit, 16, f_equ, f_branch0, -1, f_dchk,
	f_lit, 16, f_lit, 1, f_rshift, f_lit, 8, f_equ, f_branch0, -1, f_dchk,
	f_lit, 16, f_lit, 2, f_rshift, f_lit, 4, f_equ, f_branch0, -1, f_dchk,
	f_lit, 16, f_lit, 3, f_rshift, f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_lit, 16, f_lit, 4, f_rshift, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_lshift,
};

const void *test_and[] = {
	f_lit, 1, f_lit, 0, f_and, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_and, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_and, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_and, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_rshift,
};

const void *test_or[] = {
	f_lit, 1, f_lit, 0, f_or, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_or, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_or, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_or, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_and,
};

const void *test_xor[] = {
	f_lit, 1, f_lit, 0, f_xor, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_xor, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_xor, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_xor, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_or,
};

const void *test_div[] = {
	f_lit, 1, f_lit, 1, f_div, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_div, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 1, f_div, f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_lit, 4, f_lit, 2, f_div, f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_xor,
};

const void *test_bic[] = {
	f_lit, 0x81, f_lit, 0x01, f_bic, f_lit, 0x80, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0x81, f_lit, 0x80, f_bic, f_lit, 0x01, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_div,
};

const void *test_depth_drop[] = {
	f_depth, f_lit, 0, f_equ, f_branch0, -1,
	f_lit, 5, f_depth, f_lit, 1, f_equ, f_branch0, -1,
	f_drop, f_depth, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_bic,
};

const void *test_dup[] = {
	f_lit, 2, f_dup,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_depth_drop,
};

const void *test_swap[] = {
	f_lit, 1, f_lit, 2, f_swap,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_dup,
};

const void *test_over[] = {
	f_lit, 1, f_lit, 2, f_over,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_swap,
};

const void *test_rot[] = {
	f_lit, 1, f_lit, 2, f_lit, 3, f_rot,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_over,
};

const void *test_2lit[] = {
	f_2lit, 1, 2,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_rot,
};

const void *test_2drop[] = {
	f_2lit, 1, 2, f_2drop, f_dchk,
	f_branch, test_2lit,
};

const void *test_2dup[] = {
	f_2lit, 1, 2, f_2dup,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_2drop,
};

const void *test_2swap[] = {
	f_2lit, 1, 2, f_2lit, 3, 4, f_2swap,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 4, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_2dup,
};

const void *test_2over[] = {
	f_2lit, 1, 2, f_2lit, 3, 4, f_2over,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 4, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_2swap,

};

const void *test_tor_fromr[] = {
	f_lit, 1, f_tor, f_fromr,
	f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_2over,
};

const void *test_2rot[] = {
	f_2lit, 1, 2, f_2lit, 3, 4, f_2lit, 5, 6, f_2rot,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 6, f_equ, f_branch0, -1,
	f_lit, 5, f_equ, f_branch0, -1,
	f_lit, 4, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_tor_fromr,
};

const void *test_eqz[] = {
	f_lit, 0, f_eqz, f_branch0, -1, f_dchk,
	f_lit, 1, f_eqz, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_eqz, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_2rot,
};

const void *test_nez[] = {
	f_lit, 1, f_nez, f_branch0, -1, f_dchk,
	f_lit, 2, f_nez, f_branch0, -1, f_dchk,
	f_lit, 0, f_nez, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_eqz,
};

const void *test_neq[] = {
	f_lit, 1, f_lit, 0, f_neq, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_neq, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, -1, f_neq, f_branch0, -1, f_dchk,
	f_lit, -2, f_lit, -1, f_neq, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_neq, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_neq, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -1, f_neq, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_nez,

};

const void *test_lt[] = {
	f_lit, 0, f_lit, 1, f_lt, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_lt, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 1, f_lt, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 0, f_lt, f_branch0, -1, f_dchk,
	f_lit, -2, f_lit, -1, f_lt, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 0, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, -1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, -1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -2, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_neq,
};

const void *test_gt[] = {
	f_lit, 1, f_lit, 0, f_gt, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 1, f_gt, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, -1, f_gt, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, -1, f_gt, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -2, f_gt, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 1, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 0, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -2, f_lit, -1, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -1, f_gt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_lt,
};

const void *test_le[] = {
	f_lit, 0, f_lit, 1, f_le, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_le, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 1, f_le, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 0, f_le, f_branch0, -1, f_dchk,
	f_lit, -2, f_lit, -1, f_le, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 0, f_le, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 1, f_le, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, -1, f_le, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, -1, f_le, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -2, f_le, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_le, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_le, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -1, f_le, f_branch0, -1, f_dchk,
	f_branch, test_gt,
};

const void *test_ge[] = {
	f_lit, 1, f_lit, 0, f_ge, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 1, f_ge, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, -1, f_ge, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, -1, f_ge, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -2, f_ge, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 1, f_ge, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_ge, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 1, f_ge, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, 0, f_ge, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, -2, f_lit, -1, f_ge, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0, f_lit, 0, f_ge, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 1, f_ge, f_branch0, -1, f_dchk,
	f_lit, -1, f_lit, -1, f_ge, f_branch0, -1, f_dchk,
	f_branch, test_le,
};

const void *test_within[] = {
	f_lit, 2, f_lit, 1, f_lit, 3, f_within, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 2, f_lit, 3, f_within, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 2, f_lit, 3, f_within, f_branch0, -1, f_dchk,
	f_lit, 3, f_lit, 2, f_lit, 3, f_within, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 4, f_lit, 2, f_lit, 3, f_within, f_false, f_equ, f_branch0, -1, f_dchk,

	f_lit, 2, f_lit, 3, f_lit, 1, f_within, f_branch0, -1, f_dchk,
	f_lit, 1, f_lit, 3, f_lit, 2, f_within, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 2, f_lit, 3, f_lit, 2, f_within, f_branch0, -1, f_dchk,
	f_lit, 3, f_lit, 3, f_lit, 2, f_within, f_false, f_equ, f_branch0, -1, f_dchk,
	f_lit, 4, f_lit, 3, f_lit, 2, f_within, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_ge,
};

const void *test_doconst[] = {
	f_cell, f_lit, ADDRSIZE, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_within,
};

int8_t testbuf0[ADDRSIZE * 128];
int8_t testbuf1[ADDRSIZE * 128];

const void *test_load_store[] = {
	f_lit, -1, f_lit, testbuf0, f_store, f_dchk,
	f_lit, testbuf0, f_load, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0x55AA, f_lit, testbuf0, f_store, f_dchk,
	f_lit, testbuf0, f_load, f_lit, 0x55AA, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0xFF, f_lit, testbuf0, f_cstore, f_dchk,
	f_lit, testbuf0, f_cload, f_lit, 0xFF, f_equ, f_branch0, -1, f_dchk,
	f_lit, 0xAA, f_lit, testbuf0, f_cstore, f_dchk,
	f_lit, testbuf0, f_cload, f_lit, 0xAA, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_doconst,
};

const char msg0[] = "Hello Bob";
const char msg1[] = "Hello Alice";

const void *test_fill_compare_cmove[] = {
	f_2lit, testbuf0, 5, f_lit, 0x55, f_fill, f_dchk,
	//f_2lit, testbuf0, 16, f_hex8dump, f_dchk,
	f_2lit, testbuf0, 10, f_lit, 0x55, f_fill, f_dchk,
	//f_2lit, testbuf0, 16, f_hex8dump, f_dchk,
	f_2lit, testbuf1, 10, f_lit, 0x00, f_fill, f_dchk,
	f_2lit, testbuf0, testbuf1, f_lit, 5, f_cmove, f_dchk,
	f_2lit, testbuf0, 5, f_2lit, testbuf1, 5, f_compare, f_branch0, -1,
	f_2lit, testbuf0, 6, f_2lit, testbuf1, 5, f_compare, f_branch0, -1,
	f_2lit, testbuf0, 5, f_2lit, testbuf1, 6, f_compare, f_branch0, -1,
	f_2lit, testbuf0, 6, f_2lit, testbuf1, 6, f_compare, f_false,
	f_equ, f_branch0, -1,
	//f_2lit, testbuf0, 8, f_hex8dump, f_dchk,
	//f_2lit, testbuf1, 8, f_hex8dump, f_dchk,

	f_2lit, 0, 0, f_2lit, 0, 0, f_compare, f_false, f_equ, f_branch0, -1,
	f_2lit, 0, 5, f_2lit, 0, 0, f_compare, f_false, f_equ, f_branch0, -1,
	f_2lit, 0, 0, f_2lit, 0, 5, f_compare, f_false, f_equ, f_branch0, -1,
	f_2lit, msg0, 6, f_2lit, msg1, 6, f_compare, f_branch0, -1,
	f_2lit, msg0, 6, f_2lit, msg1, 7, f_compare, f_branch0, -1,
	f_2lit, msg0, 7, f_2lit, msg1, 6, f_compare, f_branch0, -1,
	f_2lit, msg0, 7, f_2lit, msg1, 7, f_compare, f_false, f_equ, f_branch0, -1,
	f_branch, test_load_store,
};

const void *test_find[] = {
	f_2lit, "nop", 3, f_find, f_lit, f_nop, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_fill_compare_cmove,
};

const void *test_defword[] = {
	f_2lit, "nul", 3, f_defword, f_dchk,
	f_2lit, "false", 5, f_find, f_hereget, f_store,
	f_hereget, f_cell, f_add, f_hereset,
	f_2lit, "exit", 4, f_find, f_hereget, f_store,
	f_hereget, f_cell, f_add, f_hereset,
	f_2lit, "nul", 3, f_find, f_execute,
	f_false, f_equ, f_branch0, -1,
	f_branch, test_find,
};

const void *human_boot[] = {
	f_yield, f_branch, test_defword,
};

const void *dog_boot[] = {
	f_feedog, f_yield, f_branch, dog_boot,
};

void *task_dog[];

tasknew(human, 8, 128, 128, 128, human_boot, task_dog);
tasknew(dog, 8, 128, 128, 128, dog_boot, task_human);

void forth(void) {
	up = task_human;
	taskload(up);
	NEXT;
};

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
			   EFI_SYSTEM_TABLE *SystemTable) {
	GST = SystemTable;
	GIH = ImageHandle;

	GST->ConOut->OutputString(GST->ConOut, L"Hello UEFI\n\r");

	forth();

	while(1);
}
