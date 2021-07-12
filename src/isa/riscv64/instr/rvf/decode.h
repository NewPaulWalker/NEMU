bool fp_enable();
static int table_op_fp_d(Decode *s);
static int table_fmadd_d_dispatch(Decode *s);

static inline def_DopHelper(fr){
  op->preg = &fpreg_l(val);
  print_Dop(op->str, OP_STR_SIZE, "%s", fpreg_name(val, 4));
#ifdef CONFIG_RVV_010
  op->reg = val;
#endif // CONFIG_RVV_010
}

static inline def_DHelper(fr) {
  decode_op_fr(s, id_src1, s->isa.instr.fp.rs1, false);
  decode_op_fr(s, id_src2, s->isa.instr.fp.rs2, false);
  decode_op_fr(s, id_dest, s->isa.instr.fp.rd,  false);
}

static inline def_DHelper(R4) {
  decode_op_fr(s, id_src1, s->isa.instr.fp.rs1, false);
  decode_op_fr(s, id_src2, s->isa.instr.fp.rs2, false);
  decode_op_fr(s, id_dest, s->isa.instr.fp.rd,  false);
  // rs3 is decoded at exec.h
}

static inline def_DHelper(fload) {
  decode_op_r(s, id_src1, s->isa.instr.i.rs1, true);
  decode_op_i(s, id_src2, (sword_t)s->isa.instr.i.simm11_0, false);
  decode_op_fr(s, id_dest, s->isa.instr.i.rd, false);
}

static inline def_DHelper(fstore) {
  decode_op_r(s, id_src1, s->isa.instr.s.rs1, true);
  sword_t simm = (s->isa.instr.s.simm11_5 << 5) | s->isa.instr.s.imm4_0;
  decode_op_i(s, id_src2, simm, false);
  decode_op_fr(s, id_dest, s->isa.instr.s.rs2, false);
}

static inline def_DHelper(fr2r){
  decode_op_fr(s, id_src1, s->isa.instr.fp.rs1, true);
  decode_op_fr(s, id_src2, s->isa.instr.fp.rs2, true);
  decode_op_r(s, id_dest, s->isa.instr.fp.rd, false);
}

static inline def_DHelper(r2fr){
  decode_op_r(s, id_src1, s->isa.instr.fp.rs1, true);
  decode_op_r(s, id_src2, s->isa.instr.fp.rs2, true);
  decode_op_fr(s, id_dest, s->isa.instr.fp.rd, false);
}

#ifdef CONFIG_RVV_010

def_THelper(vload) {
  def_INSTR_TAB("??? 000 ? ????? ????? ??? ????? ????? ??", vlduu);
  def_INSTR_TAB("??? 010 ? ????? ????? ??? ????? ????? ??", vldsu);
  def_INSTR_TAB("??? 011 ? ????? ????? ??? ????? ????? ??", vldxu);
  def_INSTR_TAB("??? 100 ? ????? ????? ??? ????? ????? ??", vldus);
  def_INSTR_TAB("??? 110 ? ????? ????? ??? ????? ????? ??", vldss);
  def_INSTR_TAB("??? 111 ? ????? ????? ??? ????? ????? ??", vldxs);
  return EXEC_ID_inv;
}

def_THelper(vstore) {
  def_INSTR_TAB("??? 000 ? ????? ????? ??? ????? ????? ??", vstu);
  def_INSTR_TAB("??? 010 ? ????? ????? ??? ????? ????? ??", vsts);
  def_INSTR_TAB("??? 011 ? ????? ????? ??? ????? ????? ??", vstx);
  def_INSTR_TAB("??? 111 ? ????? ????? ??? ????? ????? ??", vstxu);
  return EXEC_ID_inv;
}

#endif

def_THelper(fload) {
  if (!fp_enable()) return table_rt_inv(s);
  print_Dop(id_src1->str, OP_STR_SIZE, "%ld(%s)", id_src2->imm, reg_name(s->isa.instr.i.rs1, 4));
  
  #ifdef CONFIG_RVV_010
  const int table [8] = {1, 0, 0, 0, 0, 2, 4, 0};
  s->vm = s->isa.instr.v_opv1.v_vm; //1 for without mask; 0 for with mask
  s->v_width = table[s->isa.instr.vldfp.v_width];
  #endif // CONFIG_RVV_010

  int mmu_mode = isa_mmu_state();
  if (mmu_mode == MMU_DIRECT) {
    def_INSTR_TAB("??????? ????? ????? 010 ????? ????? ??", flw);
    def_INSTR_TAB("??????? ????? ????? 011 ????? ????? ??", fld);
    #ifdef CONFIG_RVV_010
    def_INSTR_TAB("??????? ????? ????? 000 ????? ????? ??", vload);
    def_INSTR_TAB("??????? ????? ????? 101 ????? ????? ??", vload);
    def_INSTR_TAB("??????? ????? ????? 110 ????? ????? ??", vload);
    #endif // CONFIG_RVV_010
  } else if (mmu_mode == MMU_TRANSLATE) {
    def_INSTR_TAB("??????? ????? ????? 010 ????? ????? ??", flw_mmu);
    def_INSTR_TAB("??????? ????? ????? 011 ????? ????? ??", fld_mmu);
    #ifdef CONFIG_RVV_010
    // todo: use mmu here
    def_INSTR_TAB("??????? ????? ????? 000 ????? ????? ??", vload);
    def_INSTR_TAB("??????? ????? ????? 101 ????? ????? ??", vload);
    def_INSTR_TAB("??????? ????? ????? 110 ????? ????? ??", vload);
    #endif // CONFIG_RVV_010
  } else { assert(0); }
  return EXEC_ID_inv;
}

def_THelper(fstore) {
  if (!fp_enable()) return table_rt_inv(s);
  print_Dop(id_src1->str, OP_STR_SIZE, "%ld(%s)", id_src2->imm, reg_name(s->isa.instr.i.rs1, 4));

  #ifdef CONFIG_RVV_010
  const int table [8] = {1, 0, 0, 0, 0, 2, 4, 0};
  s->vm = s->isa.instr.v_opv1.v_vm; //1 for without mask; 0 for with mask
  s->v_width = table[s->isa.instr.vldfp.v_width];
  #endif // CONFIG_RVV_010

  int mmu_mode = isa_mmu_state();
  if (mmu_mode == MMU_DIRECT) {
    def_INSTR_TAB("??????? ????? ????? 010 ????? ????? ??", fsw);
    def_INSTR_TAB("??????? ????? ????? 011 ????? ????? ??", fsd);
    #ifdef CONFIG_RVV_010
    def_INSTR_TAB("??????? ????? ????? 000 ????? ????? ??", vstore);
    def_INSTR_TAB("??????? ????? ????? 101 ????? ????? ??", vstore);
    def_INSTR_TAB("??????? ????? ????? 110 ????? ????? ??", vstore);
    #endif // CONFIG_RVV_010
  } else if (mmu_mode == MMU_TRANSLATE) {
    def_INSTR_TAB("??????? ????? ????? 010 ????? ????? ??", fsw_mmu);
    def_INSTR_TAB("??????? ????? ????? 011 ????? ????? ??", fsd_mmu);
    #ifdef CONFIG_RVV_010
    // todo: use mmu here
    def_INSTR_TAB("??????? ????? ????? 000 ????? ????? ??", vstore);
    def_INSTR_TAB("??????? ????? ????? 101 ????? ????? ??", vstore);
    def_INSTR_TAB("??????? ????? ????? 110 ????? ????? ??", vstore);
    #endif // CONFIG_RVV_010
  } else { assert(0); }
  return EXEC_ID_inv;
}

def_THelper(op_fp) {
  if (!fp_enable()) return table_rt_inv(s);

  if ((s->isa.instr.fp.fmt == 0b00 && s->isa.instr.fp.funct5 == 0b01000) ||
      s->isa.instr.fp.fmt == 0b01) return table_op_fp_d(s);

  // RV32F
  def_INSTR_IDTAB("00000 00 ????? ????? ??? ????? ????? ??", fr  , fadds);
  def_INSTR_IDTAB("00001 00 ????? ????? ??? ????? ????? ??", fr  , fsubs);
  def_INSTR_IDTAB("00010 00 ????? ????? ??? ????? ????? ??", fr  , fmuls);
  def_INSTR_IDTAB("00011 00 ????? ????? ??? ????? ????? ??", fr  , fdivs);
  def_INSTR_IDTAB("01011 00 00000 ????? ??? ????? ????? ??", fr  , fsqrts);
  def_INSTR_IDTAB("00100 00 ????? ????? 000 ????? ????? ??", fr  , fsgnjs);
  def_INSTR_IDTAB("00100 00 ????? ????? 001 ????? ????? ??", fr  , fsgnjns);
  def_INSTR_IDTAB("00100 00 ????? ????? 010 ????? ????? ??", fr  , fsgnjxs);
  def_INSTR_IDTAB("00101 00 ????? ????? 000 ????? ????? ??", fr  , fmins);
  def_INSTR_IDTAB("00101 00 ????? ????? 001 ????? ????? ??", fr  , fmaxs);
  def_INSTR_IDTAB("11000 00 00000 ????? ??? ????? ????? ??", fr2r, fcvt_w_s);
  def_INSTR_IDTAB("11000 00 00001 ????? ??? ????? ????? ??", fr2r, fcvt_wu_s);
  def_INSTR_IDTAB("11100 00 00000 ????? 000 ????? ????? ??", fr2r, fmv_x_w);
  def_INSTR_IDTAB("10100 00 ????? ????? 010 ????? ????? ??", fr2r, feqs);
  def_INSTR_IDTAB("10100 00 ????? ????? 001 ????? ????? ??", fr2r, flts);
  def_INSTR_IDTAB("10100 00 ????? ????? 000 ????? ????? ??", fr2r, fles);
//def_INSTR_IDTAB("11100 00 00000 ????? 001 ????? ????? ??", fr2r, fclasss);
  def_INSTR_IDTAB("11010 00 00000 ????? ??? ????? ????? ??", r2fr, fcvt_s_w);
  def_INSTR_IDTAB("11010 00 00001 ????? ??? ????? ????? ??", r2fr, fcvt_s_wu);
  def_INSTR_IDTAB("11110 00 00000 ????? 000 ????? ????? ??", r2fr, fmv_w_x);

  // RV32F
  def_INSTR_IDTAB("11000 00 00010 ????? ??? ????? ????? ??", fr2r, fcvt_l_s);
  def_INSTR_IDTAB("11000 00 00011 ????? ??? ????? ????? ??", fr2r, fcvt_lu_s);
  def_INSTR_IDTAB("11010 00 00010 ????? ??? ????? ????? ??", r2fr, fcvt_s_l);
  def_INSTR_IDTAB("11010 00 00011 ????? ??? ????? ????? ??", r2fr, fcvt_s_lu);

  return EXEC_ID_inv;
}

def_THelper(fmadd_dispatch) {
  if (!fp_enable()) return table_rt_inv(s);
  def_INSTR_TAB("????? 01 ????? ????? ??? ????? ????? ??", fmadd_d_dispatch);

  def_INSTR_TAB("????? 00 ????? ????? ??? ????? 10000 ??", fmadds);
  def_INSTR_TAB("????? 00 ????? ????? ??? ????? 10001 ??", fmsubs);
  def_INSTR_TAB("????? 00 ????? ????? ??? ????? 10010 ??", fnmsubs);
  def_INSTR_TAB("????? 00 ????? ????? ??? ????? 10011 ??", fnmadds);
  return EXEC_ID_inv;
}
