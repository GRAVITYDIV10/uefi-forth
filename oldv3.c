#include <stdint.h>
#include <efi.h>
#include <efilib.h>

EFI_HANDLE GIH = NULL;
EFI_SYSTEM_TABLE *GST = NULL;

#define ADDRSIZE sizeof(intptr_t)

static inline __attribute__((__always_inline__)) intptr_t memload(void *addr) {
	return (*((volatile intptr_t *)(addr)));
}

static inline __attribute__((__always_inline__)) void memstore(intptr_t v, void *addr) {
	*((volatile intptr_t *)(addr)) = v;
}

intptr_t wp, xp, yp, zp;
intptr_t sp, rp;
intptr_t sb, st, rb, rt;
intptr_t up, ip, ss;

enum { SSROVE = (1 << 8), SSRUND = (1 << 9),
	SSDOVE = (1 << 10), SSDUND = (1 << 11), };

enum { TINO = 0, TISP, TIRP, TISB, TIST, TIRB, TIRT, TINP, TIIP, TISS, };
enum { TONO = TINO * ADDRSIZE,
TOSP = TISP * ADDRSIZE, TORP = TIRP * ADDRSIZE,
TOSB = TISB * ADDRSIZE, TOST = TIST * ADDRSIZE,
TORB = TIRB * ADDRSIZE, TORT = TIRT * ADDRSIZE,
TONP = TINP * ADDRSIZE, TOIP = TIIP * ADDRSIZE,
TOSS = TISS * ADDRSIZE,
};

#define tasknew(label, gapsize, dsize, rsize, entry, link) \
void *dstk_##label[gapsize + dsize + gapsize]; \
void *rstk_##label[gapsize + rsize + gapsize]; \
void *task_##label[] = { \
[TISP] = &dstk_##label[gapsize], \
[TIRP] = &rstk_##label[gapsize], \
[TISB] = &dstk_##label[gapsize], \
[TIST] = &dstk_##label[gapsize + dsize - 1], \
[TIRB] = &rstk_##label[gapsize], \
[TIRT] = &rstk_##label[gapsize + rsize - 1], \
[TINP] = link, \
[TIIP] = entry, \
[TISS] = 0, \
};

#define taskload(addr) \
sp = memload(addr + TOSP); \
rp = memload(addr + TORP); \
sb = memload(addr + TOSB); \
st = memload(addr + TOST); \
rb = memload(addr + TORB); \
rt = memload(addr + TORT); \
ip = memload(addr + TOIP); \
ss = memload(addr + TOSS);

#define tasksave(addr) \
memstore(sp, addr + TOSP); \
memstore(rp, addr + TORP); \
memstore(sb, addr + TOSB); \
memstore(st, addr + TOST); \
memstore(rb, addr + TORB); \
memstore(rt, addr + TORT); \
memstore(ip, addr + TOIP); \
memstore(ss, addr + TOSS);

enum { WILINK = 0, WIENTR, WIBODY, WINAME, WIATTR, };
enum { WOLINK = WILINK * ADDRSIZE,  WOENTR = WIENTR * ADDRSIZE, 
	WOBODY = WIBODY * ADDRSIZE, WONAME = WINAME * ADDRSIZE,
	WOATTR = WIATTR * ADDRSIZE, };
enum { WATTRMASK = (0xFF00), WNLENMASK = (0xFF) };

#define wdef(label, name, link, entry, body, attr) \
char _str_##label[] = name; \
const void *f_##label[] = { \
[WILINK] = link, \
[WIENTR] = entry, \
[WIBODY] = body, \
[WINAME] = &_str_##label[0], \
[WIATTR] = (((sizeof(_str_##label) - 1) & WNLENMASK) | \
	(attr & WATTRMASK)), \
};

wdef(dummy, "dummy", 0, 0, 0, 0);

#define wdefcode(label, name, link, attr) \
void c_##label(void); \
wdef(label, name, link, c_##label, -1, attr); \
void c_##label(void)

wdefcode(bye, "bye", f_dummy, 0) {
	return;
}

wdefcode(next, "next", f_bye, 0) {
	wp = memload(ip);
	ip += ADDRSIZE;
	void (*next_jump)(void) = memload(wp + WOENTR);
	return next_jump();
}

#define NEXT return c_next();

#define RSCHK \
if (rp > (rt - 1)) { \
	ss |= SSROVE; \
} \
if (rp < rb) { \
	ss |= SSRUND; \
}

static inline __attribute__((__always_inline__)) void rpush(intptr_t v) {
	RSCHK;
	memstore(v, rp);
	rp += ADDRSIZE;
	RSCHK;
}

static inline __attribute__((__always_inline__)) intptr_t rpop(void) {
	RSCHK;
	rp -= ADDRSIZE;
	RSCHK;
	return memload(rp);
}

wdefcode(call, "call", f_next, 0) {
	rpush(ip);
	ip = memload(wp + WOBODY);
	NEXT;
}

wdefcode(exit, "exit", f_call, 0) {
	ip = rpop();
	NEXT;
}

#define wdefword(label, name, link, attr) \
void *w_##label[]; \
wdef(label, name, link, c_call, w_##label, attr); \
void *w_##label[] =

wdefword(nop, "nop", f_exit, 0) {
f_exit,
};

wdefcode(branch, "branch", f_nop, 0) {
	ip = memload(ip);
	NEXT;
}

wdefcode(yield, "yield", f_branch, 0) {
	tasksave(up);
	up = memload(up + TONP);
	taskload(up);
	NEXT;
}

wdefcode(feeddog, "feeddog", f_yield, 0) {
	GST->BootServices->SetWatchdogTimer(2, 0, 0, NULL);
	NEXT;
}

#define DSCHK \
if (sp > (st - 1)) { \
	ss |= SSDOVE; \
} \
if (sp < sb) { \
	ss |= SSDUND; \
}

static inline __attribute__((__always_inline__)) void dpush(intptr_t v) {
	DSCHK;
	memstore(v, sp);
	sp += ADDRSIZE;
	DSCHK;
}

static inline __attribute__((__always_inline__)) intptr_t dpop(void) {
	DSCHK;
	sp -= ADDRSIZE;
	DSCHK;
	return memload(sp);
}

wdefcode(branch0, "branch0", f_feeddog, 0) {
	xp = dpop();
	yp = ip;
	ip += ADDRSIZE;
	if (xp == 0) {
		ip = memload(yp);
	}
	NEXT;
}

wdefcode(lit, "lit", f_branch0, 0) {
	xp = memload(ip);
	dpush(xp);
	ip += ADDRSIZE;
	NEXT;
}

wdefcode(drop, "drop", f_lit, 0) {
	dpop();
	NEXT;
}

wdefcode(depth, "depth", f_drop, 0) {
	dpush((sp - sb) / ADDRSIZE);
	NEXT;
}

wdefcode(equ, "=", f_depth, 0) {
	if (dpop() == dpop()) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

wdefcode(invert, "invert", f_equ, 0) {
	dpush(dpop() ^ (-1));
	NEXT;
}

wdefword(dchk, "dchk", f_invert, 0) {
f_depth, f_lit, 0, f_equ, f_branch0, -1, f_exit,
};

wdefcode(doconst, "doconst", f_dchk, 0) {
	dpush(memload(wp + WOBODY));
	NEXT;
};

#define wdefconst(label, name, value, link, attr) \
wdef(label, name, link, c_doconst, value, attr);

wdefconst(false, "false", 0, f_doconst, 0);
wdefconst(true,  "true", -1, f_false, 0);

wdefcode(dup, "dup", f_true, 0) {
	xp = dpop();
	dpush(xp);
	dpush(xp);
	NEXT;
}

wdefcode(swap, "swap", f_dup, 0) {
	xp = dpop();
	yp = dpop();
	dpush(xp);
	dpush(yp);
	NEXT;
}

wdefcode(tor, ">r", f_swap, 0) {
	rpush(dpop());
	NEXT;
}

wdefcode(fromr, "r>", f_tor, 0) {
	dpush(rpop());
	NEXT;
}

wdefword(over, "over", f_fromr, 0) {
f_tor, f_dup, f_fromr, f_swap, f_exit,
};

wdefword(rot, "rot", f_over, 0) {
f_tor, f_swap, f_fromr, f_swap, f_exit,
};

wdefcode(2lit, "2lit", f_rot, 0) {
	dpush(memload(ip));
	ip += ADDRSIZE;
	dpush(memload(ip));
	ip += ADDRSIZE;
	NEXT;
};

wdefword(2drop, "2drop", f_2lit, 0) {
f_drop, f_drop, f_exit,
};

wdefword(2dup, "2dup", f_2drop, 0) {
f_over, f_over, f_exit,
};

wdefword(2swap, "2swap", f_2dup, 0) {
f_rot, f_tor, f_rot, f_fromr, f_exit,
};

wdefword(2over, "2over", f_2swap, 0) {
f_tor, f_tor, f_2dup, f_fromr, f_fromr, f_2swap, f_exit,
};

wdefword(2rot, "2rot", f_2over, 0) {
f_tor, f_tor, f_2swap, f_fromr, f_fromr, f_2swap, f_exit,
};

wdefword(eqz, "0=", f_2rot, 0) {
f_lit, 0, f_equ, f_exit,
};

wdefword(neq, "<>", f_eqz, 0) {
f_equ, f_invert, f_exit,
};

wdefword(nez, "0<>", f_neq, 0) {
f_lit, 0, f_neq, f_exit,
};

wdefcode(lt, "<", f_nez, 0) {
	xp = dpop();
	if (dpop() < xp) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

wdefcode(gt, ">", f_lt, 0) {
	xp = dpop();
	if (dpop() > xp) {
		dpush(-1);
		NEXT;
	}
	dpush(0);
	NEXT;
}

wdefcode(or, "or", f_gt, 0) {
	dpush(dpop() | dpop());
	NEXT;
}

wdefword(le, "<=", f_or, 0) {
f_2dup, f_lt, f_rot, f_rot, f_equ, f_or, f_exit,
};

wdefword(ge, ">=", f_le, 0) {
f_2dup, f_gt, f_rot, f_rot, f_equ, f_or, f_exit,
};

wdefcode(add, "+", f_ge, 0) {
	dpush(dpop() + dpop());
	NEXT;
}

wdefcode(sub, "-", f_add, 0) {
	xp = dpop();
	dpush(dpop() - xp);
	NEXT;
}

wdefcode(lshift, "lshift", f_sub, 0) {
	xp = dpop();
	dpush(dpop() << xp);
	NEXT;
}

wdefcode(rshift, "rshift", f_lshift, 0) {
	xp = dpop();
	dpush(dpop() >> xp);
	NEXT;
}

void *loop_human[] = {
f_dchk,
f_yield, f_bye,
f_branch, loop_human,
};

void *test_rshift[] = {
};

void *test_lshift[] = {
f_dchk, f_lit, 1, f_lit, 0, f_lshift, f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_lit, 1, f_lit, 1, f_lshift, f_lit, 2, f_equ, f_branch0, -1,
f_dchk, f_lit, 0, f_lit, 1, f_lshift, f_branch0, test_rshift,
};

void *test_sub[] = {
f_dchk, f_2lit, 1, 0, f_sub, f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_2lit, 0, 1, f_sub, f_lit, -1, f_equ, f_branch0, -1,
f_dchk, f_2lit, 2, 1, f_sub, f_lit,1, f_equ, f_branch0, -1,
f_dchk, f_2lit, -1, 1, f_sub, f_lit, -2, f_equ, f_branch0, -1,
f_branch, test_lshift,
};

void *test_add[] = {
f_dchk, f_2lit, 0, 1, f_add, f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_2lit, 1, 1, f_add, f_lit, 2, f_equ, f_branch0, -1,
f_dchk, f_2lit, 1, -1, f_add, f_lit, 0, f_equ, f_branch0, -1,
f_dchk, f_2lit, -1, -1, f_add, f_lit, -2, f_equ, f_branch0, -1,
f_dchk, f_branch, test_sub,
};

void *test_ge[] = {
f_dchk, f_2lit, 0, 0, f_ge, f_branch0, -1,
f_dchk, f_2lit, 0, -1, f_ge, f_branch0, -1,
f_dchk, f_2lit, 1, 0, f_ge, f_branch0, -1,
f_dchk, f_2lit, 0, 1, f_ge, f_false, f_equ, f_branch0, -1,
f_dchk, f_branch, test_add,
};

void *test_le[] = {
f_dchk, f_2lit, 0, 0, f_le, f_branch0, -1,
f_dchk, f_2lit, -1, 0, f_le, f_branch0, -1,
f_dchk, f_2lit, 1, 0, f_le, f_false, f_equ, f_branch0, -1,
f_dchk, f_branch, test_ge,
};

void *test_or[] = {
f_dchk, f_2lit, 0, 1, f_or, f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_2lit, 1, 1, f_or, f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_2lit, 1, 0, f_or, f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_2lit, 0, 0, f_or, f_branch0, test_le,
};

void *test_gt[] = {
f_dchk, f_2lit, 2, 1, f_gt, f_branch0, -1,
f_dchk, f_2lit, 1, 0, f_gt, f_branch0, -1,
f_dchk, f_2lit, 0, -1, f_gt, f_branch0, -1,
f_dchk, f_2lit, -1, -2, f_gt, f_branch0, -1,
f_dchk, f_2lit, 1, 2, f_gt, f_false, f_equ, f_branch0, -1,
f_dchk, f_2lit, 0, 1, f_gt, f_false, f_equ, f_branch0, -1,
f_dchk, f_2lit, -2, -1, f_gt, f_false, f_equ, f_branch0, -1,
f_dchk, f_branch, test_or,
};

void *test_lt[] = {
f_dchk, f_2lit, 1, 2, f_lt, f_branch0, -1,
f_dchk, f_2lit, 1, 1, f_lt, f_false ,f_equ, f_branch0, -1,
f_dchk, f_2lit, 2, 1, f_lt, f_false, f_equ, f_branch0, -1,
f_dchk, f_2lit, -1, 0, f_lt, f_branch0, -1,
f_dchk, f_2lit, -2, -1, f_lt, f_branch0, -1,
f_dchk, f_2lit, -1, 1, f_lt, f_branch0, -1,
f_dchk, f_branch, test_gt,
};

void *test_nez[] = {
f_dchk, f_lit, 0, f_nez, f_false, f_equ, f_branch0, -1,
f_dchk, f_lit, 1, f_nez, f_branch0, -1,
f_dchk, f_branch, test_lt,
};

void *test_neq[] = {
f_dchk, f_2lit, 0, 0, f_neq, f_false, f_equ,  f_branch0, -1,
f_dchk, f_2lit, 1, 0, f_neq, f_branch0, -1,
f_dchk, f_branch, test_nez,
};

void *test_eqz[] = {
f_dchk, f_lit, 1, f_eqz, f_false, f_equ, f_branch0, -1,
f_dchk, f_lit, 0, f_eqz, f_branch0, -1,
f_dchk, f_branch, test_neq,
};

void *test_2rot[] = {
f_dchk, f_2lit, 1, 2, f_2lit, 3, 4, f_2lit, 5, 6, f_2rot,
f_lit, 2, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 6, f_equ, f_branch0, -1,
f_lit, 5, f_equ, f_branch0, -1,
f_lit, 4, f_equ, f_branch0, -1,
f_lit, 3, f_equ, f_branch0, -1,
f_dchk, f_branch, test_eqz,
};

void *test_2over[] = {
f_dchk, f_2lit, 1, 2, f_2lit, 3, 4, f_2over,
f_lit, 2, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 4, f_equ, f_branch0, -1,
f_lit, 3, f_equ, f_branch0, -1,
f_lit, 2, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_branch, test_2rot,
};

void *test_2dup[] = {
f_dchk, f_2lit, 1, 2, f_2dup,
f_lit, 2, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 2, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_branch, test_2over,
};

void *test_2drop[] = {
f_dchk, f_2lit, 1, 2, f_2drop,
f_dchk, f_branch, test_2dup,
};

void *test_2lit[] = {
f_dchk, f_2lit, 0, 1,
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 0, f_equ, f_branch0, -1,
f_dchk, f_branch, test_2drop,
};

void *test_rot[] = {
f_dchk, f_lit, 0, f_lit, 1, f_lit, 2, f_rot,
f_lit, 0, f_equ, f_branch0, -1,
f_lit, 2, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_branch, test_2lit,
};

void *test_over[] = {
f_dchk, f_lit, 0, f_lit, 1, f_over,
f_lit, 0, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 0, f_equ, f_branch0, -1,
f_dchk, f_branch, test_rot,
};

void *test_tor_fromr[] = {
f_dchk, f_lit, 5, f_tor, f_dchk, f_fromr,
f_lit, 5, f_equ, f_branch0, -1,
f_dchk, f_branch, test_over,
};

void *test_swap[] = {
f_dchk, f_lit, 1, f_lit, 2, f_swap,
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 2, f_equ, f_branch0, -1,
f_dchk, f_branch, test_tor_fromr,
};

void *test_dup[] = {
f_dchk, f_lit, 1, f_dup, 
f_lit, 1, f_equ, f_branch0, -1,
f_lit, 1, f_equ, f_branch0, -1,
f_dchk, f_lit, 0, f_dup,
f_lit, 0, f_equ, f_branch0, -1,
f_lit, 0, f_equ, f_false, f_equ, f_branch0, test_swap,
};

void *test_doconst[] = {
f_dchk, f_true, f_branch0, -1,
f_dchk, f_false, f_branch0, test_dup,
};

void *test_drop_depth_equ_invert[] = {
f_lit, 0, f_invert, f_lit, -1, f_equ, f_branch0, -1, f_dchk,
f_lit, -1, f_invert, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
f_lit, -2, f_invert, f_lit, 1, f_equ, f_branch0, -1, f_dchk,
f_lit, 0, f_lit, 0, f_equ, f_branch0, -1, f_dchk,
f_lit, 0, f_lit, 1, f_equ, f_invert, f_branch0, -1, f_dchk,
f_lit, 0, f_depth, f_lit, 1, f_equ, f_branch0, -1, f_drop, f_dchk,
f_depth, f_branch0, test_doconst,
};

void *test_lit_branch0[] = {
f_lit, 0,
f_branch0, test_drop_depth_equ_invert,
};

void *test_yield[] = {
f_yield,
f_branch, test_lit_branch0,
};

void *test_nop[] = {
f_nop,
f_branch, test_yield,
};

void *boot_human[] = {
f_branch, test_nop,
};

void *task_dog[];
tasknew(human, 8, 128, 128, boot_human, task_dog);

void *boot_dog[] = {
f_feeddog, f_yield,
f_branch, boot_dog,
};

tasknew(dog, 8, 128, 128, boot_dog, task_human);

void forth(void) {
	GST->ConOut->OutputString(GST->ConOut, L"HELLO FORTH\n\r");
	up = task_human;
	taskload(up);
	NEXT;
};

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
			   EFI_SYSTEM_TABLE *SystemTable) {
	GST = SystemTable;
	GIH = ImageHandle;
	forth();
	GST->ConOut->OutputString(GST->ConOut, L"BYE FORTH\n\r");
	while(1);
}
