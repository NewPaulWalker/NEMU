#include "local-include/reg.h"
#include "local-include/csr.h"
#include "local-include/intr.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>
#include <cpu/difftest.h>

#define R(i)  gpr(i)
#define FR(i) fpr(i)
#define Mr(addr, len)       ({ word_t tmp = vaddr_read(s, addr, len, MMU_DYNAMIC); check_ex(0); tmp; })
#define Mw(addr, len, data) vaddr_write(s, addr, len, data, MMU_DYNAMIC);
#define jcond(cond, target) do { if (cond) { cpu.pc = target; is_jmp = true; } } while (0)

bool fp_enable();
void rt_inv(Decode *s) {
  save_globals(s);
  mtval->val = s->extraInfo->isa.instr.val;
  longjmp_exception(EX_II);
}

enum {
  TYPE_R, TYPE_I, TYPE_J,
  TYPE_U, TYPE_S, TYPE_B,
  TYPE_N, // none

  TYPE_CIW, TYPE_CFLD, TYPE_CLW, TYPE_CLD, TYPE_CFSD, TYPE_CSW, TYPE_CSD,
  TYPE_CI, TYPE_CASP, TYPE_CLUI, TYPE_CSHIFT,
  TYPE_CANDI, TYPE_CS, TYPE_CJ, TYPE_CB, TYPE_CIU, TYPE_CR,
  TYPE_CFLDSP, TYPE_CLWSP, TYPE_CLDSP,
  TYPE_CFSDSP, TYPE_CSWSP, TYPE_CSDSP,

  TYPE_FLD, TYPE_FST, TYPE_FR, TYPE_FR2R, TYPE_R2FR,

  TYPE_I_JALR, TYPE_CR_JALR,
};

static word_t immI(uint32_t i) { return SEXT(BITS(i, 31, 20), 12); }
static word_t immU(uint32_t i) { return SEXT(BITS(i, 31, 12), 20) << 12; }
static word_t immS(uint32_t i) { return (SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7); }
static word_t immJ(uint32_t i) { return (SEXT(BITS(i, 31, 31), 1) << 20) |
  (BITS(i, 19, 12) << 12) | (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1);
}
static word_t immB(uint32_t i) { return (SEXT(BITS(i, 31, 31), 1) << 12) |
  (BITS(i, 7, 7) << 11) | (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1);
}

#ifdef CONFIG_PERF_OPT
#include <isa-all-instr.h>
static word_t zero_null = 0;
#define src1R(n) do { id_src1->preg = &R(n); } while (0)
#define src2R(n) do { id_src2->preg = &R(n); } while (0)
#define destR(n) do { id_dest->preg = (n == 0 ? &zero_null : &R(n)); } while (0)
#define src1I(i) do { id_src1->imm = i; } while (0)
#define src2I(i) do { id_src2->imm = i; } while (0)
#define destI(i) do { id_dest->imm = i; } while (0)
#define src1FR(n) do{ id_src1->preg = &FR(n); } while (0)
#define src2FR(n) do{ id_src2->preg = &FR(n); } while (0)
#define destFR(n) do{ id_dest->preg = &FR(n); } while (0)
#else
#define src1R(n) do { *src1 = R(n); } while (0)
#define src2R(n) do { *src2 = R(n); } while (0)
#define destR(n) do { *dest = n; } while (0)
#define src1I(i) do { *src1 = i; } while (0)
#define src2I(i) do { *src2 = i; } while (0)
#define destI(i) do { *dest = i; } while (0)
#define src1FR(n) do{ *src1 = FR(n); } while (0)
#define src2FR(n) do{ *src2 = FR(n); } while (0)
#define destFR(n) do{ *dest = n; } while (0)
#endif

static void decode_operand(Decode *s, word_t *dest, word_t *src1, word_t *src2, int type) {
  uint32_t i = s->extraInfo->isa.instr.val;
  int rd  = BITS(i, 11, 7);
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  destR(rd);
  switch (type) {
    case TYPE_FST: destI(immS(i)); src1R(rs1); src2FR(rs2); break;
    case TYPE_FLD: destFR(rd); // fall through
    case TYPE_I: src1R(rs1);             src2I(immI(i)); break;
    case TYPE_U: src1I(immU(i));         src2I(immU(i) + s->extraInfo->pc); break;
    case TYPE_J: src1I(s->extraInfo->pc + immJ(i)); src2I(s->extraInfo->snpc); break;
    case TYPE_S: destI(immS(i));         goto R;
    case TYPE_B: destI(immB(i) + s->extraInfo->pc); R: // fall through
    case TYPE_R: src1R(rs1); src2R(rs2); break;
    case TYPE_N: break;
    case TYPE_FR: src1FR(rs1); src2FR(rs2); destFR(rd); break;
    case TYPE_FR2R: src1FR(rs1); src2FR(rs2); destR(rd); break;
    case TYPE_R2FR: src1R(rs1); destFR(rd); break;
    case TYPE_I_JALR: src1R(rs1);   destI(s->extraInfo->snpc);  src2I(immI(i)); break;
    default: panic("type = %d at pc = " FMT_WORD , type, s->extraInfo->pc);
  }
}

static word_t sextw(word_t x) { return (int64_t)(int32_t)x; }

static int decode_exec(Decode *s) {
  word_t dest = 0, src1 = 0, src2 = 0;
  bool is_jmp = false;

#define INSTPAT_INST(s) ((s)->extraInfo->isa.instr.val)
#define INSTPAT_MATCH(s, name, type, ... /* body */ ) { \
  decode_operand(s, &dest, &src1, &src2, concat(TYPE_, type)); \
  IFDEF(CONFIG_PERF_OPT, return concat(EXEC_ID_, name)); \
  __VA_ARGS__ ; \
}

#define SET_JMPTAB(label) \
    jmptab[idx] = &&concat(decode_, label); \
    concat(decode_, label):

// defined a label before the pattern to optmize with jump table
#define INSTLAB(pattern, name, ...) \
        SET_JMPTAB(name); \
        INSTPAT(pattern, name, ##__VA_ARGS__)

  INSTPAT_START();
  static void *jmptab[256];
  uint32_t inst_slr2 = INSTPAT_INST(s) >> 2;  // the 2 bits at LSB are always 2'b11
  int idx = BITS(inst_slr2, 4, 0) | (BITS(inst_slr2, 12, 10) << 5);
  int rs1 = BITS(s->extraInfo->isa.instr.val, 19, 15);
  int rd  = BITS(s->extraInfo->isa.instr.val, 11, 7);
  int mmu_mode = isa_mmu_state();
  const void *jmptarget = jmptab[idx];
  if (jmptarget != NULL) goto *jmptarget;


  SET_JMPTAB(lb);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb_mmu , I);
}
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb     , I, R(dest) = SEXT(Mr(src1 + src2, 1), 8));

  SET_JMPTAB(lh);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh_mmu , I);
}
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh     , I, R(dest) = SEXT(Mr(src1 + src2, 2), 16));

  SET_JMPTAB(lw);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 010 ????? 00000 11", lwsp_mmu, I);
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw_mmu , I);
}
  INSTPAT("??????? ????? 00010 010 ????? 00000 11", lwsp, I);
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw     , I, R(dest) = SEXT(Mr(src1 + src2, 4), 32));

  SET_JMPTAB(ld);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 011 ????? 00000 11", ldsp_mmu, I);
  INSTPAT("??????? ????? ????? 011 ????? 00000 11", ld_mmu , I);
}
  INSTPAT("??????? ????? 00010 011 ????? 00000 11", ldsp, I);
  INSTPAT("??????? ????? ????? 011 ????? 00000 11", ld     , I, R(dest) = Mr(src1 + src2, 8));

  SET_JMPTAB(lbu);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu_mmu, I);
}
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu    , I, R(dest) = Mr(src1 + src2, 1));

  SET_JMPTAB(lhu);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu_mmu, I);
}
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu    , I, R(dest) = Mr(src1 + src2, 2));

  SET_JMPTAB(lwu);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 110 ????? 00000 11", lwu_mmu, I);
}
  INSTPAT("??????? ????? ????? 110 ????? 00000 11", lwu    , I, R(dest) = Mr(src1 + src2, 4));

  SET_JMPTAB(flw);
if (fp_enable()) {
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 010 ????? 00001 11" , flwsp_mmu, FLD);
  INSTPAT("??????? ????? ????? 010 ????? 00001 11" , flw_mmu  , FLD);
}
  INSTPAT("??????? ????? 00010 010 ????? 00001 11" , flwsp, FLD);
  INSTPAT("??????? ????? ????? 010 ????? 00001 11" , flw  , FLD);
} else {
  INSTPAT("????? ?? ????? ????? 01? ????? 0?001 11", rt_inv, N, rt_inv(s)); // fld/flw/fsd/fsw
}

  SET_JMPTAB(fld);
if (fp_enable()) {
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 011 ????? 00001 11" , fldsp_mmu, FLD);
  INSTPAT("??????? ????? ????? 011 ????? 00001 11" , fld_mmu  , FLD);
}
  INSTPAT("??????? ????? 00010 011 ????? 00001 11" , fldsp, FLD);
  INSTPAT("??????? ????? ????? 011 ????? 00001 11" , fld  , FLD, FR(dest) = Mr(src1 + src2, 8));
} else {
  INSTPAT("????? ?? ????? ????? 01? ????? 0?001 11", rt_inv, N, rt_inv(s)); // fld/flw/fsd/fsw
}

  INSTLAB("??????? ????? ????? 000 ????? 00011 11", fence  , I); // do nothing in non-perf mode
  INSTLAB("??????? ????? ????? 001 ????? 00011 11", fence_i, I); // do nothing in non-perf mode

  INSTLAB("0000000 00000 00000 000 ????? 00100 11", p_li_0 , I);
  INSTPAT("0000000 00001 00000 000 ????? 00100 11", p_li_1 , I);
  INSTPAT("??????? ????? 00000 000 ????? 00100 11", c_li   , I);
  INSTPAT("0000000 00000 ????? 000 ????? 00100 11", p_mv_src1, I);
  INSTPAT("??????? ????? 00010 000 00010 00100 11", c_addisp_sp, I);
if (rd == rs1) {
  INSTPAT("0000000 00001 ????? 000 ????? 00100 11", p_inc  , I);
  INSTPAT("1111111 11111 ????? 000 ????? 00100 11", p_dec  , I);
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", c_addi , I);
}
  INSTPAT("??????? ????? 00010 000 ????? 00100 11", c_addix_sp, I);
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi   , I, R(dest) = src1 + src2);

  SET_JMPTAB(slli);
if (rd == rs1) {
  INSTPAT("000000? ????? ????? 001 ????? 00100 11", c_slli , I);
}
  INSTPAT("000000? ????? ????? 001 ????? 00100 11", slli   , I, R(dest) = src1 << src2);

  INSTLAB("??????? ????? ????? 010 ????? 00100 11", slti   , I, R(dest) = (sword_t)src1 < (sword_t)src2);

  INSTLAB("0000000 00001 ????? 011 ????? 00100 11", p_seqz , I);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu  , I, R(dest) = src1 <  src2);

  INSTLAB("1111111 11111 ????? 100 ????? 00100 11", p_not  , I);
  INSTPAT("??????? ????? ????? 100 ????? 00100 11", xori   , I, R(dest) = src1 ^  src2);

  SET_JMPTAB(srli);
if (rd == rs1) {
  INSTPAT("000000? ????? ????? 101 ????? 00100 11", c_srli , I);
  INSTPAT("010000? ????? ????? 101 ????? 00100 11", c_srai , I);
}
  INSTPAT("000000? ????? ????? 101 ????? 00100 11", srli   , I, R(dest) = src1 >> src2);
  INSTPAT("010000? ????? ????? 101 ????? 00100 11", srai   , I, R(dest) = (sword_t)src1 >> (src2 & 0x3f));

  INSTLAB("??????? ????? ????? 110 ????? 00100 11", ori    , I, R(dest) = src1 |  src2);

  SET_JMPTAB(andi);
if (rd == rs1) {
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", c_andi , I);
}
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", andi   , I, R(dest) = src1 &  src2);


  INSTLAB("??????? ????? ????? ??? 00001 00101 11", p_li_ra, U);   // call
  INSTPAT("??????? ????? ????? ??? 00110 00101 11", p_li_t0, U);   // tail a
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc  , U, R(dest) = src2);

  INSTLAB("0000000 00000 ????? 000 ????? 00110 11", p_sext_w, I);
if (rd == rs1) {
  INSTPAT("??????? ????? ????? 000 ????? 00110 11", c_addiw, I);
}
  INSTPAT("??????? ????? ????? 000 ????? 00110 11", addiw  , I, R(dest) = sextw(src1 + src2));

  INSTLAB("0000000 ????? ????? 001 ????? 00110 11", slliw  , I, R(dest) = sextw((uint32_t)src1 << (src2 & 0x1f)));
  INSTLAB("0000000 ????? ????? 101 ????? 00110 11", srliw  , I, R(dest) = sextw((uint32_t)src1 >> (src2 & 0x1f)));
  INSTPAT("0100000 ????? ????? 101 ????? 00110 11", sraiw  , I, R(dest) = sextw(( int32_t)src1 >> (src2 & 0x1f)));

  SET_JMPTAB(sb);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb_mmu , S);
}
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb     , S, Mw(src1 + dest, 1, src2));

  SET_JMPTAB(sh);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh_mmu , S);
}
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh     , S, Mw(src1 + dest, 2, src2));

  SET_JMPTAB(sw);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 010 ????? 01000 11", swsp_mmu, S);
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw_mmu , S);
}
  INSTPAT("??????? ????? 00010 010 ????? 01000 11", swsp, S);
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw     , S, Mw(src1 + dest, 4, src2));

  SET_JMPTAB(sd);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 011 ????? 01000 11", sdsp_mmu, S);
  INSTPAT("??????? ????? ????? 011 ????? 01000 11", sd_mmu , S);
}
  INSTPAT("??????? ????? 00010 011 ????? 01000 11", sdsp, S);
  INSTPAT("??????? ????? ????? 011 ????? 01000 11", sd     , S, Mw(src1 + dest, 8, src2));


  SET_JMPTAB(fsw);
if (fp_enable()) {
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 010 ????? 01001 11" , fswsp_mmu, FST);
  INSTPAT("??????? ????? ????? 010 ????? 01001 11" , fsw_mmu  , FST);
}
  INSTPAT("??????? ????? 00010 010 ????? 01001 11" , fswsp, FST);
  INSTPAT("??????? ????? ????? 010 ????? 01001 11" , fsw      , FST);
} else {
  INSTPAT("??????? ????? ????? 01? ????? 0?001 11", rt_inv, N, rt_inv(s)); // fld/flw/fsd/fsw
}


  SET_JMPTAB(fsd);
if (fp_enable()) {
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("??????? ????? 00010 011 ????? 01001 11" , fsdsp_mmu, FST);
  INSTPAT("??????? ????? ????? 011 ????? 01001 11" , fsd_mmu  , FST);
}
  INSTPAT("??????? ????? 00010 011 ????? 01001 11" , fsdsp, FST);
  INSTPAT("??????? ????? ????? 011 ????? 01001 11" , fsd      , FST);
  INSTPAT("??????? ????? ????? 01? ????? 0?001 11", rt_inv, N, rt_inv(s)); // fld/flw/fsd/fsw
} else {
  INSTPAT("??????? ????? ????? 01? ????? 0?001 11", rt_inv, N, rt_inv(s)); // fld/flw/fsd/fsw
}


  extern void rtl_amo_slow_path(Decode *s, rtlreg_t *dest, const rtlreg_t *src1, const rtlreg_t *src2);
  INSTLAB("??????? ????? ????? ??? ????? 01011 11", atomic , R, rtl_amo_slow_path(s, &R(dest), &src1, &src2));


  INSTLAB("0000000 ????? 00000 000 ????? 01100 11", p_mv_src2, R);  // c.mv rd rs2
if (rd == rs1) {
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", c_add  , R);
}
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add    , R, R(dest) = src1 +  src2);
  INSTPAT("0100000 ????? 00000 000 ????? 01100 11", p_neg  , R);
if (rd == rs1) {
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", c_sub  , R);
}
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub    , R, R(dest) = src1 -  src2);
  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul    , R, R(dest) = src1 *  src2);

  INSTLAB("0000000 ????? ????? 001 ????? 01100 11", sll    , R, R(dest) = src1 << src2);
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh   , R,
      R(dest) = ((__int128_t)(sword_t)src1 *  (__int128_t)(sword_t)src2) >> 64);

  INSTLAB("0000000 00000 ????? 010 ????? 01100 11", p_sltz , R);
  INSTPAT("0000000 ????? 00000 010 ????? 01100 11", p_sgtz , R);
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt    , R, R(dest) = (sword_t)src1 <  (sword_t)src2);
  INSTPAT("0000001 ????? ????? 010 ????? 01100 11", mulhsu , R,
      word_t hi = ((__uint128_t)src1 *  (__uint128_t)src2) >> 64;
      R(dest) = hi - ((sword_t)src1  < 0 ? src2 : 0));

  INSTLAB("0000000 ????? 00000 011 ????? 01100 11", p_snez , R);
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu   , R, R(dest) = src1 <  src2);
  INSTPAT("0000001 ????? ????? 011 ????? 01100 11", mulhu  , R,
      R(dest) = ((__uint128_t)src1 *  (__uint128_t)src2) >> 64);

  SET_JMPTAB(xor);
if (rd == rs1) {
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", c_xor  , R);
}
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor    , R, R(dest) = src1 ^  src2);
  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div    , R, R(dest) = (sword_t)src1 /  (sword_t)src2);

  INSTLAB("0000000 ????? ????? 101 ????? 01100 11", srl    , R, R(dest) = src1 >> src2);
  INSTPAT("0100000 ????? ????? 101 ????? 01100 11", sra    , R, R(dest) = (sword_t)src1 >> src2);
  INSTPAT("0000001 ????? ????? 101 ????? 01100 11", divu   , R, R(dest) = src1 /  src2);

  SET_JMPTAB(or);
if (rd == rs1) {
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", c_or   , R);
}
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or     , R, R(dest) = src1 |  src2);
  INSTPAT("0000001 ????? ????? 110 ????? 01100 11", rem    , R, R(dest) = (sword_t)src1 %  (sword_t)src2);

  SET_JMPTAB(and);
if (rd == rs1) {
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", c_and  , R);
}
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", and    , R, R(dest) = src1 &  src2);
  INSTPAT("0000001 ????? ????? 111 ????? 01100 11", remu   , R, R(dest) = src1 %  src2);


  INSTLAB("??????? ????? ????? ??? ????? 01101 11", lui    , U, R(dest) = src1);


  INSTLAB("0100000 ????? 00000 000 ????? 01110 11", p_negw , R);
if (rd == rs1) {
  INSTPAT("0100000 ????? ????? 000 ????? 01110 11", c_subw , R);
}
  INSTPAT("0100000 ????? ????? 000 ????? 01110 11", subw   , R, R(dest) = sextw(src1 - src2));
if (rd == rs1) {
  INSTPAT("0000000 ????? ????? 000 ????? 01110 11", c_addw , R);
}
  INSTPAT("0000000 ????? ????? 000 ????? 01110 11", addw   , R, R(dest) = sextw(src1 + src2));
  INSTPAT("0000001 ????? ????? 000 ????? 01110 11", mulw   , R, R(dest) = sextw(src1 * src2));

  INSTLAB("0000000 ????? ????? 001 ????? 01110 11", sllw   , R, R(dest) = sextw((uint32_t)src1 << (src2 & 0x1f)));

  INSTLAB("0000001 ????? ????? 100 ????? 01110 11", divw   , R, R(dest) = sextw(( int32_t)src1 / ( int32_t)src2));

  INSTLAB("0000000 ????? ????? 101 ????? 01110 11", srlw   , R, R(dest) = sextw((uint32_t)src1 >> (src2 & 0x1f)));
  INSTPAT("0100000 ????? ????? 101 ????? 01110 11", sraw   , R, R(dest) = sextw(( int32_t)src1 >> (src2 & 0x1f)));
  INSTPAT("0000001 ????? ????? 101 ????? 01110 11", divuw  , R, R(dest) = sextw((uint32_t)src1 / (uint32_t)src2));


  INSTLAB("0000001 ????? ????? 110 ????? 01110 11", remw   , R, R(dest) = sextw(( int32_t)src1 % ( int32_t)src2));
  INSTLAB("0000001 ????? ????? 111 ????? 01110 11", remuw  , R, R(dest) = sextw((uint32_t)src1 % (uint32_t)src2));


  // FIXME: check fp_enable()
  INSTLAB("????? 00 ????? ????? ??? ????? 10000 11", fmadds   , FR);
  INSTPAT("????? 01 ????? ????? ??? ????? 10000 11", fmaddd   , FR);
  INSTLAB("????? 00 ????? ????? ??? ????? 10001 11", fmsubs   , FR);
  INSTPAT("????? 01 ????? ????? ??? ????? 10001 11", fmsubd   , FR);
  INSTLAB("????? 00 ????? ????? ??? ????? 10010 11", fnmsubs  , FR);
  INSTPAT("????? 01 ????? ????? ??? ????? 10010 11", fnmsubd  , FR);
  INSTLAB("????? 00 ????? ????? ??? ????? 10011 11", fnmadds  , FR);
  INSTPAT("????? 01 ????? ????? ??? ????? 10011 11", fnmaddd  , FR);

  SET_JMPTAB(fp);
if (fp_enable()) {
int rs2 = BITS(s->extraInfo->isa.instr.val, 24, 20);
if(rs1 == rs2) {
  INSTPAT("0010000 ????? ????? 000 ????? 10100 11" , p_fmv_s  , FR);
  INSTPAT("0010000 ????? ????? 010 ????? 10100 11" , p_fabs_s , FR);
  INSTPAT("0010000 ????? ????? 001 ????? 10100 11" , p_fneg_s , FR);
  INSTPAT("0010001 ????? ????? 000 ????? 10100 11" , p_fmv_d  , FR);
  INSTPAT("0010001 ????? ????? 010 ????? 10100 11" , p_fabs_d , FR);
  INSTPAT("0010001 ????? ????? 001 ????? 10100 11" , p_fneg_d , FR);
}

  // rv32f
  INSTPAT("00000 00 ????? ????? ??? ????? 10100 11", fadds    , FR   );
  INSTPAT("00001 00 ????? ????? ??? ????? 10100 11", fsubs    , FR   );
  INSTPAT("00010 00 ????? ????? ??? ????? 10100 11", fmuls    , FR   );
  INSTPAT("00011 00 ????? ????? ??? ????? 10100 11", fdivs    , FR   );
  INSTPAT("01011 00 00000 ????? ??? ????? 10100 11", fsqrts   , FR   );
  INSTPAT("00100 00 ????? ????? 000 ????? 10100 11", fsgnjs   , FR   );
  INSTPAT("00100 00 ????? ????? 001 ????? 10100 11", fsgnjns  , FR   );
  INSTPAT("00100 00 ????? ????? 010 ????? 10100 11", fsgnjxs  , FR   );
  INSTPAT("00101 00 ????? ????? 000 ????? 10100 11", fmins    , FR   );
  INSTPAT("00101 00 ????? ????? 001 ????? 10100 11", fmaxs    , FR   );
  INSTPAT("11000 00 00000 ????? 111 ????? 10100 11", fcvt_w_s , FR2R );
  INSTPAT("11000 00 00001 ????? 111 ????? 10100 11", fcvt_wu_s, FR2R );
  INSTPAT("11100 00 00000 ????? 000 ????? 10100 11", fmv_x_w  , FR2R );
  INSTPAT("10100 00 ????? ????? 010 ????? 10100 11", feqs     , FR2R );
  INSTPAT("10100 00 ????? ????? 001 ????? 10100 11", flts     , FR2R );
  INSTPAT("10100 00 ????? ????? 000 ????? 10100 11", fles     , FR2R );
//INSTPAT("11100 00 00000 ????? 001 ????? 10100 11", fclasss  , FR2R );
  INSTPAT("11010 00 00000 ????? 111 ????? 10100 11", fcvt_s_w , R2FR );
  INSTPAT("11010 00 00001 ????? ??? ????? 10100 11", fcvt_s_wu, R2FR );
  INSTPAT("11110 00 00000 ????? 000 ????? 10100 11", fmv_w_x  , R2FR );
  INSTPAT("11010 00 00000 ????? ??? ????? 10100 11", fcvt_s_w_rm,R2FR);
  INSTPAT("11000 00 00000 ????? ??? ????? 10100 11", fcvt_w_s_rm,FR2R);
  INSTPAT("11000 00 00001 ????? ??? ????? 10100 11", fcvt_wu_s_rm,FR2R );

  // rv64f
  INSTPAT("11000 00 00010 ????? 111 ????? 10100 11", fcvt_l_s , FR2R );
  INSTPAT("11000 00 00011 ????? 111 ????? 10100 11", fcvt_lu_s, FR2R );
  INSTPAT("11010 00 00010 ????? ??? ????? 10100 11", fcvt_s_l , R2FR );
  INSTPAT("11010 00 00011 ????? ??? ????? 10100 11", fcvt_s_lu, R2FR );
  INSTPAT("11000 00 00010 ????? ??? ????? 10100 11", fcvt_l_s_rm,FR2R);
  INSTPAT("11000 00 00011 ????? ??? ????? 10100 11", fcvt_lu_s_rm,FR2R);

  // rv32d
  INSTPAT("00000 01 ????? ????? ??? ????? 10100 11", faddd    , FR  );
  INSTPAT("00001 01 ????? ????? ??? ????? 10100 11", fsubd    , FR  );
  INSTPAT("00010 01 ????? ????? ??? ????? 10100 11", fmuld    , FR  );
  INSTPAT("00011 01 ????? ????? ??? ????? 10100 11", fdivd    , FR  );
  INSTPAT("01011 01 00000 ????? ??? ????? 10100 11", fsqrtd   , FR  );
  INSTPAT("00100 01 ????? ????? 000 ????? 10100 11", fsgnjd   , FR  );
  INSTPAT("00100 01 ????? ????? 001 ????? 10100 11", fsgnjnd  , FR  );
  INSTPAT("00100 01 ????? ????? 010 ????? 10100 11", fsgnjxd  , FR  );
  INSTPAT("00101 01 ????? ????? 000 ????? 10100 11", fmind    , FR  );
  INSTPAT("00101 01 ????? ????? 001 ????? 10100 11", fmaxd    , FR  );
  INSTPAT("01000 00 00001 ????? 111 ????? 10100 11", fcvt_s_d , FR  );
  INSTPAT("01000 01 00000 ????? 111 ????? 10100 11", fcvt_d_s , FR  );
  INSTPAT("10100 01 ????? ????? 010 ????? 10100 11", feqd     , FR2R);
  INSTPAT("10100 01 ????? ????? 001 ????? 10100 11", fltd     , FR2R);
  INSTPAT("10100 01 ????? ????? 000 ????? 10100 11", fled     , FR2R);
  INSTPAT("11100 01 00000 ????? 001 ????? 10100 11", fclassd  , FR2R);
  INSTPAT("11000 01 00000 ????? 111 ????? 10100 11", fcvt_w_d , FR2R);
  INSTPAT("11000 01 00001 ????? 111 ????? 10100 11", fcvt_wu_d, FR2R);
  INSTPAT("11010 01 00000 ????? 111 ????? 10100 11", fcvt_d_w , R2FR);
  INSTPAT("11010 01 00001 ????? 111 ????? 10100 11", fcvt_d_wu, R2FR);
  INSTPAT("01000 00 00001 ????? ??? ????? 10100 11", fcvt_s_d_rm, FR);
  INSTPAT("01000 01 00000 ????? ??? ????? 10100 11", fcvt_d_s_rm, FR);
  INSTPAT("11000 01 00000 ????? ??? ????? 10100 11", fcvt_w_d_rm, FR2R);
  INSTPAT("11000 01 00001 ????? ??? ????? 10100 11", fcvt_wu_d_rm, FR2R);
  INSTPAT("11010 01 00000 ????? ??? ????? 10100 11", fcvt_d_w_rm, R2FR);
  INSTPAT("11010 01 00001 ????? ??? ????? 10100 11", fcvt_d_wu_rm, R2FR);

  // rv64d
  INSTPAT("11000 01 00010 ????? 111 ????? 10100 11", fcvt_l_d , FR2R);
  INSTPAT("11000 01 00011 ????? 111 ????? 10100 11", fcvt_lu_d, FR2R);
  INSTPAT("11100 01 00000 ????? 000 ????? 10100 11", fmv_x_d  , FR2R);
  INSTPAT("11010 01 00010 ????? 111 ????? 10100 11", fcvt_d_l , R2FR);
  INSTPAT("11010 01 00011 ????? ??? ????? 10100 11", fcvt_d_lu, R2FR);
  INSTPAT("11110 01 00000 ????? 000 ????? 10100 11", fmv_d_x  , R2FR);
  INSTPAT("11000 01 00010 ????? ??? ????? 10100 11", fcvt_l_d_rm , FR2R);
  INSTPAT("11000 01 00011 ????? ??? ????? 10100 11", fcvt_lu_d_rm, FR2R);
  INSTPAT("11010 01 00010 ????? ??? ????? 10100 11", fcvt_d_l_rm , R2FR);
} else {
  INSTPAT("????? ?? ????? ????? ??? ????? 10100 11", rt_inv, N, rt_inv(s)); // fop
}

  INSTLAB("??????? 00000 ????? 000 ????? 11000 11", c_beqz , B);
  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq    , B, jcond(src1 == src2, dest));

  INSTLAB("??????? 00000 ????? 001 ????? 11000 11", c_bnez , B);
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne    , B, jcond(src1 != src2, dest));

  INSTLAB("??????? 00000 ????? 100 ????? 11000 11", p_bltz , B);
  INSTPAT("??????? ????? 00000 100 ????? 11000 11", p_bgtz , B);
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt    , B, jcond((sword_t)src1 <  (sword_t)src2, dest));

  INSTLAB("??????? 00000 ????? 101 ????? 11000 11", p_bgez , B);
  INSTPAT("??????? ????? 00000 101 ????? 11000 11", p_blez , B);
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge    , B, jcond((sword_t)src1 >= (sword_t)src2, dest));

  INSTLAB("??????? ????? ????? 110 ????? 11000 11", bltu   , B, jcond(src1 <  src2, dest));
  INSTLAB("??????? ????? ????? 111 ????? 11000 11", bgeu   , B, jcond(src1 >= src2, dest));

  INSTLAB("0000000 00000 00001 ??? 00000 11001 11", p_ret  , I);
  INSTPAT("0000000 00000 ????? ??? 00000 11001 11", c_jr   , I);
  INSTPAT("0000000 00000 00001 ??? 00001 11001 11", p_jalr_ra_noimm , I_JALR);
  INSTPAT("??????? ????? 00001 ??? 00001 11001 11", p_jalr_ra, I_JALR);
  INSTPAT("??????? ????? 00110 ??? 00000 11001 11", p_jalr_t0, I_JALR);
  INSTPAT("??????? ????? ????? ??? ????? 11001 11", jalr   , I, jcond(true, src1 + src2); R(dest) = s->extraInfo->snpc);

  INSTLAB("??????? ????? ????? ??? ????? 11010 11", nemu_trap, N, NEMUTRAP(s->extraInfo->pc, R(10))); // R(10) is $a0

  INSTLAB("??????? ????? ????? ??? 00000 11011 11", c_j    , J);
  INSTPAT("??????? ????? ????? ??? 00001 11011 11", p_jal  , J);
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal    , J, jcond(true, src1); R(dest) = src2);


  extern int rtl_sys_slow_path(Decode *s, rtlreg_t *dest, const rtlreg_t *src1, uint32_t id, rtlreg_t *jpc);
  INSTLAB("??????? ????? ????? ??? ????? 11100 11", system , I,
      is_jmp = rtl_sys_slow_path(s, &R(dest), &src1, src2, NULL));

  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv    , N, INV(s->extraInfo->pc));
  INSTPAT_END();

  R(0) = 0; // reset $zero to 0
  if (!is_jmp) cpu.pc = s->extraInfo->snpc;

  return 0;
}

__attribute__((always_inline))
static inline word_t rvc_imm_internal(uint32_t instr, const char *str, int len, bool sign) {
  word_t imm = 0;
  uint32_t msb_mask = 0;
  uint32_t instr_sll16 = instr << 16;
  assert(len == 16);
#define macro(i) do { \
    char c = str[i]; \
    if (c != '.') { \
      Assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), \
          "invalid character '%c' in pattern string", c); \
      int pos = (c >= '0' && c <= '9') ? c - '0' : c - 'a' + 10; \
      int nr_srl = 16 + (15 - (i)) - pos; \
      uint32_t mask = 1u << pos ; \
      imm |= (instr_sll16 >> nr_srl) & mask; \
      if (mask > msb_mask) { msb_mask = mask;} \
    } \
  } while (0)

  macro16(0);
#undef macro

  if (sign && (msb_mask & imm)) {
    imm |= (-imm) & ~((word_t)msb_mask - 1);
  }
  return imm;
}

#define rvc_simm(i, str) rvc_imm_internal(i, str, STRLEN(str), true)
#define rvc_uimm(i, str) rvc_imm_internal(i, str, STRLEN(str), false)
#define creg2reg(creg) (creg + 8)

static void decode_operand_rvc(Decode *s, word_t *dest, word_t *src1, word_t *src2, int type) {
  uint32_t i = s->extraInfo->isa.instr.val;
  int r9_7 = creg2reg(BITS(i, 9, 7));
  int r4_2 = creg2reg(BITS(i, 4, 2));
  int r11_7 = BITS(i, 11, 7);
  int r6_2 = BITS(i, 6, 2);
#define rdrs1(r0, r1) do { destR(r0); src1R(r1); } while (0)
#define rdrs1_same(r) rdrs1(r, r)
#define rs1rs2(r1, r2) do { src1R(r1); src2R(r2); } while (0)
  switch (type) {
    case TYPE_CIW:    src2I(rvc_uimm(i, "...54987623....."));         rdrs1(r4_2, 2); break;
    case TYPE_CLW:    src2I(rvc_uimm(i, "...543...26.....")); goto CL;
    case TYPE_CLD:    src2I(rvc_uimm(i, "...543...76.....")); CL:     rdrs1(r4_2, r9_7); break;
    case TYPE_CSW:    destI(rvc_uimm(i, "...543...26.....")); goto CW;
    case TYPE_CSD:    destI(rvc_uimm(i, "...543...76.....")); CW:     rs1rs2(r9_7, r4_2); break;
    case TYPE_CI:     src2I(rvc_simm(i, "...5.....43210.."));         rdrs1_same(r11_7); break;
    case TYPE_CASP:   src2I(rvc_simm(i, "...9.....46875.."));         rdrs1_same(2); break;
    case TYPE_CLUI:   src1I(rvc_simm(i, "...5.....43210..") << 12);   destR(r11_7); break;
    case TYPE_CSHIFT: src2I(rvc_uimm(i, "...5.....43210.."));         rdrs1_same(r9_7); break;
    case TYPE_CANDI:  src2I(rvc_simm(i, "...5.....43210.."));         rdrs1_same(r9_7); break;
    case TYPE_CJ:     src1I(rvc_simm(i, "...b498a673215..") + s->extraInfo->pc); break;
    case TYPE_CB:     destI(rvc_simm(i, "...843...76215..") + s->extraInfo->pc); rs1rs2(r9_7, 0); break;
    case TYPE_CIU:    src2I(rvc_uimm(i, "...5.....43210.."));         rdrs1_same(r11_7); break;
    case TYPE_CLWSP:  src2I(rvc_uimm(i, "...5.....43276..")); goto CI_ld;
    case TYPE_CLDSP:  src2I(rvc_uimm(i, "...5.....43876..")); CI_ld:  rdrs1(r11_7, 2); break;
    case TYPE_CSWSP:  destI(rvc_uimm(i, "...543276.......")); goto CSS;
    case TYPE_CSDSP:  destI(rvc_uimm(i, "...543876.......")); CSS:    rs1rs2(2, r6_2); break;
    case TYPE_CFLD:   src2I(rvc_uimm(i, "...543...76.....")); destFR(r4_2); src1R(r9_7); break;
    case TYPE_CFLDSP: src2I(rvc_uimm(i, "...5.....43876..")); destFR(r11_7); src1R(2); break;
    case TYPE_CFSD:   destI(rvc_uimm(i, "...543...76.....")); src1R(r9_7); src2FR(r4_2); break;
    case TYPE_CFSDSP: destI(rvc_uimm(i, "...543876.......")); src1R(2); src2FR(r6_2); break;
    case TYPE_CS: rdrs1_same(r9_7);  src2R(r4_2); break;
    case TYPE_CR: rdrs1_same(r11_7); src2R(r6_2); break;
    case TYPE_CR_JALR: rdrs1_same(r11_7); src2R(r6_2); destI(s->extraInfo->snpc); break;
    default: panic("inst = %x type = %d at pc = " FMT_WORD , s->extraInfo->isa.instr.val, type, s->extraInfo->pc);
  }
}

static int decode_exec_rvc(Decode *s) {
  word_t dest = 0, src1 = 0, src2 = 0;
  bool is_jmp = false;

#undef INSTPAT_MATCH
#define INSTPAT_MATCH(s, name, type, ... /* body */ ) { \
  decode_operand_rvc(s, &dest, &src1, &src2, concat(TYPE_, type)); \
  IFDEF(CONFIG_PERF_OPT, return concat(EXEC_ID_, name)); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();

  static void *jmptab[256];
  uint32_t inst = INSTPAT_INST(s);
  int idx = (BITS(inst, 1, 0) << 6) | BITS(inst, 15, 10);
  int mmu_mode = isa_mmu_state();
  const void *jmptarget = jmptab[idx];
  if (jmptarget != NULL) goto *jmptarget;

if (!fp_enable()) {
  INSTPAT("?01 ??? ??? ?? ??? ?0", rt_inv, N, rt_inv(s));
}
  INSTPAT("000 0 ????? 00001 01", p_inc   , CI);
  INSTPAT("000 1 ????? 11111 01", p_dec   , CI);
  INSTPAT("010 0 ????? 00000 01", p_li_0  , CI);
  INSTPAT("010 0 ????? 00001 01", p_li_1  , CI);
  INSTPAT("100 0 00001 00000 10", p_ret   , CR);

  // Q0
  INSTLAB("000  00000000  000 00", inv    , N   , INV(s->extraInfo->pc));
  INSTPAT("000  ????????  ??? 00", c_addix_sp, CIW , R(dest) = src1 + src2); // C.ADDI4SPN

  SET_JMPTAB(fld);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("001 ??? ??? ?? ??? 00", fld_mmu, CFLD);
}
  INSTPAT("001 ??? ??? ?? ??? 00", fld    , CFLD, assert(0));

  SET_JMPTAB(lw);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("010 ??? ??? ?? ??? 00", lw_mmu , CLW);
}
  INSTPAT("010 ??? ??? ?? ??? 00", lw     , CLW , R(dest) = SEXT(Mr(src1 + src2, 4), 32));

  SET_JMPTAB(ld);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("011 ??? ??? ?? ??? 00", ld_mmu , CLD);
}
  INSTPAT("011 ??? ??? ?? ??? 00", ld     , CLD , R(dest) = Mr(src1 + src2, 8));

  SET_JMPTAB(fsd);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("101 ??? ??? ?? ??? 00", fsd_mmu, CFSD);
}
  INSTPAT("101 ??? ??? ?? ??? 00", fsd    , CFSD, assert(0));

  SET_JMPTAB(sw);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("110 ??? ??? ?? ??? 00", sw_mmu , CSW);
}
  INSTPAT("110 ??? ??? ?? ??? 00", sw     , CSW , Mw(src1 + dest, 4, src2));

  SET_JMPTAB(sd);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("111 ??? ??? ?? ??? 00", sd_mmu , CSD);
}
  INSTPAT("111 ??? ??? ?? ??? 00", sd     , CSD , Mw(src1 + dest, 8, src2));

  // Q1
  INSTLAB("000 ? ????? ????? 01", c_addi  , CI , R(dest) = src1 + src2);
  INSTLAB("001 ? ????? ????? 01", c_addiw , CI , R(dest) = sextw(src1 + src2));
  INSTLAB("010 ? ????? ????? 01", c_li    , CI , R(dest) = src2);
  INSTLAB("011 ? 00010 ????? 01", c_addisp_sp, CASP,R(dest) = src1 + src2); // C.ADDI16SP
  INSTPAT("011 ? ????? ????? 01", lui     , CLUI,R(dest) = src1);
  INSTLAB("100 ? 00??? ????? 01", c_srli  , CSHIFT, R(dest) = src1 >> src2);
  INSTLAB("100 ? 01??? ????? 01", c_srai  , CSHIFT, R(dest) = (sword_t)src1 >> src2);
  INSTLAB("100 ? 10??? ????? 01", c_andi  , CANDI , R(dest) = src1 & src2);
  INSTLAB("100 0 11??? 00??? 01", c_sub   , CS, R(dest) = src1 - src2);
  INSTPAT("100 0 11??? 01??? 01", c_xor   , CS, R(dest) = src1 ^ src2);
  INSTPAT("100 0 11??? 10??? 01", c_or    , CS, R(dest) = src1 | src2);
  INSTPAT("100 0 11??? 11??? 01", c_and   , CS, R(dest) = src1 & src2);
  INSTLAB("100 1 11??? 00??? 01", c_subw  , CS, R(dest) = sextw(src1 - src2));
  INSTPAT("100 1 11??? 01??? 01", c_addw  , CS, R(dest) = sextw(src1 + src2));
  INSTLAB("101  ???????????  01", c_j     , CJ, jcond(true, src1));
  INSTLAB("110 ??? ??? ????? 01", c_beqz  , CB, jcond(src1 == 0, dest));
  INSTLAB("111 ??? ??? ????? 01", c_bnez  , CB, jcond(src1 != 0, dest));

  // Q2
  INSTLAB("000 ? ????? ????? 10", slli    , CIU, R(dest) = src1 << src2);

  SET_JMPTAB(fldsp);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("001 ? ????? ????? 10" , fldsp_mmu, CFLDSP);
}
  INSTPAT("001 ? ????? ????? 10", fldsp     , CFLDSP, assert(0));

  SET_JMPTAB(lwsp);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("010 ? ????? ????? 10" , lwsp_mmu , CLWSP);
}
  INSTPAT("010 ? ????? ????? 10", lwsp    , CLWSP , R(dest) = SEXT(Mr(src1 + src2, 4), 32));

  SET_JMPTAB(ldsp);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("011 ? ????? ????? 10" , ldsp_mmu , CLDSP);
}
  INSTPAT("011 ? ????? ????? 10", ldsp    , CLDSP , R(dest) = Mr(src1 + src2, 8));

  INSTLAB("100 0 ????? 00000 10", c_jr    , CR, jcond(true, src1));
  INSTPAT("100 0 ????? ????? 10", c_mv    , CR, R(dest) = src2);
  // c_jalr can not handle correctly when rs1 == ra, fall back to general jalr
  INSTLAB("100 1 00001 00000 10", jalr    , CR);
  INSTPAT("100 1 00000 00000 10", inv     , N);  // ebreak
  INSTPAT("100 1 ????? 00000 10", c_jalr  , CR_JALR, jcond(true, src1); R(1) = s->extraInfo->snpc);
  INSTPAT("100 1 ????? ????? 10", c_add   , CR, R(dest) = src1 + src2);

  SET_JMPTAB(fsdsp);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("101 ? ????? ????? 10" , fsdsp_mmu, CFSDSP);
}
  INSTPAT("101 ? ????? ????? 10", fsdsp     , CFSDSP, assert(0));

  SET_JMPTAB(swsp);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("110 ? ????? ????? 10" , swsp_mmu , CSWSP);
}
  INSTPAT("110 ? ????? ????? 10", swsp    , CSWSP , Mw(src1 + dest, 4, src2));

  SET_JMPTAB(sdsp);
if (mmu_mode == MMU_TRANSLATE) {
  INSTPAT("111 ? ????? ????? 10" , sdsp_mmu , CSDSP);
}
  INSTPAT("111 ? ????? ????? 10", sdsp    , CSDSP , Mw(src1 + dest, 8, src2));

  INSTPAT("??? ??? ??? ?? ??? ??", inv    , N  , INV(s->extraInfo->pc));
  INSTPAT_END();

  R(0) = 0; // reset $zero to 0

  // if an exception is raised during execution, we will
  // still update cpu.pc here, but it will be overwritten
  // in cpu.exec.c:fetch_decode(), since `g_ex_cause ` is set
  if (!is_jmp) cpu.pc = s->extraInfo->snpc;

  return 0;
}

int isa_fetch_decode(Decode *s) {
  int idx = 0;
  ExtraInfo* extra = s->extraInfo;
  extra->isa.instr.val = instr_fetch(&extra->snpc, 2);
  check_ex(0);
  if (BITS(extra->isa.instr.val, 1, 0) != 0x3) {
    // this is an RVC instruction
    idx = decode_exec_rvc(s);
  } else {
    // this is a 4-byte instruction, should fetch the MSB part
    // NOTE: The fetch here may cause IPF.
    // If it is the case, we should have mepc = xxxffe and mtval = yyy000.
    // Refer to `mtval` in the privileged manual for more details.
    uint32_t hi = instr_fetch(&extra->snpc, 2);
    check_ex(0);
    extra->isa.instr.val |= (hi << 16);
    idx = decode_exec(s);
  }
#ifdef CONFIG_PERF_OPT
  uint32_t instr = extra->isa.instr.val;
  extern void update_exec_table(Decode* s, int idx);
  static Decode* prev_s;
  static int prev_idx = 0;
  static int prev_type_j = 0;
  static int prev_type_b = 0;
  if(prev_type_j){
    rtlreg_t target = prev_s->extraInfo->jnpc;
    if(target == extra->pc){
      int new_idx = (prev_idx == EXEC_ID_c_j) ? EXEC_ID_c_j_next :
                    (prev_idx == EXEC_ID_p_jal) ? EXEC_ID_p_jal_next :
                    (prev_idx == EXEC_ID_jal) ? EXEC_ID_jal_next : -1;
      assert(new_idx != -1);
      update_exec_table(prev_s, new_idx);
    }
  } else if(prev_type_b){
    rtlreg_t target_tk = prev_s->extraInfo->jnpc;
    rtlreg_t target_nt = prev_s->extraInfo->pc + (BITS(prev_s->extraInfo->isa.instr.val, 1, 0) == 0x3 ? 4 : 2);
    if(extra->pc == target_tk){
      int new_idx = (prev_idx == EXEC_ID_beq) ? EXEC_ID_beq_tnext :
                    (prev_idx == EXEC_ID_bne) ? EXEC_ID_bne_tnext :
                    (prev_idx == EXEC_ID_blt) ? EXEC_ID_blt_tnext :
                    (prev_idx == EXEC_ID_bge) ? EXEC_ID_bge_tnext :
                    (prev_idx == EXEC_ID_bltu) ? EXEC_ID_bltu_tnext :
                    (prev_idx == EXEC_ID_bgeu) ? EXEC_ID_bgeu_tnext :
                    (prev_idx == EXEC_ID_c_beqz) ? EXEC_ID_c_beqz_tnext :
                    (prev_idx == EXEC_ID_c_bnez) ? EXEC_ID_c_bnez_tnext :
                    (prev_idx == EXEC_ID_p_bltz) ? EXEC_ID_p_bltz_tnext :
                    (prev_idx == EXEC_ID_p_bgez) ? EXEC_ID_p_bgez_tnext :
                    (prev_idx == EXEC_ID_p_blez) ? EXEC_ID_p_blez_tnext :
                    (prev_idx == EXEC_ID_p_bgtz) ? EXEC_ID_p_bgtz_tnext : -1;
      assert(new_idx != -1);
      update_exec_table(prev_s, new_idx);
    } else if(extra->pc == target_nt){
      int new_idx = (prev_idx == EXEC_ID_beq) ? EXEC_ID_beq_ntnext :
                    (prev_idx == EXEC_ID_bne) ? EXEC_ID_bne_ntnext :
                    (prev_idx == EXEC_ID_blt) ? EXEC_ID_blt_ntnext :
                    (prev_idx == EXEC_ID_bge) ? EXEC_ID_bge_ntnext :
                    (prev_idx == EXEC_ID_bltu) ? EXEC_ID_bltu_ntnext :
                    (prev_idx == EXEC_ID_bgeu) ? EXEC_ID_bgeu_ntnext :
                    (prev_idx == EXEC_ID_c_beqz) ? EXEC_ID_c_beqz_ntnext :
                    (prev_idx == EXEC_ID_c_bnez) ? EXEC_ID_c_bnez_ntnext :
                    (prev_idx == EXEC_ID_p_bltz) ? EXEC_ID_p_bltz_ntnext :
                    (prev_idx == EXEC_ID_p_bgez) ? EXEC_ID_p_bgez_ntnext :
                    (prev_idx == EXEC_ID_p_blez) ? EXEC_ID_p_blez_ntnext :
                    (prev_idx == EXEC_ID_p_bgtz) ? EXEC_ID_p_bgtz_ntnext : -1;
      assert(new_idx != -1);
      update_exec_table(prev_s, new_idx);
    }
  }

  prev_s = s;
  prev_idx = idx;
  prev_type_j = 0;
  prev_type_b = 0;

  extra->type = INSTR_TYPE_N;
  switch (idx) {
    case EXEC_ID_c_j: case EXEC_ID_p_jal: case EXEC_ID_jal:
      extra->jnpc = id_src1->imm; extra->type = INSTR_TYPE_J; prev_type_j = 1; break;

    case EXEC_ID_beq: case EXEC_ID_bne: case EXEC_ID_blt: case EXEC_ID_bge:
    case EXEC_ID_bltu: case EXEC_ID_bgeu:
    case EXEC_ID_c_beqz: case EXEC_ID_c_bnez:
    case EXEC_ID_p_bltz: case EXEC_ID_p_bgez: case EXEC_ID_p_blez: case EXEC_ID_p_bgtz:
      extra->jnpc = id_dest->imm; extra->type = INSTR_TYPE_B; prev_type_b = 1; break;

    case EXEC_ID_p_ret: case EXEC_ID_c_jr: case EXEC_ID_c_jalr: case EXEC_ID_jalr:
    case EXEC_ID_p_jalr_ra: case EXEC_ID_p_jalr_t0: case EXEC_ID_p_jalr_ra_noimm:
      extra->type = INSTR_TYPE_I; break;

    case EXEC_ID_system:
      if (BITS(instr, 14, 12) == 0) {
        switch (BITS(instr, 31, 20)) {
          case 0:     // ecall
          case 0x102: // sret
          case 0x302: // mret
            extra->type = INSTR_TYPE_I;
        }
      }
      break;
  }
#endif
  return idx;
}
