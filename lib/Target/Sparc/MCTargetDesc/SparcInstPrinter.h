//===-- SparcInstPrinter.h - Convert Sparc MCInst to assembly syntax ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an Sparc MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCINSTPRINTER_H
#define LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class SparcInstPrinter : public MCInstPrinter {
public:
  SparcInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                   const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printRegName(raw_ostream &OS, unsigned RegNo) const override;
  void printInst(const MCInst *MI, raw_ostream &O, StringRef Annot,
                 const MCSubtargetInfo &STI) override;
  bool printSparcAliasInstr(const MCInst *MI, const MCSubtargetInfo &STI,
                            raw_ostream &OS);
  bool isV9(const MCSubtargetInfo &STI) const;

  // Autogenerated by tblgen.
  void printInstruction(const MCInst *MI, const MCSubtargetInfo &STI,
                        raw_ostream &O);
  bool printAliasInstr(const MCInst *MI, const MCSubtargetInfo &STI,
                       raw_ostream &O);
  void printCustomAliasOperand(const MCInst *MI, unsigned OpIdx,
                               unsigned PrintMethodIdx,
                               const MCSubtargetInfo &STI, raw_ostream &O);
  static const char *getRegisterName(unsigned RegNo);

  void printOperand(const MCInst *MI, int opNum, const MCSubtargetInfo &STI,
                    raw_ostream &OS);
  void printMemOperand(const MCInst *MI, int opNum, const MCSubtargetInfo &STI,
                       raw_ostream &OS, const char *Modifier = nullptr);
  void printCCOperand(const MCInst *MI, int opNum, const MCSubtargetInfo &STI,
                      raw_ostream &OS);
  bool printGetPCX(const MCInst *MI, unsigned OpNo, const MCSubtargetInfo &STI,
                   raw_ostream &OS);
  void printMembarTag(const MCInst *MI, int opNum, const MCSubtargetInfo &STI,
                      raw_ostream &O);
};
} // end namespace llvm

#endif