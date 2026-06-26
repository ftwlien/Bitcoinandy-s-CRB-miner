/* nm_fast — high-performance NeuroMorph core. Byte-identical to consensus. */
#include "nm_fast.h"
#include "nm_params.h"
#include "nm_sha256.h"
#include "nm_aes.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <x86intrin.h>
#include <cpuid.h>
#ifdef NM_PROFILE
#include <stdio.h>
#endif

#ifndef NM_LAZY_BRANCH_RESET
#define NM_LAZY_BRANCH_RESET 0
#endif

#ifndef NM_VM_AESR_INLINE
#define NM_VM_AESR_INLINE 0
#endif

enum { opIADD, opIMUL, opIMULH, opIXOR, opIROTR, opINEG, opFADD, opFMUL,
       opFDIV, opFSQRT, opLOAD, opSTORE, opCBRANCH, opAESR, opXDOM };

static inline uint64_t le64(const uint8_t *b){ uint64_t v; memcpy(&v,b,8); return v; } /* x86: LE */
static inline void put64(uint8_t *b, uint64_t v){ memcpy(b,&v,8); }
static inline uint32_t le32(const uint8_t *b){ uint32_t v; memcpy(&v,b,4); return v; }
static inline double bits2d(uint64_t x){ double d; memcpy(&d,&x,8); return d; }
static inline uint64_t d2bits(double d){ uint64_t x; memcpy(&x,&d,8); return x; }
static inline double normFloat(uint64_t x){ uint64_t b=((uint64_t)1023<<52)|(x&0x000FFFFFFFFFFFFFULL); return bits2d(b); }
static inline double fabs_bits(double d){ return bits2d(d2bits(d)&0x7FFFFFFFFFFFFFFFULL); }
static inline int bad_double(double d){
    uint64_t b=d2bits(d);
    return ((b&0x7FF0000000000000ULL)==0x7FF0000000000000ULL) || ((b&0x7FFFFFFFFFFFFFFFULL)==0);
}
static inline uint64_t rotl64(uint64_t x,int k){ unsigned s=((unsigned)k)&63u; if(!s) return x; return (x<<s)|(x>>(64-s)); }
static inline uint64_t mulhi64(uint64_t a,uint64_t b){ return (uint64_t)(((unsigned __int128)a*(unsigned __int128)b)>>64); }

/* scratch-fill dispatch: pick the fastest AES path the CPU can run, runtime.
 * Both paths emit byte-identical scratch (AES is defined per 128-bit lane, so
 * VAES-256's two-blocks-per-instruction result equals two AES-NI blocks). */
typedef void (*nm_fillfn)(const nm_epoch*, nm_lane*, const uint8_t*);
static void fill_scratch(const nm_epoch *e, nm_lane *l, const uint8_t seed[32]);
static void select_fill(void);
static inline void final_fold(nm_lane *l, uint64_t fold[8]);
static nm_fillfn g_fillfn = fill_scratch;
static int g_fill_sets_fold = 0;
static int g_has_avx2 = 0;   /* set by CPUID in select_fill; gates the AVX2 final_fold */
static int g_has_avx512 = 0; /* set by CPUID in select_fill; gates the AVX-512 final_fold */
static const char *g_fillname = "AES-NI";

#ifdef NM_PROFILE
typedef struct {
    unsigned long long hashes, setup, seed, fill, gen, init, vm, dataset, loop_fold, final_fold, digest;
} nm_prof;
static nm_prof g_prof;
static inline unsigned long long cyc(void){ return __rdtsc(); }
static inline void prof_add(unsigned long long *dst, unsigned long long t0){ *dst += cyc() - t0; }
void nm_fast_profile_reset(void){ memset(&g_prof, 0, sizeof g_prof); }
void nm_fast_profile_print(const char *tag){
    unsigned long long total = g_prof.setup + g_prof.vm + g_prof.dataset +
        g_prof.loop_fold + g_prof.final_fold + g_prof.digest;
    if(!total) total = 1;
    printf("\nPROFILE %s hashes=%llu cycles/hash=%.0f\n",
           tag ? tag : "", g_prof.hashes, g_prof.hashes ? (double)total / (double)g_prof.hashes : 0.0);
    printf("  setup      %8.2f%%\n", 100.0 * (double)g_prof.setup / (double)total);
    if(g_prof.setup){
        printf("    seed     %8.2f%% of setup\n", 100.0 * (double)g_prof.seed / (double)g_prof.setup);
        printf("    fill     %8.2f%% of setup\n", 100.0 * (double)g_prof.fill / (double)g_prof.setup);
        printf("    gen      %8.2f%% of setup\n", 100.0 * (double)g_prof.gen / (double)g_prof.setup);
        printf("    init     %8.2f%% of setup\n", 100.0 * (double)g_prof.init / (double)g_prof.setup);
    }
    printf("  vm         %8.2f%%\n", 100.0 * (double)g_prof.vm / (double)total);
    printf("  dataset    %8.2f%%\n", 100.0 * (double)g_prof.dataset / (double)total);
    printf("  loop_fold  %8.2f%%\n", 100.0 * (double)g_prof.loop_fold / (double)total);
    printf("  final_fold %8.2f%%\n", 100.0 * (double)g_prof.final_fold / (double)total);
    printf("  digest     %8.2f%%\n", 100.0 * (double)g_prof.digest / (double)total);
}
#define PROF_T0(name) unsigned long long name = cyc()
#define PROF_ADD(field, start) prof_add(&g_prof.field, start)
#define PROF_HASHES(n) do{ g_prof.hashes += (unsigned long long)(n); }while(0)
#else
#define PROF_T0(name) do{}while(0)
#define PROF_ADD(field, start) do{}while(0)
#define PROF_HASHES(n) do{}while(0)
#endif

void nm_fast_epoch_init(nm_epoch *e, const uint8_t seed[32]){
    select_fill();
    nm_derive_params(seed, &e->params);
    nm_aes128_expand(&e->aes, e->params.aes_key);
    e->dataset = NULL;
}
const char *nm_fast_fill_name(void){ select_fill(); return g_fillname; }
void nm_fast_set_dataset(nm_epoch *e, uint64_t *dataset){ e->dataset = dataset; }

int nm_fast_lane_init(nm_lane *l){
    l->scratch=(uint64_t*)malloc((size_t)NM_SCRATCH_WORDS*8);
    l->prog=(nm_instr*)malloc((size_t)NM_PROG_MAX*sizeof(nm_instr));
    l->taken=(uint8_t*)malloc(NM_PROG_MAX);
    l->taken_gen=(uint16_t*)calloc(NM_PROG_MAX,sizeof(uint16_t));
    l->taken_epoch=0;
    return (l->scratch&&l->prog&&l->taken&&l->taken_gen)?0:-1;
}
void nm_fast_lane_free(nm_lane *l){ free(l->scratch); free(l->prog); free(l->taken); free(l->taken_gen); memset(l,0,sizeof*l); }
void nm_fast_lane_attach(nm_lane *l, void *scratch){ l->scratch=(uint64_t*)scratch; }

static inline void lane_branch_reset(nm_lane *l, int loops){
    if((unsigned)l->taken_epoch + (unsigned)loops + 1u >= 0xff00u){
        memset(l->taken_gen,0,(size_t)NM_PROG_MAX*sizeof(uint16_t));
        l->taken_epoch=0;
    }
    l->taken_epoch = (uint16_t)(l->taken_epoch + (uint16_t)loops + 1u);
}

static inline uint8_t lane_branch_taken(nm_lane *l, int pc, int loop){
    uint16_t stamp = (uint16_t)(l->taken_epoch + (uint16_t)loop + 1u);
    if(l->taken_gen[pc] != stamp){
        l->taken_gen[pc] = stamp;
        l->taken[pc] = 0;
    }
    return l->taken[pc];
}

static inline void lane_branch_inc(nm_lane *l, int pc){
    l->taken[pc]++;
}

void nm_fast_build_dataset(const nm_epoch *e, uint64_t *dataset){
    nm_aes128 a; nm_aes128_expand(&a, e->params.dataset_key);
    __m128i rk[11]; for(int r=0;r<11;r++) rk[r]=_mm_loadu_si128((const __m128i*)(a.rk+16*r));
    for(uint32_t i=0;i<NM_DATASET_WORDS;i+=16){
        __m128i m[8];
        for(int j=0;j<8;j++) m[j]=_mm_xor_si128(_mm_set_epi64x(0,(long long)(uint64_t)(i+2*j)),rk[0]);
        for(int r=1;r<10;r++){ __m128i k=rk[r]; for(int j=0;j<8;j++) m[j]=_mm_aesenc_si128(m[j],k); }
        for(int j=0;j<8;j++) _mm_storeu_si128((__m128i*)&dataset[i+2*j],_mm_aesenclast_si128(m[j],rk[10]));
    }
}

/* 2 MiB scratchpad fill: inlined AES-CTR, direct __m128i stores. Way-count is the
 * number of independent AES blocks in flight (each block is independent CTR so
 * the stored bytes are identical for any width). NM_FILL_WAYS lets us A/B the
 * width on a given uarch (8 saturates Zen2's 2 AES units @ lat 4; wider can cut
 * inter-iteration bubbles on cores with a deeper OoO window). */
#ifndef NM_FILL_WAYS
#define NM_FILL_WAYS 8
#endif
static void fill_scratch(const nm_epoch *e, nm_lane *l, const uint8_t seed[32]){
    (void)e;
    uint8_t key[32],t[33]; memcpy(t,seed,32); t[32]=0x53; nm_sha256(t,33,key);
    nm_aes128 a; nm_aes128_expand(&a,key);
    __m128i rk[11]; for(int r=0;r<11;r++) rk[r]=_mm_loadu_si128((const __m128i*)(a.rk+16*r));
    uint64_t khi; memcpy(&khi,key+24,8);
    uint64_t *sc=l->scratch;
    enum { W=NM_FILL_WAYS };
    for(uint32_t i=0;i<NM_SCRATCH_WORDS;i+=2*W){
        __m128i m[W];
        for(int j=0;j<W;j++) m[j]=_mm_xor_si128(_mm_set_epi64x((long long)khi,(long long)(uint64_t)(i+2*j)),rk[0]);
        for(int r=1;r<10;r++){ __m128i k=rk[r]; for(int j=0;j<W;j++) m[j]=_mm_aesenc_si128(m[j],k); }
        __m128i kl=rk[10];
        for(int j=0;j<W;j++) _mm_storeu_si128((__m128i*)&sc[i+2*j], _mm_aesenclast_si128(m[j],kl));
    }
}

#if defined(__x86_64__) || defined(_M_X64)
/* VAES-256 scratch fill: 2 AES blocks per instruction (Zen3+/Ice Lake+). Same
 * 8-blocks-per-iteration shape as the AES-NI path above, just packed two blocks
 * to a 256-bit register, so the stored bytes are identical. ~2x the AES issue
 * rate on the 49%-of-the-hash fill phase. */
__attribute__((target("avx2,vaes")))
static void fill_scratch_vaes256(const nm_epoch *e, nm_lane *l, const uint8_t seed[32]){
    (void)e;
    uint8_t key[32],t[33]; memcpy(t,seed,32); t[32]=0x53; nm_sha256(t,33,key);
    nm_aes128 a; nm_aes128_expand(&a,key);
    __m256i rk[11];
    for(int r=0;r<11;r++) rk[r]=_mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)(a.rk+16*r)));
    uint64_t khi; memcpy(&khi,key+24,8); long long kh=(long long)khi;
    uint64_t *sc=l->scratch;
    for(uint32_t i=0;i<NM_SCRATCH_WORDS;i+=16){
        __m256i y0=_mm256_xor_si256(_mm256_set_epi64x(kh,(long long)(uint64_t)(i+2 ),kh,(long long)(uint64_t)(i+0 )),rk[0]);
        __m256i y1=_mm256_xor_si256(_mm256_set_epi64x(kh,(long long)(uint64_t)(i+6 ),kh,(long long)(uint64_t)(i+4 )),rk[0]);
        __m256i y2=_mm256_xor_si256(_mm256_set_epi64x(kh,(long long)(uint64_t)(i+10),kh,(long long)(uint64_t)(i+8 )),rk[0]);
        __m256i y3=_mm256_xor_si256(_mm256_set_epi64x(kh,(long long)(uint64_t)(i+14),kh,(long long)(uint64_t)(i+12)),rk[0]);
        for(int r=1;r<10;r++){ __m256i k=rk[r];
            y0=_mm256_aesenc_epi128(y0,k); y1=_mm256_aesenc_epi128(y1,k);
            y2=_mm256_aesenc_epi128(y2,k); y3=_mm256_aesenc_epi128(y3,k); }
        __m256i kl=rk[10];
        _mm256_storeu_si256((__m256i*)&sc[i+0], _mm256_aesenclast_epi128(y0,kl));
        _mm256_storeu_si256((__m256i*)&sc[i+4], _mm256_aesenclast_epi128(y1,kl));
        _mm256_storeu_si256((__m256i*)&sc[i+8], _mm256_aesenclast_epi128(y2,kl));
        _mm256_storeu_si256((__m256i*)&sc[i+12],_mm256_aesenclast_epi128(y3,kl));
    }
}

/* VAES-512 scratch fill: 4 AES blocks per instruction on AVX-512+VAES (Zen4/Zen5
 * Genoa/Turin/Ryzen 7000+, Intel Ice Lake-SP / Sapphire Rapids / Tiger Lake+).
 * 32 blocks per iteration in 8 ZMM regs; per-128-bit-lane AES -> identical bytes.
 * clang splits 512-bit AVX-512 behind the 'evex512' feature; GCC doesn't use that
 * name, so select the right target string per compiler. */
#if defined(__clang__)
__attribute__((target("avx512f,vaes,evex512")))
#else
__attribute__((target("avx512f,vaes")))
#endif
static void fill_scratch_vaes512(const nm_epoch *e, nm_lane *l, const uint8_t seed[32]){
    (void)e;
    uint8_t key[32],t[33]; memcpy(t,seed,32); t[32]=0x53; nm_sha256(t,33,key);
    nm_aes128 a; nm_aes128_expand(&a,key);
    __m512i rk[11];
    for(int r=0;r<11;r++) rk[r]=_mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)(a.rk+16*r)));
    uint64_t khi; memcpy(&khi,key+24,8); long long kh=(long long)khi;
    uint64_t *sc=l->scratch;
    __m512i foldv=_mm512_setzero_si512();
    __m512i c0=_mm512_set_epi64(kh,6LL, kh,4LL, kh,2LL, kh,0LL);
    __m512i c1=_mm512_set_epi64(kh,14LL,kh,12LL,kh,10LL,kh,8LL);
    __m512i c2=_mm512_set_epi64(kh,22LL,kh,20LL,kh,18LL,kh,16LL);
    __m512i c3=_mm512_set_epi64(kh,30LL,kh,28LL,kh,26LL,kh,24LL);
    __m512i c4=_mm512_set_epi64(kh,38LL,kh,36LL,kh,34LL,kh,32LL);
    __m512i c5=_mm512_set_epi64(kh,46LL,kh,44LL,kh,42LL,kh,40LL);
    __m512i c6=_mm512_set_epi64(kh,54LL,kh,52LL,kh,50LL,kh,48LL);
    __m512i c7=_mm512_set_epi64(kh,62LL,kh,60LL,kh,58LL,kh,56LL);
    const __m512i bump=_mm512_set_epi64(0,64LL,0,64LL,0,64LL,0,64LL);
    for(uint32_t i=0;i<NM_SCRATCH_WORDS;i+=64){
        __m512i z0=_mm512_xor_si512(c0,rk[0]);
        __m512i z1=_mm512_xor_si512(c1,rk[0]);
        __m512i z2=_mm512_xor_si512(c2,rk[0]);
        __m512i z3=_mm512_xor_si512(c3,rk[0]);
        __m512i z4=_mm512_xor_si512(c4,rk[0]);
        __m512i z5=_mm512_xor_si512(c5,rk[0]);
        __m512i z6=_mm512_xor_si512(c6,rk[0]);
        __m512i z7=_mm512_xor_si512(c7,rk[0]);
        c0=_mm512_add_epi64(c0,bump); c1=_mm512_add_epi64(c1,bump);
        c2=_mm512_add_epi64(c2,bump); c3=_mm512_add_epi64(c3,bump);
        c4=_mm512_add_epi64(c4,bump); c5=_mm512_add_epi64(c5,bump);
        c6=_mm512_add_epi64(c6,bump); c7=_mm512_add_epi64(c7,bump);
        for(int r=1;r<10;r++){ __m512i k=rk[r];
            z0=_mm512_aesenc_epi128(z0,k); z1=_mm512_aesenc_epi128(z1,k);
            z2=_mm512_aesenc_epi128(z2,k); z3=_mm512_aesenc_epi128(z3,k);
            z4=_mm512_aesenc_epi128(z4,k); z5=_mm512_aesenc_epi128(z5,k);
            z6=_mm512_aesenc_epi128(z6,k); z7=_mm512_aesenc_epi128(z7,k); }
        __m512i kl=rk[10];
        z0=_mm512_aesenclast_epi128(z0,kl);
        z1=_mm512_aesenclast_epi128(z1,kl);
        z2=_mm512_aesenclast_epi128(z2,kl);
        z3=_mm512_aesenclast_epi128(z3,kl);
        z4=_mm512_aesenclast_epi128(z4,kl);
        z5=_mm512_aesenclast_epi128(z5,kl);
        z6=_mm512_aesenclast_epi128(z6,kl);
        z7=_mm512_aesenclast_epi128(z7,kl);
        foldv=_mm512_xor_si512(foldv,
            _mm512_xor_si512(
                _mm512_xor_si512(_mm512_xor_si512(z0,z1),_mm512_xor_si512(z2,z3)),
                _mm512_xor_si512(_mm512_xor_si512(z4,z5),_mm512_xor_si512(z6,z7))));
        _mm512_storeu_si512((void*)&sc[i+0],  z0);
        _mm512_storeu_si512((void*)&sc[i+8],  z1);
        _mm512_storeu_si512((void*)&sc[i+16], z2);
        _mm512_storeu_si512((void*)&sc[i+24], z3);
        _mm512_storeu_si512((void*)&sc[i+32], z4);
        _mm512_storeu_si512((void*)&sc[i+40], z5);
        _mm512_storeu_si512((void*)&sc[i+48], z6);
        _mm512_storeu_si512((void*)&sc[i+56], z7);
    }
    _mm512_storeu_si512((void*)l->fold,foldv);
}
#endif

/* Pick the fastest scratch-fill the CPU can run, AUTO-BENCHMARKED at startup and
 * BYTE-VERIFIED against AES-NI - so every generation runs at its max with zero
 * config and a wrong/slow path can never be chosen:
 *   AES-NI    : any AES+SSE2 x86 (Zen2/Rome, Haswell, ...)
 *   VAES-256  : VAES (Zen3+, Ice Lake+)
 *   VAES-512  : VAES + AVX-512 (Zen4/Zen5, Ice Lake-SP / Sapphire Rapids, ...)
 * CPUID gates which paths are even attempted; each candidate must reproduce the
 * AES-NI bytes exactly, then is timed (min of repeated rdtsc runs); fastest wins.
 * Override/A-B: NM_FILL=aesni|vaes256|vaes512, NM_NO_VAES=1 (force AES-NI). */
static void select_fill(void){
    static int done=0; if(done) return; done=1;

    unsigned a,b,c,d; int has_vaes=0, has_avx512=0;
    if(__get_cpuid_count(7,0,&a,&b,&c,&d)){ has_vaes=(c>>9)&1u; has_avx512=(b>>16)&1u; g_has_avx2=(b>>5)&1u; g_has_avx512=has_avx512; } /* ECX[9]=VAES, EBX[16]=AVX512F, EBX[5]=AVX2 */

    nm_fillfn fn[3]={ fill_scratch, 0, 0 };
    const char *nm[3]={ "AES-NI","VAES-256","VAES-512" };
#if defined(__x86_64__) || defined(_M_X64)
    if(has_vaes)               fn[1]=fill_scratch_vaes256;
    if(has_vaes && has_avx512) fn[2]=fill_scratch_vaes512;
#endif

    int force=-1;
    if(getenv("NM_NO_VAES")) force=0;
    { const char *e=getenv("NM_FILL");
      if(e){ if(!strcmp(e,"aesni"))force=0; else if(!strcmp(e,"vaes256"))force=1; else if(!strcmp(e,"vaes512"))force=2; } }
    if(force>=0 && force<3 && fn[force]){ g_fillfn=fn[force]; g_fillname=nm[force]; g_fill_sets_fold=(force==2); return; }

    uint64_t *sc =(uint64_t*)malloc((size_t)NM_SCRATCH_WORDS*8);
    uint64_t *ref=(uint64_t*)malloc((size_t)NM_SCRATCH_WORDS*8);
    if(!sc||!ref){ free(sc); free(ref); g_fillfn=fill_scratch; g_fillname="AES-NI"; g_fill_sets_fold=0; return; }
    nm_lane lane; lane.scratch=sc; lane.prog=0; lane.taken=0;
    nm_epoch dummy; memset(&dummy,0,sizeof dummy);
    uint8_t seed[32]; for(int i=0;i<32;i++) seed[i]=(uint8_t)(i*11u+3u);
    fill_scratch(&dummy,&lane,seed); memcpy(ref,sc,(size_t)NM_SCRATCH_WORDS*8);

    int best=0; unsigned long long bestcyc=~0ull;
    for(int k=0;k<3;k++){
        if(!fn[k]) continue;
        fn[k](&dummy,&lane,seed);
        if(memcmp(sc,ref,(size_t)NM_SCRATCH_WORDS*8)!=0) continue;        /* reject wrong bytes */
        unsigned long long bc=~0ull;
        uint64_t fold_tmp[8];
        for(int rep=0;rep<4;rep++){
            unsigned long long t0=__rdtsc();
            fn[k](&dummy,&lane,seed); if(k!=2) final_fold(&lane,fold_tmp);
            fn[k](&dummy,&lane,seed); if(k!=2) final_fold(&lane,fold_tmp);
            unsigned long long t1=__rdtsc();
            if(t1-t0<bc) bc=t1-t0;
        }
        if(bc<bestcyc){ bestcyc=bc; best=k; }
    }
    free(sc); free(ref);
    g_fillfn=fn[best]?fn[best]:fill_scratch; g_fillname=nm[best]; g_fill_sets_fold=(best==2);
}

static void gen_program(const nm_epoch *e, nm_lane *l, const uint8_t seed[32]){
    uint8_t key[32],t[33]; memcpy(t,seed,32); t[32]=0x50; nm_sha256(t,33,key);
    nm_aes128 a; nm_aes128_expand(&a,key);
    int progSize=e->params.prog_size;
    const uint8_t *ot=e->params.op_table; nm_instr *pr=l->prog;
    uint8_t in[16],o[16];
    memcpy(in,key+16,16);
    for(int i=0;i<progSize;){
        nm_aes128_encrypt(&a,in,o);
        const uint8_t *b=o;
        pr[i].op=ot[b[0]]; pr[i].dst=b[1]&15; pr[i].src=b[2]&15; pr[i].imm=le32(b+4); i++;
        if(i<progSize){
            b=o+8;
            pr[i].op=ot[b[0]]; pr[i].dst=b[1]&15; pr[i].src=b[2]&15; pr[i].imm=le32(b+4); i++;
        }
        memcpy(in,o,16);
        put64(in,le64(in)+1);
    }
}

/* run ONE outer loop of the VM (the pc instruction stream) for one lane.
 * Updates r[16] and f[8] and the lane scratchpad. Computed-goto dispatch. */
static void vm_one_loop(const nm_epoch *e, nm_lane *l, uint64_t r[16], double f[8], uint64_t fold[8], int loop){
    const int progSize=e->params.prog_size;
    const uint64_t branchMask=e->params.branch_mask, rotSalt=e->params.rot_salt;
    uint64_t *sc=l->scratch; nm_instr *prog=l->prog;
#if !NM_LAZY_BRANCH_RESET
    uint8_t *taken=l->taken;
#endif
#if NM_VM_AESR_INLINE
    __m128i aesrk[11];
    for(int ar=0;ar<11;ar++) aesrk[ar]=_mm_loadu_si128((const __m128i*)(e->aes.rk+16*ar));
#endif
    uint8_t aesIn[16],aesOut[16];
    static const void *const dtab[15]={ &&L_IADD,&&L_IMUL,&&L_IMULH,&&L_IXOR,&&L_IROTR,&&L_INEG,
        &&L_FADD,&&L_FMUL,&&L_FDIV,&&L_FSQRT,&&L_LOAD,&&L_STORE,&&L_CBRANCH,&&L_AESR,&&L_XDOM };
#if !NM_LAZY_BRANCH_RESET
    memset(taken,0,progSize);
#endif
    int pc=0; nm_instr *ins; int d,s; uint32_t imm;
    #define DISP() do{ ins=&prog[pc]; d=ins->dst; s=ins->src; imm=ins->imm; goto *dtab[ins->op]; }while(0)
    #define ADV()  do{ if(++pc>=progSize) goto Lend; DISP(); }while(0)
    if(pc<progSize){ DISP(); } else goto Lend;
    L_IADD:  r[d]+=r[s]+(uint64_t)imm; ADV();
    L_IMUL:  r[d]*=r[s]|1ULL; ADV();
    L_IMULH: r[d]=mulhi64(r[d],r[s])^(uint64_t)imm; ADV();
    L_IXOR:  r[d]^=r[s]+rotSalt; ADV();
    L_IROTR: r[d]=rotl64(r[d],-(int)((r[s]^(uint64_t)imm)&63)); ADV();
    L_INEG:  r[d]=~r[d]+(uint64_t)imm; ADV();
    L_FADD:  f[d&7]=f[d&7]+f[s&7]; goto Lfix;
    L_FMUL:  f[d&7]=f[d&7]*f[s&7]; goto Lfix;
    L_FDIV:  f[d&7]=f[d&7]/normFloat(d2bits(f[s&7])); goto Lfix;
    L_FSQRT: f[d&7]=sqrt(fabs_bits(f[d&7]));
    Lfix:    { double v=f[d&7]; if(bad_double(v)) f[d&7]=normFloat(r[d]|1ULL); } ADV();
    L_LOAD:  { uint64_t w=((r[s]+(uint64_t)imm)>>3)&((uint64_t)NM_SCRATCH_WORDS-1); r[d]^=sc[w]; } ADV();
    L_STORE: { uint64_t w=((r[d]+(uint64_t)imm)>>3)&((uint64_t)NM_SCRATCH_WORDS-1), x=r[s]+(uint64_t)loop; sc[w]^=x; fold[w&7]^=x; } ADV();
    L_CBRANCH:
#if NM_LAZY_BRANCH_RESET
        if(((r[d]+(uint64_t)imm)&branchMask)==0 && lane_branch_taken(l,pc,loop)<8){
            lane_branch_inc(l,pc); int back=(int)(imm%31)+1; pc-=back; if(pc<0)pc=0; DISP();
        }
#else
        if(((r[d]+(uint64_t)imm)&branchMask)==0 && taken[pc]<8){ taken[pc]++; int back=(int)(imm%31)+1; pc-=back; if(pc<0)pc=0; DISP(); }
#endif
        ADV();
    L_AESR:  { uint64_t w=(((r[s]+(uint64_t)imm)>>3)&((uint64_t)NM_SCRATCH_WORDS-1))&~1ULL;
        uint64_t old0=sc[w], old1=sc[w+1];
#if NM_VM_AESR_INLINE
        __m128i m=_mm_set_epi64x((long long)old1,(long long)old0);
        m=_mm_xor_si128(m,aesrk[0]);
        for(int ar=1;ar<10;ar++) m=_mm_aesenc_si128(m,aesrk[ar]);
        m=_mm_aesenclast_si128(m,aesrk[10]);
        uint64_t aesWords[2]; _mm_storeu_si128((__m128i*)aesWords,m);
        uint64_t new0=aesWords[0], new1=aesWords[1];
#else
        put64(aesIn,old0); put64(aesIn+8,old1); nm_aes128_encrypt(&e->aes,aesIn,aesOut);
        uint64_t new0=le64(aesOut), new1=le64(aesOut+8);
#endif
        sc[w]=new0; sc[w+1]=new1; fold[w&7]^=old0^new0; fold[(w+1)&7]^=old1^new1; r[d]^=new0; } ADV();
    L_XDOM:  if((imm&1)==0) r[d]^=d2bits(f[s&7]); else f[d&7]=f[d&7]*normFloat(r[s]); ADV();
    Lend:;
    #undef DISP
    #undef ADV
}

/* per-loop fold of registers back into the scratchpad */
#if defined(__clang__)
__attribute__((target("avx512f,evex512")))
#else
__attribute__((target("avx512f")))
#endif
static inline void loop_fold_xor16_avx512(uint64_t *sc, uint32_t base, const uint64_t r[16]){
    __m512i a=_mm512_loadu_si512((const void*)(sc+base+0));
    __m512i b=_mm512_loadu_si512((const void*)(sc+base+8));
    __m512i ra=_mm512_loadu_si512((const void*)(r+0));
    __m512i rb=_mm512_loadu_si512((const void*)(r+8));
    _mm512_storeu_si512((void*)(sc+base+0), _mm512_xor_si512(a,ra));
    _mm512_storeu_si512((void*)(sc+base+8), _mm512_xor_si512(b,rb));
}
static inline void loop_fold(nm_lane *l, uint64_t r[16], double f[8], uint64_t fold[8], int loop){
    uint64_t *sc=l->scratch;
    uint32_t base=(uint32_t)(((r[0]^(uint64_t)loop*0x9E3779B97F4A7C15ULL)>>3)&((uint64_t)NM_SCRATCH_WORDS-1));
    if(g_has_avx512 && base <= (uint32_t)NM_SCRATCH_WORDS-16){
        loop_fold_xor16_avx512(sc,base,r);
    } else {
        for(int i=0;i<16;i++) sc[(base+(uint32_t)i)&((uint32_t)NM_SCRATCH_WORDS-1)]^=r[i];
    }
    int off=(int)(base&7u);
    for(int i=0;i<8;i++) fold[(off+i)&7]^=r[i]^r[i+8];
    for(int i=0;i<8;i++)  r[i+8]^=d2bits(f[i]);
}

/* XOR-fold the whole 2 MiB scratchpad into fold[8]. AVX2 path for CPUs that have
 * it, byte-identical scalar fallback for the rest (XOR is commutative, so the
 * grouping is identical regardless of vector width). Dispatched on g_has_avx2 so
 * the binary runs on non-AVX2 CPUs (Westmere/Sandy/Ivy Bridge) using AES-NI. */
__attribute__((target("avx2")))
static void final_fold_avx2(nm_lane *l, uint64_t fold[8]){
    __m256i a0=_mm256_setzero_si256(), a1=_mm256_setzero_si256();
    const __m256i *sp=(const __m256i*)l->scratch;
    for(uint32_t i=0;i<NM_SCRATCH_WORDS/4;i+=2){ a0=_mm256_xor_si256(a0,_mm256_loadu_si256(sp+i)); a1=_mm256_xor_si256(a1,_mm256_loadu_si256(sp+i+1)); }
    _mm256_storeu_si256((__m256i*)fold,a0); _mm256_storeu_si256((__m256i*)(fold+4),a1);
}
#if defined(__clang__)
__attribute__((target("avx512f,evex512")))
#else
__attribute__((target("avx512f")))
#endif
static void final_fold_avx512(nm_lane *l, uint64_t fold[8]){
    __m512i a0=_mm512_setzero_si512(), a1=_mm512_setzero_si512();
    __m512i a2=_mm512_setzero_si512(), a3=_mm512_setzero_si512();
    const __m512i *sp=(const __m512i*)l->scratch;
    for(uint32_t i=0;i<NM_SCRATCH_WORDS/8;i+=4){
        a0=_mm512_xor_si512(a0,_mm512_loadu_si512((const void*)(sp+i+0)));
        a1=_mm512_xor_si512(a1,_mm512_loadu_si512((const void*)(sp+i+1)));
        a2=_mm512_xor_si512(a2,_mm512_loadu_si512((const void*)(sp+i+2)));
        a3=_mm512_xor_si512(a3,_mm512_loadu_si512((const void*)(sp+i+3)));
    }
    a0=_mm512_xor_si512(_mm512_xor_si512(a0,a1),_mm512_xor_si512(a2,a3));
    _mm512_storeu_si512((void*)fold,a0);
}
static void final_fold_scalar(nm_lane *l, uint64_t fold[8]){
    const uint64_t *sc=l->scratch;
    for(int k=0;k<8;k++) fold[k]=0;
    for(uint32_t i=0;i<NM_SCRATCH_WORDS/4;i+=2)
        for(int k=0;k<4;k++){ fold[k]^=sc[4*i+k]; fold[4+k]^=sc[4*(i+1)+k]; }
}
static inline void final_fold(nm_lane *l, uint64_t fold[8]){
    if(g_has_avx512) final_fold_avx512(l,fold);
    else if(g_has_avx2) final_fold_avx2(l,fold);
    else final_fold_scalar(l,fold);
}

static void digest(const uint8_t seed[32], const uint64_t r[16], const double f[8],
                   const uint64_t fold[8], uint8_t out[32]){
    uint8_t buf[4+32+16*8+8*8+8*8]; int n=0;
    memcpy(buf+n,"NMv1",4); n+=4; memcpy(buf+n,seed,32); n+=32;
    for(int i=0;i<16;i++){ put64(buf+n,r[i]); n+=8; }
    for(int i=0;i<8;i++){ put64(buf+n,d2bits(f[i])); n+=8; }
    for(int i=0;i<8;i++){ put64(buf+n,fold[i]); n+=8; }
    nm_sha256(buf,n,out);
}

#ifndef NM_WORDIDX_SINGLE
#define NM_WORDIDX_SINGLE 1
#endif

#ifndef NM_DATASET_PREFETCH_HINT
#define NM_DATASET_PREFETCH_HINT _MM_HINT_T0
#endif

#ifndef NM_NO_DATASET_PREFETCH
#define NM_PREFETCH_DATASET(ptr) _mm_prefetch((const char*)(ptr), NM_DATASET_PREFETCH_HINT)
#else
#define NM_PREFETCH_DATASET(ptr) do{}while(0)
#endif

#ifndef NM_PREFETCH_SINGLE_WALK
#define NM_PREFETCH_SINGLE_WALK 0
#endif

static inline void dataset_walk64_single(const uint64_t *ds, uint64_t r[16], uint64_t idx, uint64_t loopu){
#if NM_WORDIDX_SINGLE
#define DSTEP(k) do{ uint64_t v=ds[idx]; r[(k)&15]^=v; idx=((v+r[((k)+1)&15]+loopu)>>3)&((uint64_t)NM_DATASET_WORDS-1); if(NM_PREFETCH_SINGLE_WALK) NM_PREFETCH_DATASET(&ds[idx]); }while(0)
#else
#define DSTEP(k) do{ uint64_t v=ds[idx>>3]; r[(k)&15]^=v; idx=(v+r[((k)+1)&15]+loopu)&NM_DATASET_MASK; if(NM_PREFETCH_SINGLE_WALK) NM_PREFETCH_DATASET(&ds[idx>>3]); }while(0)
#endif
#define DSTEP16(b) do{ \
    DSTEP((b)+0); DSTEP((b)+1); DSTEP((b)+2); DSTEP((b)+3); \
    DSTEP((b)+4); DSTEP((b)+5); DSTEP((b)+6); DSTEP((b)+7); \
    DSTEP((b)+8); DSTEP((b)+9); DSTEP((b)+10); DSTEP((b)+11); \
    DSTEP((b)+12); DSTEP((b)+13); DSTEP((b)+14); DSTEP((b)+15); \
}while(0)
    DSTEP16(0); DSTEP16(16); DSTEP16(32); DSTEP16(48);
#undef DSTEP16
#undef DSTEP
}

static inline void seed_of(const uint8_t *header, uint8_t seed[32]){
    uint8_t tmp[8+NM_HEADER_LEN]; memcpy(tmp,"nm-seed|",8); memcpy(tmp+8,header,NM_HEADER_LEN);
    nm_sha256(tmp,8+NM_HEADER_LEN,seed);
}

void nm_fast_hash(const nm_epoch *e, nm_lane *l, const uint8_t *header, uint64_t height, uint8_t out[32]){
    const nm_params *p=&e->params;
    PROF_T0(t_setup);
    PROF_T0(t_seed);
    uint8_t seed[32]; seed_of(header,seed);
    PROF_ADD(seed,t_seed);
    int useDS = height>=NM_DATASET_HEIGHT;
    PROF_T0(t_fill);
    g_fillfn(e,l,seed);
    PROF_ADD(fill,t_fill);
    if(!g_fill_sets_fold){ PROF_T0(t_ff0); final_fold(l,l->fold); PROF_ADD(final_fold,t_ff0); }
    PROF_T0(t_gen);
    gen_program(e,l,seed);
    PROF_ADD(gen,t_gen);
    PROF_T0(t_init);
    uint64_t r[16]; double f[8];
    for(int i=0;i<4;i++)  r[i]=le64(seed+i*8);
    for(int i=4;i<16;i++) r[i]=l->scratch[i]^p->rot_salt;
    for(int i=0;i<8;i++)  f[i]=normFloat(l->scratch[16+i]);
    PROF_ADD(init,t_init);
    PROF_ADD(setup,t_setup);
    const uint64_t rotSalt=p->rot_salt;
#if NM_LAZY_BRANCH_RESET
    lane_branch_reset(l,p->loops);
#endif
    for(int loop=0;loop<p->loops;loop++){
        PROF_T0(t_vm);
        vm_one_loop(e,l,r,f,l->fold,loop);
        PROF_ADD(vm,t_vm);
        if(useDS){
#if NM_WORDIDX_SINGLE
            uint64_t addr=((r[1]^rotSalt)>>3)&((uint64_t)NM_DATASET_WORDS-1);
#else
            uint64_t addr=(r[1]^rotSalt)&NM_DATASET_MASK;
#endif
            PROF_T0(t_ds);
            dataset_walk64_single(e->dataset,r,addr,(uint64_t)loop);
            PROF_ADD(dataset,t_ds);
        }
        PROF_T0(t_lf);
        loop_fold(l,r,f,l->fold,loop);
        PROF_ADD(loop_fold,t_lf);
    }
    PROF_T0(t_dig);
    digest(seed,r,f,l->fold,out);
    PROF_ADD(digest,t_dig);
    PROF_HASHES(1);
}

void nm_fast_hash_batch(const nm_epoch *e, nm_lane *lanes, int K,
                        const uint8_t *headers, uint64_t height, uint8_t *out){
    if(K==1){
        nm_fast_hash(e,&lanes[0],headers,height,out);
        return;
    }
    const nm_params *p=&e->params;
    const int loops=p->loops; const uint64_t rotSalt=p->rot_salt;
    const int useDS = height>=NM_DATASET_HEIGHT;
    const uint64_t *ds=e->dataset;

    uint8_t  seed[NM_FAST_MAXLANES][32];
    uint64_t r[NM_FAST_MAXLANES][16];
    double   f[NM_FAST_MAXLANES][8];

    for(int L=0;L<K;L++){
        PROF_T0(t_setup);
        const uint8_t *hdr=headers + (size_t)L*NM_HEADER_LEN;
        PROF_T0(t_seed);
        seed_of(hdr, seed[L]);
        PROF_ADD(seed,t_seed);
        PROF_T0(t_fill);
        g_fillfn(e,&lanes[L],seed[L]);
        PROF_ADD(fill,t_fill);
        if(!g_fill_sets_fold){ PROF_T0(t_ff0); final_fold(&lanes[L],lanes[L].fold); PROF_ADD(final_fold,t_ff0); }
        PROF_T0(t_gen);
        gen_program(e,&lanes[L],seed[L]);
        PROF_ADD(gen,t_gen);
        PROF_T0(t_init);
        for(int i=0;i<4;i++)  r[L][i]=le64(seed[L]+i*8);
        for(int i=4;i<16;i++) r[L][i]=lanes[L].scratch[i]^p->rot_salt;
        for(int i=0;i<8;i++)  f[L][i]=normFloat(lanes[L].scratch[16+i]);
        PROF_ADD(init,t_init);
        PROF_ADD(setup,t_setup);
#if NM_LAZY_BRANCH_RESET
        lane_branch_reset(&lanes[L],loops);
#endif
    }

    for(int loop=0;loop<loops;loop++){
        for(int L=0;L<K;L++){
            PROF_T0(t_vm);
            vm_one_loop(e,&lanes[L],r[L],f[L],lanes[L].fold,loop);
            PROF_ADD(vm,t_vm);
        }

        if(useDS){
            uint64_t addr[NM_FAST_MAXLANES];
            for(int L=0;L<K;L++) addr[L]=((r[L][1]^rotSalt)>>3)&((uint64_t)NM_DATASET_WORDS-1);
            for(int L=0;L<K;L++) NM_PREFETCH_DATASET(&ds[addr[L]]);
            /* interleave the K dependent dataset chains -> K DRAM reads in flight */
            PROF_T0(t_ds);
            for(int k=0;k<NM_DATASET_READS_PER_LOOP;k++){
                int k0=k&15, k1=(k+1)&15;
                for(int L=0;L<K;L++){
                    uint64_t v=ds[addr[L]];
                    r[L][k0]^=v;
                    addr[L]=((v + r[L][k1] + (uint64_t)loop)>>3)&((uint64_t)NM_DATASET_WORDS-1);
                    if(k+1<NM_DATASET_READS_PER_LOOP) NM_PREFETCH_DATASET(&ds[addr[L]]);
                }
            }
            PROF_ADD(dataset,t_ds);
        }
        for(int L=0;L<K;L++){
            PROF_T0(t_lf);
            loop_fold(&lanes[L],r[L],f[L],lanes[L].fold,loop);
            PROF_ADD(loop_fold,t_lf);
        }
    }

    for(int L=0;L<K;L++){
        PROF_T0(t_dig);
        digest(seed[L],r[L],f[L],lanes[L].fold, out + (size_t)L*32);
        PROF_ADD(digest,t_dig);
    }
    PROF_HASHES(K);
}
