#include "runtime.h"

/*
	the whole address space               is 256 TB
	a page directory pointer table addresses 512 GB
	a page directory               addresses   1 GB
	a page table                   addresses   2 MB
	a page                         addresses   4 KB
*/

enum {
	COREMAPSIZE   = 512,
	KERNELSTART   = 0x100000,
	E820FREE      = 1,
	E820RSVD      = 2,
	MAXE820       = 100,   // maximum number of E820 entries
	PAGESIZE      = 4096,
	LARGEPAGESIZE = 2097152,
	PAGETABLESIZE = 512,
	PAGEAVAIL     = 1,
	PAGEWRITE     = 2,
	PAGEUSER      = 4,
	PAGELARGE     = 0x80,
	ANTIPAGE      = ~(PAGESIZE - 1),
	TMPPAGESTART  = 0xffffffffffe00000LL
};

void main·fuck(int8*, uint32);
void runtime·FlushTLB();
void runtime·SetCR3(uint64*);
void runtime·InvlPG(void*);

typedef struct coreMapEntry coreMapEntry;

struct coreMapEntry {
	uint64 start, end;
};

uint64* e820map;
uint32 e820num;
coreMapEntry coremap[COREMAPSIZE];
uint32 cmsize;
uint64 memsize;
uint64 e820limits[2 * MAXE820];
uint64 runtime·highest;
uint64 *tmppagetable;
uint64 *pml4;

#define pageroundup(n) (((n) + PAGESIZE - 1) & ~(PAGESIZE - 1))

#pragma textflag 7
uint64
falloc(uint64 n)
{
	uint32 i;
	uint64 p;

	if(!n) return 0;
        for(i = 0; i < cmsize; i++) {
                if(coremap[i].end - coremap[i].start >= PAGESIZE*n) {
                        p = coremap[i].start;
                        coremap[i].start += PAGESIZE * n;
                        if(coremap[i].start == coremap[i].end) {
                                i++;
				cmsize--;
                                for(; i < cmsize; i++) coremap[i-1] = coremap[i];
                        }
                        return p;
                }
        }
	int8 s[] = "out of memory";
	main·fuck(s, sizeof(s));
        return 0;
}

void
ffree(uint64 p, uint64 n)
{
	uint32 i;

	if(!n) return;
	for(i = 0; i < cmsize && coremap[i].start <= i; i++);
	if(i && coremap[i-1].end == p) {
		coremap[i-1].end += n;
		if(p+n == coremap[i].start) {
			coremap[i-1].end = coremap[i].end;
			i++;
			cmsize--;
			for(; i < cmsize; i++) {
				coremap[i-1] = coremap[i];
			}
		}
	} else {
		if(p+n == coremap[i].start) {
			coremap[i].start -= n;
		} else {
			coreMapEntry *j;

			j = coremap + i;
			for(; i < cmsize; i++) {
				coremap[i+1] = coremap[i];
			}
			cmsize++;
			j->start = p;
			j->end = p+n;
		}
        }
}


#pragma textflag 7
void
runtime·processe820(void)
{
	uint32 i, j;
	uint64 *l, *lk, t;
	bool swapped, found;

	lk = e820limits;
	for(i = 0; i < e820num; i++) {
		e820map[3*i] = pageroundup(e820map[3*i]);
		e820map[3*i+1] &= ANTIPAGE;
		*lk++ = e820map[3*i];
		*lk++ = e820map[3*i] + e820map[3*i+1];
	}
	swapped = true;
	while(swapped) {
		swapped = false;
		for(l=e820limits + 1; l < lk; l++) {
			if(*l == *(l-1)) {
				lk--;
				t = *l;
				*l = *lk;
				*lk = t;
				swapped = true;
			}
			if(*l < *(l-1)) {
				t = *l;
				*l = *(l-1);
				*(l-1) = t;
				swapped = true;
			}
		}
	}
	cmsize = 0;
	memsize = 0;
	for(l = e820limits; l < lk-1; l++) {
		found = false;
		for(j = 0; j < e820num; j++) {
			if(*l >= e820map[3*j] && *l < e820map[3*j]+e820map[3*j+1]) {
				if(e820map[3*j+2] != E820FREE) {
					goto cont;
				} else {
					found = true;
				}
			}
		}
		if(found) {
			if(cmsize && coremap[cmsize-1].end == *l) {
				coremap[cmsize-1].end = *(l+1);
				memsize += *(l+1) - *l;
			} else {
				coremap[cmsize].start = *l;
				coremap[cmsize].end = *(l+1);
				memsize += *(l+1) - *l;
				cmsize++;
			}
		} 
	cont: ;
	}
	/* this is considered "reserved" even though being perfectly usable memory -- it's just already allocated */
	memsize += runtime·highest - KERNELSTART;
	coremap[cmsize].start = 0;
	coremap[cmsize].end = 0;
	runtime·printf("%d MB core\n", (uint32)((memsize + 524288) / 1048576));
	if(memsize < 16777216) {
		int8 s[] = "Sorry, GOFY doesn't run on toasters";
		main·fuck(s, sizeof(s));
	}
}

#pragma textflag 7
void
runtime·mapmemory(void)
{
	uint64 addr;
	uint32 pmlo, pdpo, pdo, pto;
	uint64 *pdp, *pd, *pt;

	pml4 = (uint64*) falloc(1);
	pmlo = pdpo = pdo = pto = 511;
	pdp = pd = pt = 0; // make the compiler happy
	for(addr=0; addr < runtime·highest; addr += 4096) {
		pto++;
		if(pto == 512) {
			pdo++;
			pto = 0;
			if(pdo == 512) {
				pdpo++;
				pdo = 0;
				if(pdpo == 512) {
					pmlo++;
					if(pmlo == 512) pmlo = 0;
					pdpo = 0;
					pdp = (uint64*) falloc(1);
					pml4[pmlo] = ((uint64)pdp) | PAGEAVAIL | PAGEWRITE;
				}
				pd = (uint64*) falloc(1);
				pdp[pdpo] = ((uint64) pd) | PAGEAVAIL | PAGEWRITE;
			}
			pt = (uint64*) falloc(1);
			pd[pdo] = ((uint64) pt) | PAGEAVAIL | PAGEWRITE;
		}
		pt[pto] = addr | PAGEAVAIL | PAGEWRITE;
	}
	pdp = (uint64*) falloc(1);
	pml4[511] = ((uint64) pdp) | PAGEAVAIL | PAGEWRITE;
	pd = (uint64*) falloc(1);
	pdp[511] = ((uint64) pd) | PAGEAVAIL | PAGEWRITE;
	tmppagetable = (uint64*) falloc(1);
	pd[511] = ((uint64) tmppagetable) | PAGEAVAIL | PAGEWRITE;
	runtime·SetCR3(pml4);
}

#pragma textflag 7
void
runtime·SysMemInit(void)
{
	e820num = *(uint32*)0x600;
	if(e820num == 0) {
		int8 s[] = "E820 fucked up";
		main·fuck(s, sizeof(s));
	}
	if(e820num > MAXE820) {
		int8 s[] = "E820 map too large";
		main·fuck(s, sizeof(s));
	}
	runtime·highest = pageroundup(runtime·highest);
	e820map = (uint64*) 0x608;
	e820map[3*e820num+0] = KERNELSTART;
	e820map[3*e820num+1] = runtime·highest - KERNELSTART;
	e820map[3*e820num+2] = E820RSVD;
	e820map[3*e820num+3] = 0;
	e820map[3*e820num+4] = 0x10000;
	e820map[3*e820num+5] = E820RSVD;
	e820num += 2;
	runtime·processe820();
	runtime·mapmemory();
}

uint32 tmpref[512];

#pragma textflag 7
void*
runtime·MapTmp(uint64 phys)
{
	void *r;
	uint32 i;
	if((phys & (PAGESIZE - 1)) != 0) {
		int8 s[] = "MapTmp called with invalid address";
		main·fuck(s, sizeof(s));
	}

	for(i=0;i<512;i++) {
		if((tmppagetable[i] & ANTIPAGE) == phys) {
			tmpref[i]++;
			return (void*) (TMPPAGESTART + i * PAGESIZE);
		}
	}
	for(i=0;i<512;i++) {
		if(tmpref[i] == 0) {
			tmpref[i]++;
			tmppagetable[i] = phys | PAGEAVAIL | PAGEWRITE;
			r = (void*) (TMPPAGESTART + i * PAGESIZE);
			runtime·InvlPG(r);
			return r;
		}
	}
	int8 s[] = "out of temporary pages";
	main·fuck(s, sizeof(s));
	return 0;
}

#pragma textflag 7
void
runtime·FreeTmp(void* t)
{
	uint32 i;

	if(t == 0) return;
	if(t < (void*) TMPPAGESTART) {
		int8 s[] = "FreeTmp called with invalid address";
		main·fuck(s, sizeof(s));
	}
	i = ((uint64)t - TMPPAGESTART) / PAGESIZE;
	if(tmpref[i] > 0) tmpref[i]--;
}

#pragma textflag 7
void
runtime·MapMem(uint64 pmlphys, uint64 phys, void* virt, uint32 n)
{
	uint32 pmlo, pdpo, pdo, pto;
	uint64 *pml, *pdp, *pd, *pt;

	if((uint64)phys & ~ANTIPAGE || (uint64)virt & ~ANTIPAGE) {
		int8 s[] = "MapMem called with invalid address";
		main·fuck(s, sizeof(s));
	}

	pml = runtime·MapTmp(pmlphys);
	pmlo = ((uint64)virt >> 39) & 0x1FF;
	pdpo = ((uint64)virt >> 30) & 0x1FF;
	pdo = ((uint64)virt >> 21) & 0x1FF;
	pto = ((uint64)virt >> 12) & 0x1FF;
	if((pml[pmlo] & PAGEAVAIL) == 0) pml[pmlo] = falloc(1) | PAGEAVAIL | PAGEWRITE;
	pdp = runtime·MapTmp(pml[pmlo] & ANTIPAGE);
	if((pdp[pdpo] & PAGEAVAIL) == 0) pdp[pdpo] = falloc(1) | PAGEAVAIL | PAGEWRITE;
	pd = runtime·MapTmp(pdp[pdpo] & ANTIPAGE);
	if((pd[pdo] & PAGEAVAIL) == 0) pd[pdo] = falloc(1) | PAGEAVAIL | PAGEWRITE;
	pt = runtime·MapTmp(pd[pdo] & ANTIPAGE);

	while(n--) {
		pt[pto] = phys | PAGEAVAIL | PAGEWRITE;
		pto++;
		if(pto == PAGETABLESIZE) {
			pto = 0;
			pdo++;
			runtime·FreeTmp(pt);
			if(pdo == PAGETABLESIZE) {
				pdo = 0;
				pdpo++;
				runtime·FreeTmp(pd);
				if(pdpo == PAGETABLESIZE) {
					pdpo = 0;
					pmlo++;
					runtime·FreeTmp(pdp);
					if((pml[pmlo] & PAGEAVAIL) == 0) pml[pmlo] = falloc(1) | PAGEAVAIL | PAGEWRITE;
					pdp = runtime·MapTmp(pml[pmlo] & ANTIPAGE);
				}
				if((pdp[pdpo] & PAGEAVAIL) == 0) pdp[pdpo] = falloc(1) | PAGEAVAIL | PAGEWRITE;
				pd = runtime·MapTmp(pdp[pdpo] & ANTIPAGE);
			}
			if((pd[pdo] & PAGEAVAIL) == 0) pd[pdo] = falloc(1) | PAGEAVAIL | PAGEWRITE;
			pt = runtime·MapTmp(pd[pdo] & ANTIPAGE);
		}
		phys += PAGESIZE;
	}
	runtime·FreeTmp(pml);
	runtime·FreeTmp(pdp);
	runtime·FreeTmp(pd);
	runtime·FreeTmp(pt);
}

#pragma textflag 7
void
runtime·SysFree()
{
}

#pragma textflag 7
void*
runtime·SysAlloc(uintptr n)
{
	uint64 phys;
	void* virt;

	virt = (void*) runtime·highest;
	n = (n + PAGESIZE - 1) / PAGESIZE;
	phys = falloc(n);
	runtime·highest += n * PAGESIZE;
	runtime·MapMem((uint64) pml4, phys, virt, n);
	runtime·FlushTLB();
	runtime·memclr(virt, n * PAGESIZE);
	return virt;
}
