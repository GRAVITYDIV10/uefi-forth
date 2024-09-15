#include <efi.h>
#include <efilib.h>

#define forth_t  __INTPTR_TYPE__
#define addrsize sizeof(forth_t)

forth_t task_dog[];
forth_t task_human[];

forth_t wp, xp, yp, zp;
forth_t sp, rp;
forth_t sb, st;
forth_t rb, rt;
forth_t ip, ss;
forth_t up;

enum { tinext = 0, tisp, tirp, tisb, tist, tirb, tirt, tiip, tiss, };
enum {
tonext = tinext * addrsize,
tosp = tisp * addrsize,
torp = tirp * addrsize,
tosb = tisb * addrsize,
tost = tist * addrsize,
torb = tirb * addrsize,
tort = tirt * addrsize,
toip = tiip * addrsize,
toss = tiss * addrsize,
};

enum {
sscomp = (1 << 0),
ssrove = (1 << 8),
ssrund = (1 << 9),
ssdove = (1 << 10),
ssdund = (1 << 11),
};

#define tasknew(label, gap, dsize, rsize, entry, link) \
forth_t dstk_##label[gap + dsize + gap]; \
forth_t rstk_##label[gap + rsize + gap]; \
forth_t task_##label[] = { \
[tinext] = link, \
[tisp] = &dstk_##label[gap], \
[tirp] = &rstk_##label[gap], \
[tisb] = &dstk_##label[gap], \
[tist] = &dstk_##label[gap + dsize - 1], \
[tirb] = &rstk_##label[gap], \
[tirt] = &rstk_##label[gap + rsize - 1], \
[tiip] = entry, \
[tiss] = 0, \
};

EFI_HANDLE GIH = NULL;
EFI_SYSTEM_TABLE *GST = NULL;

static inline forth_t memload(void *addr) {
        return (*((volatile forth_t *)(addr)));
}

static inline void memstore(forth_t v, void *addr) {
        *((volatile forth_t *)(addr)) = v;
}

#define taskload(addr) \
sp = memload(addr + tosp); \
rp = memload(addr + torp); \
sb = memload(addr + tosb); \
st = memload(addr + tost); \
rb = memload(addr + torb); \
rt = memload(addr + tort); \
ip = memload(addr + toip); \
ss = memload(addr + toss);

#define tasksave(addr) \
memstore(sp, addr + tosp); \
memstore(rp, addr + torp); \
memstore(sb, addr + tosb); \
memstore(st, addr + tost); \
memstore(rb, addr + torb); \
memstore(rt, addr + tort); \
memstore(ip, addr + toip); \
memstore(ss, addr + toss);

enum { wilink = 0, wientr, wibody, winame, wiattr, winlen };
enum {
wolink = wilink * addrsize,
woentr = wientr * addrsize,
wobody = wibody * addrsize,
woname = winame * addrsize,
woattr = wiattr * addrsize,
wonlen = winlen * addrsize,
whdrsize = wonlen + addrsize,
};

enum {
attrimm = (1 << 0),
};

#define wdef(label, name, link, entry, body, attr) \
forth_t f_##label[whdrsize] = { \
[wientr] = entry, \
[wilink] = link, \
[wibody] = body, \
[winame] = name, \
[wiattr] = attr, \
[winlen] = sizeof(name) - 1, \
};

wdef(dummy, "dummy", 0, 0, 0, 0);

#define wdefcode(label, name, link, attr) \
void c_##label(void); \
wdef(label, name, f_##link, c_##label, -1, attr); \
void c_##label(void) 

#define jump(addr) \
void (*jmpfunc)(void) = addr; \
return jmpfunc();

void c_next(void);
wdef(next, "next", f_dummy, c_next, -1, 0);
void c_next(void) {
	wp = memload(ip);
	ip += addrsize;
	jump(memload(wp + woentr));
}

#define next() return __attribute__((musttail)) c_next()

EFI_RUNTIME_SERVICES *GRS = NULL;

void efi_shutdown(void) {
	GRS->ResetSystem(EfiResetShutdown, 0, 0, 0);
};

wdefcode(bye, "bye", next, 0) {
        GST->ConOut->OutputString(GST->ConOut, L"BYE FORTH");
	efi_shutdown();
}

wdefcode(branch, "branch", bye, 0) {
	ip = memload(ip);
	next();
}

wdefcode(yield, "yield", branch, 0) {
	tasksave(up);
	up = memload(up + tinext);
	taskload(up);
	next();
}

void efi_feedog(void) {
	GST->BootServices->SetWatchdogTimer(120, 0, 0, NULL);
};

wdefcode(feedog, "feedog", yield, 0) {
	efi_feedog();
	next();
}

static inline void rschk(void) {
	if (rp >= rt) {
		ss |= ssrove;
	}
	if (rp < rb) {
		ss |= ssrund;
	}
}

static inline void rpush(forth_t val) {
	rschk();
	memstore(val, rp);
	rp += addrsize;
	rschk();
}

wdefcode(call, "call", feedog, 0) {
	rpush(ip);
	ip = memload(wp + wobody);
	next();
}

static inline forth_t rpop(void) {
	rschk();
	rp -= addrsize;
	rschk();
	return memload(rp);
}

wdefcode(exit, "exit", call, 0) {
	ip = rpop();
	next();
}

#define wdefword(label, name, link, attr) \
forth_t w_##label[]; \
wdef(label, name, f_##link, c_call, w_##label, attr); \
forth_t w_##label[] = 

wdefword(nop, "nop", exit, 0) {
	f_exit,
};

static inline void dschk(void) {
        if (sp >= st) {
                ss |= ssdove;
        }
        if (sp < sb) {
                ss |= ssdund;
        }
}

static inline void dpush(forth_t val) {
        dschk();
        memstore(val, sp);
        sp += addrsize;
        dschk();
}

wdefcode(lit, "lit", nop, 0) {
	dpush(memload(ip));
	ip += addrsize;
	next();
}

forth_t dpop(void) {
	dschk();
	sp -= addrsize;
	dschk();
	return memload(sp);
}

wdefcode(branch0, "branch0", lit, 0) {
	wp = dpop();
	if (wp == 0) {
		ip = memload(ip);
		next();
	}
	ip += addrsize;
	next();
}

wdefcode(doconst, "doconst", branch0, 0) {
	dpush(memload(wp + wobody));
	next();
}

#define wdefconst(label, name, value, link, attr) \
wdef(label, name, f_##link, c_doconst, value, attr);

wdefconst(true, "true", -1, doconst, 0);
wdefconst(false, "false", 0, true, 0);

wdefcode(equ, "=", false, 0) {
	if (dpop() == dpop()) {
		dpush(-1);
		next();
	};
	dpush(0);
	next();
}

wdefcode(2lit, "2lit", equ, 0) {
	dpush(memload(ip));
	ip += addrsize;
	dpush(memload(ip));
	ip += addrsize;
	next();
}

wdefcode(add, "+", 2lit, 0) {
	dpush(dpop() + dpop());
	next();
}

wdefcode(xor, "xor", add, 0) {
	dpush(dpop() ^ dpop());
	next();
}

wdefword(invert, "invert", xor, 0) {
	f_lit, -1, f_xor, f_exit,
};

wdefword(inc, "1+", invert, 0) {
	f_lit, 1, f_add, f_exit,
};

wdefword(negate, "negate", inc, 0) {
	f_invert, f_inc, f_exit,
};

wdefword(sub, "-", negate, 0) {
	f_negate, f_add, f_exit,
};

wdefcode(rshift, "rshift", sub, 0) {
	xp = dpop();
	dpush(dpop() >> xp);
	next();
}

wdefword(2div, "2/", rshift, 0) {
	f_lit, 1, f_rshift, f_exit,
};

wdefconst(cell, "cell", addrsize, 2div, 0);

forth_t w_celldiv_4[] = {
	f_cell, f_lit, 4, f_equ, f_branch0, -1,
	f_lit, 2, f_rshift, f_exit,
};

wdefword(celldiv, "cell/", cell, 0) {
	f_cell, f_lit, 8, f_equ, f_branch0, w_celldiv_4,
	f_lit, 3, f_rshift, f_exit,
};

wdefcode(spget, "sp@", celldiv, 0) {
	wp = sp;
	dpush(wp);
	next();
}

wdefcode(sbget, "sb@", spget, 0) {
	wp = sb;
	dpush(wp);
	next();
}

wdefword(depth, "depth", sbget, 0) {
	f_spget, f_sbget, f_sub, f_celldiv, f_exit,
};

wdefword(dchk, "dchk", depth, 0) {
	f_depth, f_lit, 0, f_equ, f_branch0, -1, f_exit,
};


CHAR16 contxbuf[2];

void efi_contx(CHAR16 xc) {
	contxbuf[0] = xc;
	contxbuf[1] = '\0';
	GST->ConOut->OutputString(GST->ConOut, contxbuf);
	return;
};

wdefcode(contx, "contx", dchk, 0) {
	efi_contx(dpop());
	next();
};

wdefword(emit, "emit", contx, 0) {
	f_yield, f_contx, f_yield, f_exit,
};

wdefcode(and, "and", emit, 0) {
	dpush(dpop() & dpop());
	next();
}

static inline unsigned char memload8(void *addr) {
        return (*((volatile unsigned char *)(addr)));
}

wdefcode(cload, "c@", and, 0) {
	dpush(memload8(dpop()));
	next();
}

char xdigits[] = "0123456789ABCDEF";

wdefword(hex4, "hex4", and, 0) {
	f_lit, 0xF, f_and, f_lit, xdigits, f_add, f_cload, f_emit,
	f_exit,
};

wdefcode(dup, "dup", hex4, 0) {
	wp = dpop();
	dpush(wp);
	dpush(wp);
	next();
}

wdefword(hex8, "hex8", dup, 0) {
	f_dup, f_lit, 4, f_rshift, f_hex4, f_hex4, f_exit,
};

wdefword(hex16, "hex16", hex8, 0) {
	f_dup, f_lit, 8, f_rshift, f_hex8, f_hex8, f_exit,
};

wdefword(hex32, "hex32", hex16, 0) {
	f_dup, f_lit, 16, f_rshift, f_hex16, f_hex16, f_exit,
};

wdefword(hex64, "hex64", hex32, 0) {
	f_dup, f_lit, 32, f_rshift, f_hex32, f_hex32, f_exit,
};

forth_t w_hex_16[] = {
	f_cell, f_lit, 2, f_equ, f_branch0, -1,
	f_hex16, f_exit,
};

forth_t w_hex_32[] = {
	f_cell, f_lit, 4, f_equ, f_branch0, w_hex_16,
	f_hex32, f_exit,
};

wdefword(hex, "hex", hex64, 0) {
	f_cell, f_lit, 8, f_equ, f_branch0, w_hex_32,
	f_hex64, f_exit,
};

wdefcode(swap, "swap", hex, 0) {
	wp = dpop();
	xp = dpop();
	dpush(wp);
	dpush(xp);
	next();
};

wdefcode(tor, ">r", swap, 0) {
	rpush(dpop());
	next();
};

wdefcode(fromr, "r>", tor, 0) {
	dpush(rpop());
	next();
};

wdefword(over, "over", fromr, 0) {
	f_tor, f_dup, f_fromr ,f_swap, f_exit,
};

wdefword(2dup, "2dup", over, 0) {
	f_over, f_over, f_exit,
};

wdefcode(drop, "drop", 2dup, 0) {
	dpop();
	next();
};

wdefword(2drop, "2drop", drop, 0) {
	f_drop, f_drop, f_exit,
};

wdefcode(load, "@", 2drop, 0) {
	dpush(memload(dpop()));
	next();
}

wdefcode(gt, ">", load, 0) {
	wp = dpop();
	if (dpop() > wp) {
		dpush(-1);
		next();
	};
	dpush(0);
	next();
}

forth_t w_dsdump_end[] = {
	f_2drop, f_exit,
};

forth_t w_dsdump_loop[] = {
	f_2dup, f_gt, f_branch0, w_dsdump_end,
	f_dup, f_load, f_hex, f_lit, ' ', f_emit,
	f_cell, f_add, f_branch, w_dsdump_loop,
};

wdefword(dsdump, ".s", gt, 0) {
	f_depth,
	f_lit, '(', f_emit,
	f_hex16,
	f_lit, ')', f_emit,
	f_spget, f_sbget,
	f_branch, w_dsdump_loop,
};

wdefword(dec, "1-", dsdump, 0) {
	f_lit, 1, f_sub, f_exit,
};

forth_t w_type_end[] = {
	f_2drop, f_exit,
};

wdefword(type, "type", dec, 0) {
	f_dup, f_branch0, w_type_end,
	f_swap, f_dup, f_cload, f_emit,
	f_inc, f_swap, f_dec,
	f_branch, w_type,
};

wdefword(nip, "nip", type, 0) {
	f_swap, f_drop, f_exit,
};

wdefword(lt, "lt", nip, 0) {
	f_swap, f_gt, f_exit,
};

forth_t w_min_top[] = {
	f_nip, f_exit,
};

wdefword(min, "min", lt, 0) {
	f_2dup, f_lt, f_branch0, w_min_top,
	f_drop, f_exit,
};

wdefword(rot, "rot", min, 0) {
	f_tor, f_swap, f_fromr, f_swap, f_exit,
};

forth_t w_compare_fail[] = {
	f_drop, f_2drop, f_false, f_exit,
};

forth_t w_compare_ok[] = {
	f_drop, f_2drop, f_true, f_exit,
};

forth_t w_compare_loop[] = {
	f_dup, f_branch0, w_compare_ok,
	f_tor, f_2dup, f_cload, f_swap, f_cload,
	f_fromr, f_rot, f_rot,
	f_equ, f_branch0, w_compare_fail,
	f_rot, f_inc, f_rot, f_inc, f_rot, f_dec,
	f_branch, w_compare_loop,
};

wdefword(compare, "compare", type, 0) {
	f_rot, f_min, f_dup, f_branch0, w_compare_fail,
	f_branch, w_compare_loop,
};

void *latest;

wdefconst(latest, "latest", &latest, compare, 0);

wdefword(latestget, "latest@", latest, 0) {
	f_latest, f_load, f_exit,
};

wdefconst(wolink, "wolink", wolink, latest, 0);

wdefword(wlinkget, "wlink@", wolink, 0) {
	f_wolink, f_add, f_load, f_exit,
};

wdefconst(woname, "woname", woname, wlinkget, 0);

wdefword(wnameget, "wname@", woname, 0) {
	f_woname, f_add, f_load, f_exit,
};

wdefconst(wonlen, "wonlen", wonlen, wnameget, 0);

wdefword(wnlenget, "wnlen@", wonlen, 0) {
	f_wonlen, f_add, f_load, f_exit,
};

forth_t w_words_end[] = {
	f_drop, f_exit,
};

forth_t w_words_loop[] = {
	f_dup, f_branch0, w_words_end,
	f_dup, f_wnameget, f_over, f_wnlenget,
	f_type, f_lit, ' ', f_emit, f_wlinkget,
	f_branch, w_words_loop,
};

wdefword(words, "words", wnlenget, 0) {
	f_latestget,
	f_branch, w_words_loop,
};

wdefword(2swap, "2swap", words, 0) {
	f_rot, f_tor, f_rot, f_fromr, f_exit,
};

wdefword(2over, "2over", 2swap, 0) {
	f_tor, f_tor, f_2dup, f_fromr, f_fromr, f_2swap, f_exit,
};

forth_t w_find_fail[] = {
	f_drop, f_2drop, f_false, f_exit,
};

forth_t w_find_loop[];

forth_t w_find_next[] = {
	f_fromr, f_wlinkget,
	f_branch, w_find_loop,
};

forth_t w_find_loop[] = {
	//f_dsdump,
	//f_2lit, '\n', '\r', f_emit, f_emit,
	f_dup, f_branch0, w_find_fail,
	f_dup, f_tor, f_wnlenget, f_over, f_equ,
	f_branch0, w_find_next,
	f_fromr, f_dup, f_tor, f_wnameget, f_over,
	f_2over, f_compare, f_branch0, w_find_next,
	f_2drop, f_fromr, f_exit,
};

wdefword(find, "find", words, 0) {
	f_latestget,
	f_branch, w_find_loop,
};

wdefcode(execute, "execute", find, 0) {
	wp = dpop();
	jump(memload(wp + woentr));
}

EFI_STATUS conrx_stat = ~EFI_SUCCESS;
EFI_INPUT_KEY conrx_key;

void efi_conrx(void) {
	if (conrx_stat == EFI_SUCCESS) {
		return;
	}
	conrx_stat = GST->ConIn->ReadKeyStroke(GST->ConIn, &conrx_key);
	return;
}

wdefcode(conrxava, "conrx?", execute, 0) {
	efi_conrx();
	if (conrx_stat == EFI_SUCCESS) {
		dpush(-1);
		next();
	}
	dpush(0);
	next();
}

wdefword(conrxwait, "conrxwait", conrxava, 0) {
	f_conrxava, f_yield, f_branch0, w_conrxwait, f_exit,
};

wdefcode(conrxread, "conrxread", conrxwait, 0) {
	dpush(conrx_key.UnicodeChar);
	next();
}

wdefcode(conrxclear, "conrxclear", conrxread, 0) {
	conrx_stat = ~EFI_SUCCESS;
	next();
}

wdefword(conrx, "conrx", conrxclear, 0) {
	f_conrxwait, f_conrxread, f_conrxclear, f_exit,
};

wdefword(key, "key", conrx, 0) {
	f_conrx, f_exit,
};

#define tibsize 4096

int8_t tib[tibsize];
forth_t tip = &tib[0];
forth_t tit = &tib[tibsize - 1];

wdefconst(tib, "tib", &tib[0], key, 0);
wdefconst(tip, "tip", &tip, tib, 0);
wdefconst(tit, "tit", &tit, tib, 0);

wdefword(tipget, "tip@", tit, 0) {
	f_tip, f_load, f_exit,
};

wdefword(titget, "tit@", tipget, 0) {
	f_tit, f_load, f_exit,
};

wdefcode(store, "!", titget, 0) {
	wp = dpop();
	memstore(dpop(), wp);
	next();
};

wdefword(tipset, "tip!", store, 0) {
	f_tip, f_store, f_exit,
};

wdefword(tiprst, "tiprst", tipset, 0) {
	f_tib, f_tipset, f_exit,
};

wdefword(tiused, "tiused", tiprst, 0) {
	f_tipget, f_tib, f_sub, f_exit,
};

static inline void memstore8(uint8_t v, void *addr) {
        *((volatile uint8_t *)(addr)) = v;
}

wdefcode(cstore, "c!", tiused, 0) {
	wp = dpop();
	memstore8(dpop(), wp);
	next();
};

forth_t w_max_top[] = {
        f_nip, f_exit,
};                                                                                                                                                                wdefword(max, "max", cstore, 0) {
        f_2dup, f_gt, f_branch0, w_min_top,                                              f_drop, f_exit,
};

wdefcode(or, "or", max, 0) {
	dpush(dpop() | dpop());
	next();
}

wdefword(ge, "ge", or, 0) {
	f_2dup, f_gt, f_rot, f_rot, f_equ, f_or,
	f_exit,
};

wdefword(le, "le", ge, 0) {
	f_2dup, f_lt, f_rot, f_rot, f_equ, f_or,
	f_exit,
};

wdefword(within, "within", le, 0) {
	f_2dup, f_min, f_rot, f_rot, f_max,
	f_rot, f_dup, f_rot, f_lt,
	f_rot, f_rot, f_le,
	f_and, f_exit,
};

wdefword(tipchk, "tipchk", within, 0) {
	f_tipget, f_tib, f_titget, f_within, f_exit,
};

wdefword(tipinc, "tip1+", tipchk, 0) {
	f_tipget, f_inc, f_tipset,
	f_tipchk, f_branch0, w_tiprst,
	f_exit,
};

wdefword(tipdec, "tip1-", tipinc, 0) {
	f_tipget, f_dec, f_tipset,
	f_tipchk, f_branch0, w_tiprst,
	f_exit,
};

wdefword(tipush, "tipush", tipdec, 0) {
	f_tipget, f_cstore, f_tipinc, f_exit,
};

wdefword(tipop, "tipop", tipush, 0) {
	f_tipdec, f_tipget, f_cload, f_exit,
};

wdefword(isdelim, "isdelim", tipop, 0) {
	f_dup, f_lit, ' ', f_equ, f_swap,
	f_dup, f_lit, '\n', f_equ, f_swap,
	f_lit, '\r', f_equ,
	f_or, f_or, f_exit,
};

wdefword(isdelete, "isdelete", isdelim, 0) {
	f_dup, f_lit, '\b', f_equ, f_swap,
	f_lit, 127, f_equ,
	f_or, f_exit,
};

wdefword(isnewline, "isnewline", isdelete, 0) {
	f_dup, f_lit, '\n', f_equ, f_swap,
	f_lit, '\r', f_equ,
	f_or, f_exit,
};

forth_t w_token_end[] = {
	f_exit,
};

forth_t w_token_nl[] = {
	f_isnewline, f_branch0, w_token_end,
	f_lit, '\n', f_lit, '\r', f_emit, f_emit ,f_exit,
};

forth_t w_token_loop[];

forth_t w_token_del[] = {
	f_lit, '\b', f_emit,
	f_tipop, f_2drop, f_branch, w_token_loop,
};

forth_t w_token_loop[] = {
	f_key,
	f_dup, f_isdelete, f_invert, f_branch0, w_token_del,
	f_dup, f_emit,
	f_dup, f_isdelim, f_invert, f_branch0, w_token_nl,
	f_tipush, f_branch, w_token_loop,
};

wdefword(token, "token", isnewline, 0) {
	f_tiprst,
	f_branch, w_token_loop,
};

wdefword(isxdigit, "isxdigit", token, 0) {
	f_dup, f_2lit, '0', '9' + 1, f_within, f_swap,
	f_2lit, 'A', 'F' + 1, f_within, f_or, f_exit,
};

forth_t w_isnumber_false[] = {
	f_2drop, f_false, f_exit,
};

forth_t w_isnumber_true[] = {
	f_2drop, f_true, f_exit,
};

forth_t w_isnumber_loop[] = {
	f_dup, f_branch0, w_isnumber_true,
	f_swap, f_dup, f_cload, f_isxdigit, f_branch0, w_isnumber_false,
	f_inc, f_swap, f_dec,
	f_branch, w_isnumber_loop,
};

// (addr u) -- (bool)
wdefword(isnumber, "isnumber", isxdigit, 0) {
	f_dup, f_branch0, w_isnumber_false,
	f_branch, w_isnumber_loop,
};

wdefcode(lshift, "lshift", isnumber, 0) {
	wp = dpop();
	dpush(dpop() << wp);
	next();
}

wdefword(4mul, "4*", lshift, 0) {
	f_lit, 2, f_lshift, f_exit,
};

forth_t w_hex2num_x[] = {
	f_lit, 'A', f_sub, f_lit, 0xA, f_add,
	f_exit,
};

wdefword(hex2num, "hex2num", 4mul, 0) {
	f_dup, f_2lit, '0', '9' + 1, f_within, f_branch0, w_hex2num_x,
	f_lit, '0', f_sub, f_exit,
};

forth_t w_number_zero[] = {
	f_2drop, f_lit, 0, f_exit,
};

forth_t w_number_end[] = {
	f_2drop, f_drop, f_exit,
};

forth_t w_number_loop[] = {
	f_2swap, f_dup, f_branch0, w_number_end,
	// (out shi addr u)
	f_over, f_cload, f_hex2num, f_tor,
	f_dec, f_swap, f_inc, f_swap,
	f_2swap, f_fromr, f_over, f_lshift, f_tor,
	f_lit, 4, f_sub, f_swap, f_fromr, f_or, f_swap,
	f_branch, w_number_loop,
};

// (addr u) -- (n)
wdefword(number, "number", hex2num, 0) {
	f_dup, f_branch0, w_number_zero,
	f_lit, 0, f_over, f_dec, f_4mul,
	// (addr u out shi)
	f_branch, w_number_loop,
};

uint8_t dict[32 * 1024 * 1024];
forth_t here = &dict[0];

wdefconst(here, "here", &here, number, 0);

wdefword(hereget, "here@", here, 0) {
	f_here, f_load, f_exit,
};

wdefword(wlinkset, "wlink!", hereget, 0) {
	f_wolink, f_add, f_store, f_exit,
};

wdefconst(woentr, "woentr", woentr, wlinkset, 0);

wdefword(wentrset, "wentr!", woentr, 0) {
	f_woentr, f_add, f_store, f_exit,
};

wdefword(wentrget, "wentr@", wentrset, 0) {
	f_woentr, f_add, f_load, f_exit,
};

wdefword(wnameset, "wname!", wentrget, 0) {
	f_woname, f_add, f_store, f_exit,
};

wdefword(wnlenset, "wnlen!", wnameset, 0) {
	f_wonlen, f_add, f_store, f_exit,
};

wdefconst(wobody, "wobody", wobody, wnlenset, 0);

wdefword(wbodyget, "wbody@", wobody, 0) {
	f_wobody, f_add, f_load, f_exit,
};

wdefword(wbodyset, "wbody!", wbodyget, 0) {
	f_wobody, f_add, f_store, f_exit,
};

EFI_BOOT_SERVICES *GBS = NULL;
EFI_RNG_PROTOCOL *GRNG = NULL;

static uint64_t seed;

void srand(unsigned s) {
        seed = s-1;
}

forth_t rand(void) {
        seed = 6364136223846793005ULL*seed + 1;
        return seed>>33;
}

forth_t efi_rand(void) {
	if ((GRNG == NULL) ||
		(GRNG->GetRNG(GRNG, NULL, sizeof(seed), (void *)&seed) != EFI_SUCCESS)) {
		// some machine not support hwrng
		return (rand() | (rand() << 31));
	}
	efi_feedog();
	srand(seed);
	return seed;
}

wdefcode(rand, "rand", wbodyset, 0) {
	dpush(efi_rand());
	next();
}

EFI_TIME Time;

void efi_time(void) {
	GRS->GetTime(&Time, NULL);
};

wdefcode(date, "date", rand, 0) {
	efi_time();
	dpush(Time.Year);
	dpush(Time.Month);
	dpush(Time.Day);
	dpush(Time.Hour);
	dpush(Time.Minute);
	dpush(Time.Second);
	next();
}

// (addr u)
wdefcode(randfill, "randfill", date, 0) {
	wp = dpop();
	xp = dpop();
        if (GRNG != NULL) {
		if (GRNG->GetRNG(GRNG, NULL, wp, (void *)xp) == EFI_SUCCESS) {
			next();
		}
        }
	while (wp != 0) {
		memstore8(efi_rand(), xp);
		xp += 1; wp -= 1;
	}
	next();
}

wdefcode(cmove, "cmove", randfill, 0) {
	wp = dpop(); // u
	xp = dpop(); // dst
	GBS->CopyMem(xp, dpop(), wp);
	next();
};

wdefconst(woattr, "woattr", woattr, cmove, 0);

wdefword(wattrget, "wattr@", woattr, 0) {
	f_woattr, f_add, f_load, f_exit,
};

wdefword(wattrset, "wattr!", wattrget, 0) {
	f_woattr, f_add, f_store, f_exit,
};

wdefword(latestset, "latest!", wattrset, 0) {
	f_latest, f_store, f_exit,
};

wdefword(hereset, "here!", latestset, 0) {
	f_here, f_store, f_exit,
};

wdefconst(whdrsize, "whdrsize", whdrsize, hereset, 0);

// (addr u)
wdefword(defword, "defword", whdrsize, 0) {
	f_latestget, f_hereget, f_wlinkset,
	f_2lit, "call", 4, f_find, f_wentrget, f_hereget, f_wentrset,
	f_lit, 0, f_hereget, f_wattrset,
	f_hereget, f_wnlenset,
	f_hereget, f_wnlenget, f_hereget, f_add, f_whdrsize, f_add, f_hereget, f_wnameset,
	f_hereget, f_wnameget, f_hereget, f_wnlenget, f_cmove,
	f_hereget, f_wnameget, f_hereget, f_wnlenget, f_add, f_hereget, f_wbodyset,
	f_hereget, f_latestset,
	f_hereget, f_wbodyget, f_hereset,
	f_exit,
};

wdefword(herepush, "herepush", defword, 0) {
	f_hereget, f_store, f_hereget, f_cell, f_add, f_hereset, f_exit,
};

wdefcode(ssget, "ss@", herepush, 0) {
	dpush(ss);
	next();
}

wdefcode(ssset, "ss!", ssget, 0) {
	ss = dpop();
	next();
};

wdefconst(sscomp, "sscomp", sscomp, ssset, 0);

wdefword(compon, "]", sscomp, 0) {
	f_ssget, f_sscomp, f_or, f_ssset, f_exit,
};

wdefword(bic, "bic", compon, 0) {
	f_invert, f_and, f_exit,
};

wdefword(compoff, "[", bic, attrimm) {
	f_ssget, f_sscomp, f_bic, f_ssset, f_exit,
};

wdefword(incomp, "incomp", compoff, 0) {
	f_ssget, f_sscomp, f_and, f_sscomp, f_equ, f_exit,
};

wdefconst(attrimm, "attrimm", attrimm, incomp, 0);

wdefword(wisimm, "wisimm", attrimm, 0) {
	f_wattrget, f_attrimm, f_and, f_attrimm, f_equ, f_exit,
};

wdefword(docon, ":", wisimm, 0) {
	f_token, f_tiused, f_branch0, w_docon,
	f_tib, f_tiused, f_defword, f_compon,
	f_exit,
};

wdefword(semcol, ";", docon, attrimm) {
	f_2lit, "exit", 4, f_find, f_herepush,
	f_compoff, f_exit,
};

wdefword(ifnz, "if", semcol, attrimm) {
	f_2lit, "branch0", 7, f_find, f_herepush,
	f_hereget, f_lit, 0, f_herepush, f_exit,
};

wdefword(then, "then", ifnz, attrimm) {
	f_hereget, f_swap, f_store, f_exit,
};

wdefword(begin, "begin", then, attrimm) {
	f_hereget, f_exit,
};

wdefword(until, "until", begin, attrimm) {
	f_2lit, "branch0", 7, f_find, f_herepush, f_herepush,
	f_exit,
};

wdefword(eqz, "0=", until, 0) {
	f_lit, 0, f_equ, f_exit,
};

wdefword(nez, "0<>", eqz, 0) {
	f_eqz, f_invert, f_exit,
};

wdefconst(ssdove, "ssdove", ssdove, nez, 0);
wdefconst(ssrove, "ssrove", ssrove, ssdove, 0);
wdefconst(ssdund, "ssdund", ssdund, ssrove, 0);
wdefconst(ssrund, "ssrund", ssrund, ssdund, 0);

wdefword(neq, "<>", ssrund, 0) {
	f_equ, f_invert, f_exit,
};

wdefword(dot, ".", neq, 0) {
	f_hex, f_exit,
};

void efi_reboot(void) {
	GRS->ResetSystem(EfiResetCold, 0, 0, NULL);
}

wdefcode(reboot, "reboot", dot, 0) {
	efi_reboot();
}

const char msg_motd[] = "\n\r"\
" _____ ___  ____ _____ _   _ \n\r"\
"|  ___/ _ \\|  _ \\_   _| | | |\n\r"\
"| |_ | | | | |_) || | | |_| |\n\r"\
"|  _|| |_| |  _ < | | |  _  |\n\r"\
"|_|   \\___/|_| \\_\\|_| |_| |_|\n\r";


wdefword(motd, "motd", reboot, 0) {
	f_2lit, msg_motd, sizeof(msg_motd), f_type, f_exit,
};

EFI_GRAPHICS_OUTPUT_PROTOCOL *GGOP = NULL;

forth_t efi_fbaddr(void) {
	if (GGOP == NULL) {
		return GGOP;
	}
	return GGOP->Mode->FrameBufferBase;
}

wdefcode(fbaddr, "fbaddr", motd, 0) {
	dpush(efi_fbaddr());
	next();
}

forth_t efi_fbsize(void) {
	if (GGOP == NULL) {
		return GGOP;
	}
	return GGOP->Mode->FrameBufferSize;
}

wdefcode(fbsize, "fbsize", fbaddr, 0) {
	dpush(efi_fbsize());
	next();
}

wdefword(fbrand, "fbrand", fbsize, 0) {
	f_fbaddr, f_fbsize, f_randfill, f_exit,
};

wdefcode(fill, "fill", fbrand, 0) {
	wp = dpop(); // c
	xp = dpop(); // u
	GBS->SetMem(dpop(), xp, wp);
	next();
};

wdefword(fbblank, "fbblank", fill, 0) {
	f_fbaddr, f_fbsize, f_lit, 0, f_fill, f_exit,
};

forth_t efi_fbxmax(void) {
	if (GGOP == NULL) {
		return GGOP;
	}
	return GGOP->Mode->Info->HorizontalResolution;
}

wdefcode(fbxmax, "fbxmax", fbblank, 0) {
	dpush(efi_fbxmax());
	next();
};

forth_t efi_fbymax(void) {
	if (GGOP == NULL) {
		return GGOP;
	}
	return GGOP->Mode->Info->VerticalResolution;
}

wdefcode(fbymax, "fbymax", fbxmax, 0) {
	dpush(efi_fbymax());
	next();
};

forth_t efi_fblinesize(void) {
	if (GGOP == NULL) {
		return GGOP;
	}
	return GGOP->Mode->Info->PixelsPerScanLine;
};

wdefcode(fblinesize, "fblinesize", fbymax, 0) {
	dpush(efi_fblinesize());
	next();
};

forth_t efi_fbbpp(void) {
	// 32bit framebuffer
	return 4;
};

wdefcode(fbbpp, "fbbpp", fblinesize, 0) {
	dpush(efi_fbbpp());
	next();
};

static inline void efi_fbdraw(int x, int y, forth_t color) {
	if (GGOP == NULL) {
		return;
	}
	if ((x >= efi_fbxmax()) || (y >= efi_fbymax())) {
		return;
	}
	*((uint32_t *)(efi_fbaddr() + efi_fbbpp() * efi_fblinesize() * y
			+ efi_fbbpp() * x)) = color;
}

wdefcode(fbdraw, "fbdraw", fbbpp, 0) {
	wp = dpop();
	xp = dpop();
	efi_fbdraw(dpop(), xp, wp);
	next();
};

void efi_clr(void) {
	GST->ConOut->ClearScreen(GST->ConOut);
}

wdefcode(clr, "clr", fbdraw, 0) {
	efi_clr();
	next();
};

void efi_fbxline(int xmin, int xmax, int y, forth_t color) {
	while(xmin <= xmax) {
		efi_fbdraw(xmin, y, color);
		xmin++;
	}
}

wdefcode(fbxline, "fbxline", clr, 0) {
	wp = dpop(); // color
	xp = dpop(); // y
	rpush(xp); // y
	xp = dpop(); // xmax
	efi_fbxline(dpop(), xp, rpop(), wp);
	next();
}

void efi_fbyline(int ymin, int ymax, int x, forth_t color) {
	while(ymin <= ymax) {
		efi_fbdraw(x, ymin, color);
		ymin++;
	}
}

wdefcode(fbyline, "fbyline", fbxline, 0) {
	wp = dpop(); // color
	xp = dpop(); // x
	rpush(xp); // x
	xp = dpop(); // ymax
	efi_fbyline(dpop(), xp, rpop(), wp);
	next();
}

void efi_fbrect(int xmin, int xmax, int ymin, int ymax, forth_t color) {
	efi_fbxline(xmin, xmax, ymin, color);
	efi_fbxline(xmin, xmax, ymax, color);
	efi_fbyline(ymin, ymax, xmin, color);
	efi_fbyline(ymin, ymax, xmax, color);
}

wdefcode(fbrect, "fbrect", fbyline, 0) {
	rpush(dpop()); // color
	yp = dpop(); // ymax
	wp = dpop(); // ymin
	xp = dpop(); // xmax
	efi_fbrect(dpop(), xp, wp, yp, rpop());
	next();
}

void efi_fbsolid(int xmin, int xmax, int ymin, int ymax, forth_t color) {
	while(ymin <= ymax) {
		efi_fbxline(xmin, xmax, ymin, color);
		ymin++;
	}
}

wdefcode(fbsolid, "fbsolid", fbrect, 0) {
	rpush(dpop()); // color
	yp = dpop(); // ymax
	wp = dpop(); // ymin
	xp = dpop(); // xmax
	efi_fbsolid(dpop(), xp, wp, yp, rpop());
	next();
}

wdefword(iseven, "iseven", fbsolid, 0) {
	f_lit, 1, f_and, f_eqz, f_exit,
};

wdefword(isodd, "isodd", iseven, 0) {
	f_lit, 1, f_and, f_nez, f_exit,
};

forth_t w_fbxzebra_end[] = {
	f_drop, f_exit,
};

forth_t w_fbxzebra_loop[] = {
	f_dup, f_fbymax, f_lt, f_branch0, w_fbxzebra_end,
	f_dup, f_lit, 0, f_fbxmax, f_rot, f_dup, f_isodd, f_fbxline,
	f_inc, f_branch, w_fbxzebra_loop,
};

wdefword(fbxzebra, "fbxzebra", isodd, 0) {
	f_lit, 0, f_branch, w_fbxzebra_loop,
};

forth_t w_fbyzebra_end[] = {
	f_drop, f_exit,
};

forth_t w_fbyzebra_loop[] = {
	f_dup, f_fbxmax, f_lt, f_branch0, w_fbyzebra_end,
	f_dup, f_lit, 0, f_fbymax, f_rot, f_dup, f_isodd, f_fbyline,
	f_inc, f_branch, w_fbyzebra_loop,
};

wdefword(fbyzebra, "fbyzebra", fbxzebra, 0) {
	f_lit, 0, f_branch, w_fbyzebra_loop,
};

wdefcode(sprst, "sprst", fbyzebra, 0) {
	sp = sb;
	ss &= ~(ssdove);
	ss &= ~(ssdund);
	next();
}

forth_t w_interpret[];

char msg_notfound[] = " not found";

forth_t w_interpret_nonum[] = {
	f_sprst,
	f_tib, f_tiused, f_type,
	f_2lit, msg_notfound, sizeof(msg_notfound), f_type,
	f_branch, w_interpret,
};

forth_t w_interpret_noword[] = {
	f_drop,
	f_tib, f_tiused, f_isnumber, f_branch0, w_interpret_nonum,
	f_tib, f_tiused, f_number,
	f_incomp, f_branch0, w_interpret,
	f_2lit, "lit", 3, f_find, f_herepush,
	f_herepush, f_branch, w_interpret,
};

char msg_dserr[] = " data stack error ";

forth_t w_interpret_dserr[] = {
	f_sprst,
	f_2lit, msg_dserr, sizeof(msg_dserr), f_type,
	f_branch, w_interpret,
};

forth_t w_interpret_stkchk[] = {
	f_ssdove, f_ssdund, f_or, f_ssget, f_and, f_eqz, f_branch0, w_interpret_dserr,
	f_branch, w_interpret,
};

forth_t w_interpret_exec[] = {
	f_execute, 
	f_branch, w_interpret_stkchk,
};

wdefword(interpret, "interpret", sprst, 0) {
	f_token, f_tiused, f_branch0, w_interpret,
	f_tib, f_tiused, f_find, f_dup, f_branch0, w_interpret_noword,
	f_dup, f_wisimm, f_invert, f_branch0, w_interpret_exec,
	f_incomp, f_branch0, w_interpret_exec,
	f_herepush, f_branch, w_interpret,
};

void *latest = f_interpret;

forth_t test_loop[] = {
	//f_key, f_dup, f_hex8, f_emit, f_dchk,
	f_token,
	f_dchk,
	f_lit, '[', f_emit,
	f_tib, f_tiused, f_type,
	f_lit, ']', f_emit,
	f_yield, f_branch, test_loop,
};

forth_t test_app[] = {
	f_dchk,
	f_words,
	f_motd,
	f_interpret,
	//f_bye, -1,
	f_branch, test_loop,
};

forth_t test_neq[] = {
	f_dchk, f_2lit, 0, 1, f_neq, f_branch0, -1,
	f_dchk, f_2lit, 1, 0, f_neq, f_branch0, -1,
	f_dchk, f_2lit, 0, 0, f_neq, f_false, f_equ, f_branch0, -1,
	f_branch, test_app,
};

forth_t test_nez[] = {
	f_dchk, f_lit, 1, f_nez, f_branch0, -1,
	f_dchk, f_lit, 0, f_nez, f_false, f_equ, f_branch0, -1,
	f_branch, test_neq,
};

forth_t test_eqz[] = {
	f_dchk, f_lit, 0, f_eqz, f_branch0, -1,
	f_dchk, f_lit, 1, f_eqz, f_false, f_equ, f_branch0, -1,
	f_branch, test_nez,
};

forth_t test_attrimm[] = {
	f_dchk, f_lit, f_compoff, f_wisimm, f_branch0, -1,
	f_dchk, f_lit, f_compon, f_wisimm, f_false, f_equ, f_branch0, -1,
	f_branch, test_eqz,
};

forth_t test_sscomp[] = {
	f_dchk, f_sscomp, f_lit, sscomp, f_equ, f_branch0, -1,
	f_dchk, f_compon, f_incomp, f_branch0, -1,
	f_dchk, f_compoff, f_incomp, f_false, f_equ, f_branch0, -1,
	f_branch, test_attrimm,
};

forth_t test_bic[] = {
	f_dchk, f_2lit, 1, 1, f_bic, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 0, 1, f_bic, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 0, f_bic, f_lit, 1, f_equ, f_branch0, -1,
	f_branch, test_sscomp,
};

forth_t test_defword_herepush[] = {
	f_dchk, f_2lit, "star", 4, f_defword,
	f_dchk, f_2lit, "star", 4, f_find, f_branch0, -1,
	f_dchk, f_2lit, "lit", 3, f_find, f_herepush,
	f_dchk, f_lit, '*', f_herepush,
	f_dchk, f_2lit, "emit", 4, f_find, f_herepush,
	f_dchk, f_2lit, "exit", 4, f_find, f_herepush,
	f_dchk, f_branch, test_bic,
};

forth_t test_wattrset[] = {
	f_dchk,
	f_lit, 0x6644, f_hereget, f_wattrset,
	f_hereget, f_wattrget, f_lit, 0x6644, f_equ, f_branch0, -1,
	f_branch, test_defword_herepush,
};

uint8_t m1[128]; 
uint8_t m2[128]; 

forth_t test_cmove[] = {
	f_dchk, f_2lit, m1, 12, f_randfill,
	f_dchk, f_lit, m1, f_lit, m2, f_lit, 0, f_cmove,
	f_dchk, f_lit, m1, f_lit, m2, f_lit, 12, f_cmove,
	f_dchk, f_lit, m1 + 0, f_cload, f_lit, m2 + 0, f_cload, f_equ, f_branch0, -1,
	f_dchk, f_lit, m1 + 5, f_cload, f_lit, m2 + 5, f_cload, f_equ, f_branch0, -1,
	f_dchk, f_lit, m1 + 11, f_cload, f_lit, m2 + 11, f_cload, f_equ, f_branch0, -1,
	//f_dchk, f_lit, ' ', f_emit, f_lit, m1, f_hex, f_lit, ' ', f_emit, f_lit, m2, f_hex, f_lit, ' ', f_emit,
	f_branch, test_wattrset,
};

forth_t test_rand[] = {
	f_dchk,
	f_rand, f_rand, f_equ, f_invert, f_branch0, -1,
	f_branch, test_cmove,
};

forth_t test_wbodyset[] = {
	f_dchk, f_lit, 0x9911, f_hereget, f_wbodyset,
	f_dchk, f_hereget, f_wbodyget, f_lit, 0x9911, f_equ, f_branch0, -1,
	f_branch, test_rand,
};

forth_t test_wnlenset[] = {
	f_dchk, f_lit, 0x4433, f_hereget, f_wnlenset,
	f_dchk, f_hereget, f_wnlenget, f_lit, 0x4433, f_equ, f_branch0, -1, 
	f_branch, test_wbodyset,
};

forth_t test_wnameset[] = {
	f_dchk, f_lit, 0xFF77, f_hereget, f_wnameset,
	f_dchk, f_hereget, f_wnameget, f_lit, 0xFF77, f_equ, f_branch0, -1,
	f_branch, test_wnlenset,
};

forth_t test_wentrset[] = {
	f_dchk, f_lit, 0xAA55, f_hereget, f_wentrset,
	f_dchk, f_hereget, f_wentrget, f_lit, 0xAA55, f_equ, f_branch0, -1,
	f_branch, test_wnameset,
};

forth_t test_wlinkset[] = {
	f_dchk, f_lit, 0x55AA, f_hereget, f_wlinkset,
	f_dchk, f_hereget, f_wlinkget, f_lit, 0x55AA, f_equ, f_branch0, -1,
	f_branch, test_wentrset,
};

forth_t test_number[] = {
	f_dchk, f_2lit, "", 0, f_number, f_lit, 0x0, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "0", 1, f_number, f_lit, 0x0, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "12", 2, f_number, f_lit, 0x12, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "0123", 4, f_number, f_lit, 0x0123, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "0123456789ABCDEF", 16, f_number,
	f_lit, 0x0123456789ABCDEF, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "FEDCBA9876543210", 16, f_number,
	f_lit, 0xFEDCBA9876543210, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_wlinkset,
};

forth_t test_hex2num[] = {
	f_dchk, f_lit, '0', f_hex2num, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_lit, '9', f_hex2num, f_lit, 9, f_equ, f_branch0, -1,
	f_dchk, f_lit, 'A', f_hex2num, f_lit, 0xA, f_equ, f_branch0, -1,
	f_dchk, f_lit, 'F', f_hex2num, f_lit, 0xF, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_number,
};

forth_t test_4mul[] = {
	f_dchk, f_lit, 0, f_4mul, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_lit, 1, f_4mul, f_lit, 4, f_equ, f_branch0, -1,
	f_dchk, f_lit, 2, f_4mul, f_lit, 8, f_equ, f_branch0, -1,
	f_dchk, f_lit, 4, f_4mul, f_lit, 16, f_equ, f_branch0, -1,
	f_branch, test_hex2num,
};

forth_t test_lshift[] = {
	f_dchk, f_2lit, 1, 0, f_lshift, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 1, f_lshift, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 2, f_lshift, f_lit, 4, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 3, f_lshift, f_lit, 8, f_equ, f_branch0, -1,
	f_branch, test_4mul,
};

forth_t test_isnumber[] = {
	f_dchk, f_2lit, "", 0, f_isnumber, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, " 1", 2, f_isnumber, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "1 ", 2, f_isnumber, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, "1", 1, f_isnumber, f_branch0, -1,
	f_dchk, f_2lit, "55", 2, f_isnumber, f_branch0, -1,
	f_dchk, f_branch, test_lshift,
};

forth_t test_isxdigit[] = {
	f_dchk, f_lit, '0', f_isxdigit, f_branch0, -1,
	f_dchk, f_lit, '9', f_isxdigit, f_branch0, -1,
	f_dchk, f_lit, 'A', f_isxdigit, f_branch0, -1,
	f_dchk, f_lit, 'F', f_isxdigit, f_branch0, -1,
	f_dchk, f_lit, 'n', f_isxdigit, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, ' ', f_isxdigit, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, 'p', f_isxdigit, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, '\n', f_isxdigit, f_false, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_isnumber,
};

forth_t test_isdelete[] = {
	f_dchk, f_lit, '\b', f_isdelete, f_branch0, -1,
	f_dchk, f_lit, 127, f_isdelete, f_branch0, -1,
	f_dchk, f_lit, ' ', f_isdelete, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, 'a', f_isdelete, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, 'A', f_isdelete, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, '1', f_isdelete, f_false, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_isxdigit,
};

forth_t test_isdelim[] = {
	f_dchk, f_lit, ' ', f_isdelim, f_branch0, -1,
	f_dchk, f_lit, '\n', f_isdelim, f_branch0, -1,
	f_dchk, f_lit, '\r', f_isdelim, f_branch0, -1,
	f_dchk, f_lit, 'a', f_isdelim, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, 'A', f_isdelim, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, '1', f_isdelim, f_false, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_isdelete,
};

forth_t test_tipush_tipop[] = {
	f_dchk, f_lit, 0x55, f_tipush,
	f_dchk, f_tiused, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_tipop, f_lit, 0x55, f_equ, f_branch0, -1,
	f_dchk, f_tiused, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_isdelim,
};

forth_t test_tipinc_tipdec[] = {
	f_dchk, f_tiprst, f_tipdec, f_tipchk, f_branch0, -1,
	f_dchk, f_titget, f_dec, f_tipset, f_tipinc,
	f_tipget, f_tib, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_tipush_tipop,
};

forth_t test_tipchk[] = {
	f_dchk, f_tipchk, f_branch0, -1,
	f_dchk, f_tipget, f_dec, f_tipset, f_tipchk, f_false, f_equ, f_branch0, -1,
	f_dchk, f_tiprst, f_tipchk, f_branch0, -1,
	f_dchk, f_titget, f_tipset, f_tipchk, f_false, f_equ, f_branch0, -1,
	f_dchk, f_tiprst, f_tipchk, f_branch0, -1,
	f_branch, test_tipinc_tipdec,
};

forth_t test_within[] = {
	f_dchk, f_lit, 1, f_2lit, 2, 3, f_within, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, 3, f_2lit, 2, 3, f_within, f_false, f_equ, f_branch0, -1,
	f_dchk, f_lit, 1, f_2lit, 1, 3, f_within, f_branch0, -1,
	f_dchk, f_lit, 2, f_2lit, 1, 3, f_within, f_branch0, -1,
	f_branch, test_tipchk,
};

forth_t test_le[] = {
	f_dchk, f_2lit, 1, 2, f_le, f_branch0, -1,
	f_dchk, f_2lit, 1, 1, f_le, f_branch0, -1,
	f_dchk, f_2lit, -1, 1, f_le, f_branch0, -1,
	f_dchk, f_2lit, -2, -1, f_le, f_branch0, -1,
	f_dchk, f_2lit, 2, 1, f_le, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, -1, -2, f_le, f_false, f_equ, f_branch0, -1,
	f_branch, test_within,
};

forth_t test_ge[] = {
	f_dchk, f_2lit, 1, 1, f_ge ,f_branch0, -1,
	f_dchk, f_2lit, 1, 0, f_ge ,f_branch0, -1,
	f_dchk, f_2lit, 0, -1, f_ge ,f_branch0, -1,
	f_dchk, f_2lit, -1, -2, f_ge ,f_branch0, -1,
	f_dchk, f_2lit, 0, 1, f_ge, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 2, f_ge, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, -1, 0, f_ge, f_false, f_equ, f_branch0, -1,
	f_dchk, f_2lit, -2, -1, f_ge, f_false, f_equ, f_branch0, -1,
	f_branch, test_le,
};

forth_t test_or[] = {
	f_dchk, f_2lit, 0, 1, f_or, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 0, f_or, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 1, f_or, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 0, 0, f_or, f_lit, 0, f_equ, f_branch0, -1,
	f_branch, test_ge,
};

forth_t test_max[] = {
	f_dchk, f_2lit, 0, 1, f_max, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 1, 0, f_max, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, -1, 0, f_max, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_2lit, 0, -1, f_max, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_2lit, -2, -1, f_max, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk, f_2lit, -1, -2, f_max, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_or,
};

forth_t test_tib[] = {
	f_dchk,
	f_tib, f_lit, &tib[0], f_equ, f_branch0, -1,
	f_tipget, f_tib, f_equ, f_branch0, -1,
	f_titget, f_lit, &tib[tibsize - 1], f_equ, f_branch0, -1,
	f_lit, &tib[1], f_tipset, f_tipget, f_lit, &tib[1], f_equ, f_branch0, -1,
	f_tiprst, f_tib, f_tipget, f_equ, f_branch0, -1,
	f_tiused, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_max,
};

forth_t test_find_execute[] = {
	f_dchk,
	f_2lit, "noa", 3, f_find, f_false, f_equ, f_branch0, -1,
	f_2lit, "nop", 3, f_find, f_execute,
	f_2lit, "", 0, f_find, f_false, f_equ, f_branch0, -1,
	f_lit, 1,
	f_2lit, "drop", 4, f_find, f_execute,
	f_dchk,
	f_branch, test_tib,
};

forth_t test_2swap[] = {
	f_dchk,
	f_2lit, 1, 2, f_2lit, 3, 4, f_2swap,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 4, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_find_execute,
};

forth_t test_2over[] = {
	f_dchk,
	f_2lit, 1, 2, f_2lit, 3, 4, f_2over,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 4, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_2swap,
};

forth_t test_wname_wnlen[] = {
	f_dchk,
	f_woname, f_lit, woname, f_equ, f_branch0, -1,
	f_lit, f_next, f_wnameget,
	f_lit, f_next, f_wnlenget,
	f_2lit, "next", 4,
	f_compare, f_branch0, -1,
	f_dchk,
	f_branch, test_2over,
};

forth_t test_wlink[] = {
	f_dchk,
	f_wolink, f_lit, wolink, f_equ, f_branch0, -1,
	f_lit, f_next, f_wlinkget, f_lit, f_dummy, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_wname_wnlen,
};

forth_t test_latest[] = {
	f_dchk,
	f_latest, f_lit, &latest, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_wlink,
};

forth_t test_compare[] = {
	f_dchk,
	f_2lit, "", 0, f_2lit, "", 0, f_compare, f_false, f_equ, f_branch0, -1,
	f_2lit, "1", 1, f_2lit, "1", 1, f_compare, f_branch0, -1,
	f_2lit, "1234", 5, f_2lit, "1234", 4, f_compare, f_branch0, -1,
	f_2lit, "0123", 4, f_2lit, "1234", 5, f_compare, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_latest,
};

forth_t test_rot[] = {
	f_dchk,
	f_2lit, 1, 2, f_lit, 3, f_rot,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 3, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_compare,
};

forth_t test_min[] = {
	f_dchk,
	f_2lit, 1, 2, f_min, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 2, 1, f_min, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 1, 0, f_min, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 0, 1, f_min, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
	f_2lit, -1, 0, f_min, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 0, -1, f_min, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
	f_2lit, -2, -1, f_min, f_lit, -2, f_equ, f_branch0, -1, f_dchk,
	f_2lit, -1, -2, f_min, f_lit, -2, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_rot,
};

forth_t test_lt[] = {
	f_dchk,
	f_2lit, 0, 1, f_lt, f_branch0, -1, f_dchk,
	f_2lit, 1, 2, f_lt, f_branch0, -1, f_dchk,
	f_2lit, -1, 0, f_lt, f_branch0, -1, f_dchk,
	f_2lit, -2, -1, f_lt, f_branch0, -1, f_dchk,
	f_2lit, 0, 0, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 1, 0, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 2, 1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_2lit, 0, -1, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_2lit, -1, -2, f_lt, f_false, f_equ, f_branch0, -1, f_dchk,
	f_branch, test_min,
};

forth_t test_nip[] = {
	f_dchk,
	f_2lit, 1, 2, f_nip,
	f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_lt,
};

forth_t test_type[] = {
	f_dchk,
	f_2lit, 0, 0, f_type,
	f_dchk,
	f_2lit, "T", 1, f_type,
	f_dchk,
	f_2lit, "YPE", 3, f_type,
	f_dchk,
	f_2lit, " OK", 3, f_type,
	f_dchk,
	f_branch, test_nip,
};

forth_t test_dec[] = {
	f_dchk, f_lit, 0, f_dec, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk, f_lit, 1, f_dec, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk, f_lit, 2, f_dec, f_lit, 1, f_equ, f_branch0, -1,
	f_branch, test_type,
};

forth_t test_dsdump[] = {
	f_dchk,
	f_dsdump,
	f_lit, 1,
	f_dsdump,
	f_lit, 2,
	f_dsdump,
	f_lit, 3,
	f_dsdump,
	f_2drop, f_drop,
	f_dchk,
	f_branch, test_dec,
};

forth_t test_gt[] = {
	f_dchk,
	f_2lit, 1, 0, f_gt, f_branch0, -1,
	f_dchk,
	f_2lit, 2, 1, f_gt, f_branch0, -1,
	f_dchk,
	f_2lit, 0, -1, f_gt, f_branch0, -1,
	f_dchk,
	f_2lit, -1, -2, f_gt, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 0, f_gt, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 1, f_gt, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 2, f_gt, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, -1, 0, f_gt, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, -2, -1, f_gt, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_dsdump,
};

const forth_t testdata = 0x0123456789ABCDEF;

forth_t test_load[] = {
	f_dchk,
	f_lit, &testdata, f_load, f_lit, testdata, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_gt,
};

forth_t test_2drop[] = {
	f_dchk, f_2lit, 1, 1, f_2drop,
	f_dchk, f_branch, test_load,
};

forth_t test_drop[] = {
	f_dchk, f_lit, 1, f_drop,
	f_dchk,
	f_branch, test_2drop,
};

forth_t test_2dup[] = {
	f_dchk, f_2lit, 1, 2, f_2dup,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_drop,
};

forth_t test_over[] = {
	f_dchk, f_2lit, 1, 2, f_over,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_lit, 1, f_equ, f_branch0, -1,
	f_dchk, f_branch, test_2dup,
};

forth_t test_tor_fromr[] = {
	f_dchk, f_lit, 5, f_tor, f_dchk,
	f_fromr, f_lit, 5, f_equ, f_branch0, -1,
	f_branch, test_over,
};

forth_t test_swap[] = {
	f_dchk, f_2lit, 1, 2, f_swap,
	f_lit, 1, f_equ, f_branch0, -1,
	f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_tor_fromr,
};

forth_t test_hex[] = {
	f_dchk, f_lit, 0, f_hex4,
	f_dchk, f_lit, 9, f_hex4,
	f_dchk, f_lit, 0xA, f_hex4,
	f_dchk, f_lit, 0xF, f_hex4,
	f_dchk, f_lit, 0x12, f_hex8,
	f_dchk, f_lit, 0x81, f_hex8,
	f_dchk, f_lit, 0xFF, f_hex8,
	f_dchk, f_lit, 0xAA, f_hex8,
	f_dchk, f_lit, 0x55AA, f_hex16,
	f_dchk, f_lit, 0xFF00, f_hex16,
	f_dchk, f_lit, 0x55AAFF00, f_hex32,
	f_dchk, f_lit, 0x0123456789ABCDEF, f_hex64,
	f_dchk, f_lit, 0x0123456789ABCDEF, f_hex,
	f_branch, test_swap,
};

forth_t test_dup[] = {
	f_dchk, f_lit, 0, f_dup,
	f_lit, 0, f_equ, f_branch0, -1,
	f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_hex,
};

forth_t test_cload[] = {
	f_dchk,
	f_lit, xdigits + 0, f_cload, f_lit, '0', f_equ, f_branch0, -1,
	f_dchk,
	f_lit, xdigits + 1, f_cload, f_lit, '1', f_equ, f_branch0, -1,
	f_dchk,
	f_lit, xdigits + 10, f_cload, f_lit, 'A', f_equ, f_branch0, -1,
	f_dchk,
	f_lit, xdigits + 15, f_cload, f_lit, 'F', f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_dup,
};

forth_t test_and[] = {
	f_dchk,
	f_2lit, 0, 0, f_and, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 0, f_and, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 1, f_and, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 1, f_and, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_cload,
};

forth_t test_contx[] = {
	f_dchk,
	f_2lit, 'O', 'C',
	f_contx, f_contx, 
	f_2lit, 'T', 'N',
	f_contx, f_contx, 
	f_2lit, ' ', 'X',
	f_contx, f_contx, 
	f_2lit, 'K', 'O',
	f_contx, f_contx, 
	f_dchk,
	f_branch, test_and,
};

forth_t test_depth[] = {
	f_dchk,
	f_depth, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 0, f_depth, f_lit, 1, f_equ, f_branch0, -1,
	f_branch0, test_contx,
};

forth_t test_sbget_spget[] = {
	f_dchk,
	f_spget, f_sbget, f_sub, f_branch0, test_depth,
};

forth_t test_celldiv[] = {
	f_dchk,
	f_cell, f_celldiv, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 0, f_celldiv, f_branch0, test_sbget_spget,
};

forth_t test_cell[] = {
	f_dchk,
	f_cell, f_lit, addrsize, f_sub, f_branch0, test_celldiv,
};

forth_t test_2div[] = {
	f_dchk,
	f_lit, 4, f_2div, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 2, f_2div, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 8, f_2div, f_lit, 4, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 0, f_2div, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_cell,
};

forth_t test_rshift[] = {
	f_dchk,
	f_2lit, 4, 0, f_rshift, f_lit, 4, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 4, 1, f_rshift, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 4, 2, f_rshift, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_2div,
};

forth_t test_sub[] = {
	f_dchk,
	f_2lit, 1, 1, f_sub, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 0, f_sub, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 1, f_sub, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 2, 1, f_sub, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, -1, 1, f_sub, f_lit, -2, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 0, f_sub, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_rshift,
};

forth_t test_negate[] = {
	f_dchk,
	f_lit, 1, f_negate, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 0, f_negate, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 2, f_negate, f_lit, -2, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -1, f_negate, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -2, f_negate, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_sub,
};

forth_t test_inc[] = {
	f_dchk,
	f_lit, 1, f_inc, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 0, f_inc, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -1, f_inc, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -2, f_inc, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_negate,
};

forth_t test_invert[] = {
	f_dchk,
	f_lit, 0, f_invert, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -1, f_invert, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -2, f_invert, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 1, f_invert, f_lit, -2, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, -3, f_invert, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_lit, 2, f_invert, f_lit, -3, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_inc,
};

forth_t test_xor[] = {
	f_dchk,
	f_2lit, 0, 0, f_xor, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 1, f_xor, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 0, f_xor, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 1, f_xor, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_invert,
};

forth_t test_add[] = {
	f_dchk,
	f_2lit, 0, 0, f_add, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 1, f_add, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 0, f_add, f_lit, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 1, f_add, f_lit, 2, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, -1, f_add, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, -1, 0, f_add, f_lit, -1, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, -1, f_add, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, -1, 1, f_add, f_lit, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, -1, -1, f_add, f_lit, -2, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_xor,
};

forth_t test_2lit_equ[] = {
	f_dchk,
	f_2lit, 0, 0, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 0, f_equ, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 0, 1, f_equ, f_false, f_equ, f_branch0, -1,
	f_dchk,
	f_2lit, 1, 1, f_equ, f_branch0, -1,
	f_dchk,
	f_branch, test_add,
};

forth_t test_true_false[] = {
	f_true, f_branch0, -1,
	f_dchk,
	f_false, f_branch0, test_2lit_equ,
};

forth_t test_lit_branch0[] = {
	f_lit, 1, f_branch0, -1,
	f_dchk,
	f_lit, 0, f_branch0, test_true_false,
};

forth_t test_nop[] = {
	f_nop,
	f_dchk,
	f_branch, test_lit_branch0,
};

forth_t test_yield[] = {
	f_dchk,
	f_yield,
	f_branch, test_nop,
};

forth_t boot_human[] = {
	f_dchk, f_branch, test_yield,
};

forth_t boot_dog[] = {
	f_dchk,
	f_feedog, f_yield,
	f_branch, boot_dog,
};

tasknew(human, 8, 128, 128, boot_human, task_dog);
tasknew(dog, 0, 16, 16, boot_dog, task_human);

void forth(void) {
	up = task_human;
	taskload(up);
	next();
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
		EFI_SYSTEM_TABLE *SystemTable) {
	EFI_STATUS Status;

	GST = SystemTable;
	GIH = ImageHandle;
	GBS = GST->BootServices;
	GRS = GST->RuntimeServices;

	EFI_GUID rngGUID = EFI_RNG_PROTOCOL_GUID;
	Status = GBS->LocateProtocol(&rngGUID, NULL, (void **)&GRNG);
	if (Status != EFI_SUCCESS) {
		GST->ConOut->OutputString(GST->ConOut, L"NO HWRNG ");
		GRNG = NULL;
	}
	EFI_GUID gopGUID = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	Status = GBS->LocateProtocol(&gopGUID, NULL, (void **)&GGOP);
        if (Status != EFI_SUCCESS) {
                GST->ConOut->OutputString(GST->ConOut, L"NO GOP");
                GGOP = NULL;
        }
	Status = GGOP->SetMode(GGOP, GGOP->Mode->MaxMode - 1);
	if(EFI_ERROR(Status)) {
		Status = GGOP->SetMode(GGOP, 0);
		if(EFI_ERROR(Status)) {
			GST->ConOut->OutputString(GST->ConOut, L"GOP GET MODE FAIL");
			GGOP = NULL;
		}
	}

	efi_time();
	seed = Time.Year + Time.Month + Time.Day \
		+ Time.Hour + Time.Minute + Time.Second;

	GST->ConOut->EnableCursor(GST->ConOut, 1);
	GST->ConOut->SetAttribute(GST->ConOut,
		EFI_GREEN | EFI_BACKGROUND_BLACK);
        GST->ConOut->OutputString(GST->ConOut, L"HELLO FORTH");
	forth();
	while(1);
}
