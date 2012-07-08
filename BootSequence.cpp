// Copyright (c) 2012, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "BootSequence.h"
#include "Core.h"
#include "Node.h"
#include "SystemState.h"
#include "SyscallHandler.h"
#include "XE.h"
#include "ScopedArray.h"
#include "SymbolInfo.h"
#include "Trace.h"
#include <gelf.h>
#include <iostream>

const unsigned XCORE_ELF_MACHINE = 0xB49E;

static void readSymbols(Elf *e, Elf_Scn *scn, const GElf_Shdr &shdr,
                        unsigned low, unsigned high,
                        std::auto_ptr<CoreSymbolInfo> &SI)
{
  Elf_Data *data = elf_getdata(scn, NULL);
  if (data == NULL) {
    return;
  }
  unsigned count = shdr.sh_size / shdr.sh_entsize;
  
  CoreSymbolInfoBuilder builder;
  
  for (unsigned i = 0; i < count; i++) {
    GElf_Sym sym;
    if (gelf_getsym(data, i, &sym) == NULL) {
      continue;
    }
    if (sym.st_shndx == SHN_ABS)
      continue;
    if (sym.st_value < low || sym.st_value >= high)
      continue;
    builder.addSymbol(elf_strptr(e, shdr.sh_link, sym.st_name),
                      sym.st_value,
                      sym.st_info);
  }
  SI = builder.getSymbolInfo();
}

static void readSymbols(Elf *e, unsigned low, unsigned high,
                        std::auto_ptr<CoreSymbolInfo> &SI)
{
  Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  while ((scn = elf_nextscn(e, scn)) != NULL) {
    if (gelf_getshdr(scn, &shdr) == NULL) {
      continue;
    }
    if (shdr.sh_type == SHT_SYMTAB) {
      // Found the symbol table
      break;
    }
  }

  if (scn != NULL) {
    readSymbols(e, scn, shdr, low, high, SI);
  }
}

class BootSequenceStep {
public:
  enum Type {
    ELF,
    SCHEDULE,
    RUN,
  };
private:
  Type type;
protected:
  BootSequenceStep(Type t) : type(t) {}
public:
  virtual ~BootSequenceStep() {}
  Type getType() const { return type; }
  virtual int execute(SystemState &sys) = 0;
};

class BootSequenceStepElf : public BootSequenceStep {
public:
  Core *core;
  const XEElfSector *elfSector;
  BootSequenceStepElf(Core *c, const XEElfSector *e) :
    BootSequenceStep(ELF),
    core(c),
    elfSector(e) {}
  int execute(SystemState &sys);
};

class BootSequenceStepSchedule : public BootSequenceStep {
public:
  Core *core;
  uint32_t address;
  BootSequenceStepSchedule(Core *c, uint32_t a) :
    BootSequenceStep(SCHEDULE),
    core(c),
    address(a) {}
  int execute(SystemState &sys);
};

class BootSequenceStepRun : public BootSequenceStep {
public:
  unsigned numDoneSyscalls;
  BootSequenceStepRun(unsigned n) :
    BootSequenceStep(RUN),
    numDoneSyscalls(n) {}
  int execute(SystemState &sys);
};

int BootSequenceStepElf::execute(SystemState &sys)
{
  uint64_t ElfSize = elfSector->getElfSize();
  const scoped_array<char> buf(new char[ElfSize]);
  if (!elfSector->getElfData(buf.get())) {
    std::cerr << "Error reading ELF data from ELF sector" << std::endl;
    std::exit(1);
  }
  Elf *e;
  if ((e = elf_memory(buf.get(), ElfSize)) == NULL) {
    std::cerr << "Error reading ELF: " << elf_errmsg(-1) << std::endl;
    std::exit(1);
  }
  if (elf_kind(e) != ELF_K_ELF) {
    std::cerr << "ELF section is not an ELF object" << std::endl;
    std::exit(1);
  }
  GElf_Ehdr ehdr;
  if (gelf_getehdr(e, &ehdr) == NULL) {
    std::cerr << "Reading ELF header failed: " << elf_errmsg(-1) << std::endl;
    std::exit(1);
  }
  if (ehdr.e_machine != XCORE_ELF_MACHINE) {
    std::cerr << "Not a XCore ELF" << std::endl;
    std::exit(1);
  }
  uint32_t entryPoint = core->getRamBase();
  if (ehdr.e_entry != 0) {
    if (core->isValidRamAddress(ehdr.e_entry)) {
      entryPoint = ehdr.e_entry;
    } else {
      std::cout << "Warning: invalid ELF entry point 0x";
      std::cout << std::hex << ehdr.e_entry << std::dec << "\n";
    }
  }
  unsigned num_phdrs = ehdr.e_phnum;
  if (num_phdrs == 0) {
    std::cerr << "No ELF program headers" << std::endl;
    std::exit(1);
  }
  core->resetCaches();
  uint32_t ram_base = core->getRamBase();
  uint32_t ram_size = core->getRamSize();
  for (unsigned i = 0; i < num_phdrs; i++) {
    GElf_Phdr phdr;
    if (gelf_getphdr(e, i, &phdr) == NULL) {
      std::cerr << "Reading ELF program header " << i << " failed: " << elf_errmsg(-1) << std::endl;
      std::exit(1);
    }
    if (phdr.p_filesz == 0) {
      continue;
    }
    if (phdr.p_offset > ElfSize) {
      std::cerr << "Invalid offet in ELF program header" << i << std::endl;
      std::exit(1);
    }
    if (!core->isValidRamAddress(phdr.p_paddr) ||
        !core->isValidRamAddress(phdr.p_paddr + phdr.p_memsz)) {
      std::cerr << "Error data from ELF program header " << i;
      std::cerr << " does not fit in memory" << std::endl;
      std::exit(1);
    }
    core->writeMemory(phdr.p_paddr, &buf[phdr.p_offset], phdr.p_filesz);
  }

  SymbolInfo *SI = Tracer::get().getSymbolInfo();
  std::auto_ptr<CoreSymbolInfo> CSI;
  readSymbols(e, ram_base, ram_base + ram_size, CSI);
  SI->add(core, CSI);

  // Patch in syscall instruction at the syscall address.
  if (const ElfSymbol *syscallSym = SI->getGlobalSymbol(core, "_DoSyscall")) {
    if (!core->setSyscallAddress(syscallSym->value)) {
      std::cout << "Warning: invalid _DoSyscall address "
      << std::hex << syscallSym->value << std::dec << "\n";
    }
  }
  // Patch in exception instruction at the exception address
  if (const ElfSymbol *doExceptionSym = SI->getGlobalSymbol(core, "_DoException")) {
    if (!core->setExceptionAddress(doExceptionSym->value)) {
      std::cout << "Warning: invalid _DoException address "
      << std::hex << doExceptionSym->value << std::dec << "\n";
    }
  }
  sys.schedule(core->getThread(0));
  core->getThread(0).setPcFromAddress(entryPoint);
  return 0;

  elf_end(e);
  return 0;
}

int BootSequenceStepSchedule::execute(SystemState &sys)
{
  sys.schedule(core->getThread(0));
  core->getThread(0).setPcFromAddress(address);
  return 0;
}

int BootSequenceStepRun::execute(SystemState &sys)
{
  SyscallHandler::setDoneSyscallsRequired(numDoneSyscalls);
  return sys.run();
}

BootSequence::~BootSequence() {
  for (std::vector<BootSequenceStep*>::iterator it = steps.begin(),
       e = steps.end(); it != e; ++it) {
    delete *it;
  }
}

void BootSequence::addElf(Core *c, const XEElfSector *elfSector) {
  steps.push_back(new BootSequenceStepElf(c, elfSector));
}

void BootSequence::addSchedule(Core *c, uint32_t address) {
  steps.push_back(new BootSequenceStepSchedule(c, address));
}

void BootSequence::addRun(unsigned numDoneSyscalls) {
  steps.push_back(new BootSequenceStepRun(numDoneSyscalls));
}

int BootSequence::execute() {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    std::cerr << "ELF library intialisation failed: "
    << elf_errmsg(-1) << std::endl;
    std::exit(1);
  }
  for (std::vector<BootSequenceStep*>::iterator it = steps.begin(),
       e = steps.end(); it != e; ++it) {
    int status = (*it)->execute(sys);
    if (status != 0)
      return status;
  }
  return 0;
}