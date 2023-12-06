/***************************************************************************************
* Copyright (c) 2014-2021 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

def_THelper(rvm) {
  // def_INSTR_TAB("??????? ????? ????? 000 ????? ????? ??", mul);
  // def_INSTR_TAB("??????? ????? ????? 001 ????? ????? ??", mulh);
  def_INSTR_TAB("??????? ????? ????? 010 ????? ????? ??", mulhsu);
  // def_INSTR_TAB("??????? ????? ????? 011 ????? ????? ??", mulhu);
  def_INSTR_TAB("??????? ????? ????? 100 ????? ????? ??", div);
  // def_INSTR_TAB("??????? ????? ????? 101 ????? ????? ??", divu);
  def_INSTR_TAB("??????? ????? ????? 110 ????? ????? ??", rem);
  // def_INSTR_TAB("??????? ????? ????? 111 ????? ????? ??", remu);
  return EXEC_ID_inv;
}

def_THelper(rvm32) {
  def_INSTR_TAB("??????? ????? ????? 000 ????? ????? ??", mulw);
  // def_INSTR_TAB("??????? ????? ????? 100 ????? ????? ??", divw);
  // def_INSTR_TAB("??????? ????? ????? 101 ????? ????? ??", divuw);
  def_INSTR_TAB("??????? ????? ????? 110 ????? ????? ??", remw);
  // def_INSTR_TAB("??????? ????? ????? 111 ????? ????? ??", remuw);
  return EXEC_ID_inv;
}
