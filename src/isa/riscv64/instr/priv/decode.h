static inline def_DHelper(csr) {
  decode_op_r(s, id_src1, s->isa.instr.i.rs1, false);
  decode_op_i(s, id_src2, s->isa.instr.csr.csr, true);
  decode_op_r(s, id_dest, s->isa.instr.i.rd, true);
}

#ifdef CONFIG_DEBUG
def_THelper(system) {
  def_INSTR_TAB("000000000000 ????? 000 ????? ????? ??", ecall);
  def_INSTR_TAB("000100000010 ????? 000 ????? ????? ??", sret);
  def_INSTR_TAB("000100000101 ????? 000 ????? ????? ??", wfi);
  def_INSTR_TAB("000100100000 ????? 000 ????? ????? ??", sfence_vma);
  def_INSTR_TAB("001100000010 ????? 000 ????? ????? ??", mret);
  def_INSTR_TAB("???????????? ????? 001 ????? ????? ??", csrrw);
  def_INSTR_TAB("???????????? ????? 010 ????? ????? ??", csrrs);
  def_INSTR_TAB("???????????? ????? 011 ????? ????? ??", csrrc);
  def_INSTR_TAB("???????????? ????? 101 ????? ????? ??", csrrwi);
  def_INSTR_TAB("???????????? ????? 110 ????? ????? ??", csrrsi);
  def_INSTR_TAB("???????????? ????? 111 ????? ????? ??", csrrci);
  return EXEC_ID_inv;
};
#endif