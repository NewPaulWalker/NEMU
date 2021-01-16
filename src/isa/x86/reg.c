#include <isa.h>
#include <stdlib.h>
#include <time.h>
#include "local-include/reg.h"

const char *regsl[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
const char *regsw[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
const char *regsb[] = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};

void reg_test() {
  srand(time(0));
  word_t sample[8];
  word_t pc_sample = rand();
  cpu.pc = pc_sample;

  int i;
  for (i = R_EAX; i <= R_EDI; i ++) {
    sample[i] = rand();
    reg_l(i) = sample[i];
    assert(reg_w(i) == (sample[i] & 0xffff));
  }

  assert(reg_b(R_AL) == (sample[R_EAX] & 0xff));
  assert(reg_b(R_AH) == ((sample[R_EAX] >> 8) & 0xff));
  assert(reg_b(R_BL) == (sample[R_EBX] & 0xff));
  assert(reg_b(R_BH) == ((sample[R_EBX] >> 8) & 0xff));
  assert(reg_b(R_CL) == (sample[R_ECX] & 0xff));
  assert(reg_b(R_CH) == ((sample[R_ECX] >> 8) & 0xff));
  assert(reg_b(R_DL) == (sample[R_EDX] & 0xff));
  assert(reg_b(R_DH) == ((sample[R_EDX] >> 8) & 0xff));

  assert(sample[R_EAX] == cpu.eax);
  assert(sample[R_ECX] == cpu.ecx);
  assert(sample[R_EDX] == cpu.edx);
  assert(sample[R_EBX] == cpu.ebx);
  assert(sample[R_ESP] == cpu.esp);
  assert(sample[R_EBP] == cpu.ebp);
  assert(sample[R_ESI] == cpu.esi);
  assert(sample[R_EDI] == cpu.edi);

  assert(pc_sample == cpu.pc);
}

#ifndef __ICS_EXPORT
#include <memory/vaddr.h>

void isa_reg_display() {
  int i;
  for (i = 0; i < 8; i ++) {
    printf("%s: 0x%08x\n", regsl[i], cpu.gpr[i]._32);
  }
  printf("pc: 0x%08x\n", cpu.pc);
}

word_t isa_reg_str2val(const char *s, bool *success) {
  int i;
  *success = true;
  for (i = 0; i < 8; i ++) {
    if (strcmp(regsl[i], s) == 0) return reg_l(i);
    if (strcmp(regsw[i], s) == 0) return reg_w(i);
    if(strcmp(regsb[i], s) == 0) return reg_b(i);
  }

  if (strcmp("pc", s) == 0) return cpu.pc;

  *success = false;
  return 0;
}

static void load_sreg(int idx, uint16_t val) {
  cpu.sreg[idx].val = val;

  if (val == 0) return;
  uint16_t old_cpl = cpu.sreg[CSR_CS].val;
  cpu.sreg[CSR_CS].rpl = 0; // use ring 0 to index GDT

  assert(cpu.sreg[idx].ti == 0); // check the table bit
  uint32_t desc_base = cpu.gdtr.base + (cpu.sreg[idx].idx << 3);
  uint32_t desc_lo = vaddr_read(desc_base + 0, 4);
  uint32_t desc_hi = vaddr_read(desc_base + 4, 4);
  assert((desc_hi >> 15) & 0x1); // check the present bit
  uint32_t base = (desc_hi & 0xff000000) | ((desc_hi & 0xff) << 16) | (desc_lo >> 16);
  cpu.sreg[idx].base = base;

  cpu.sreg[CSR_CS].rpl = old_cpl;
}

void isa_csrrw(rtlreg_t *dest, const rtlreg_t *src, uint32_t csrid) {
  if (dest != NULL) {
    switch (csrid) {
      case 0 ... CSR_LDTR: *dest = cpu.sreg[csrid].val; break;
      case CSR_CR0 ... CSR_CR4: *dest = cpu.cr[csrid - CSR_CR0]; break;
      default: panic("Reading from CSR = %d is not supported", csrid);
    }
  }
  if (src != NULL) {
    switch (csrid) {
      case CSR_IDTR:
        cpu.idtr.limit = vaddr_read(*src, 2);
        cpu.idtr.base  = vaddr_read(*src + 2, 4);
        break;
      case CSR_GDTR:
        cpu.gdtr.limit = vaddr_read(*src, 2);
        cpu.gdtr.base  = vaddr_read(*src + 2, 4);
        break;
      case 0 ... CSR_LDTR: load_sreg(csrid, *src); break;
      case CSR_CR0 ... CSR_CR4: cpu.cr[csrid - CSR_CR0] = *src; break;
      default: panic("Writing to CSR = %d is not supported", csrid);
    }
  }
}

#else
void isa_reg_display() {
}

word_t isa_reg_str2val(const char *s, bool *success) {
  return 0;
}
#endif
