//===- ClangOpenCLBuiltinEmitter.cpp - Generate Clang OpenCL Builtin handling
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits code for checking whether a function is an
// OpenCL builtin function. If so, all overloads of this function are
// added to the LookupResult. The generated include file is used by
// SemaLookup.cpp
//
// For a successful lookup of e.g. the "cos" builtin, isOpenCLBuiltin("cos")
// returns a pair <Index, Len>.
// OpenCLBuiltins[Index] to OpenCLBuiltins[Index + Len] contains the pairs
// <SigIndex, SigLen> of the overloads of "cos".
// OpenCLSignature[SigIndex] to OpenCLSignature[SigIndex + SigLen] contains
// one of the signatures of "cos". The OpenCLSignature entry can be
// referenced by other functions, i.e. "sin", since multiple OpenCL builtins
// share the same signature.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <set>

using namespace llvm;

namespace {
class BuiltinNameEmitter {
public:
  BuiltinNameEmitter(RecordKeeper &Records, raw_ostream &OS)
      : Records(Records), OS(OS) {}

  // Entrypoint to generate the functions and structures for checking
  // whether a function is an OpenCL builtin function.
  void Emit();

private:
  // Contains OpenCL builtin functions and related information, stored as
  // Record instances. They are coming from the associated TableGen file.
  RecordKeeper &Records;

  // The output file.
  raw_ostream &OS;

  // Emit the enums and structs.
  void EmitDeclarations();

  // Parse the Records generated by TableGen and populate OverloadInfo and
  // SignatureSet.
  void GetOverloads();

  // Emit the OpenCLSignature table. This table contains all possible
  // signatures, and is a struct OpenCLType. A signature is composed of a
  // return type (mandatory), followed by zero or more argument types.
  // E.g.:
  // // 12
  // { OCLT_uchar, 4, clang::LangAS::Default, false },
  // { OCLT_float, 4, clang::LangAS::Default, false },
  // This means that index 12 represents a signature
  //   - returning a uchar vector of 4 elements, and
  //   - taking as first argument a float vector of 4 elements.
  void EmitSignatureTable();

  // Emit the OpenCLBuiltins table. This table contains all overloads of
  // each function, and is a struct OpenCLBuiltinDecl.
  // E.g.:
  // // acos
  //   { 2, 0, "", 100 },
  // This means that the signature of this acos overload is defined in OpenCL
  // version 1.0 (100) and does not belong to any extension ("").  It has a
  // 1 argument (+1 for the return type), stored at index 0 in the
  // OpenCLSignature table.
  void EmitBuiltinTable();

  // Emit a StringMatcher function to check whether a function name is an
  // OpenCL builtin function name.
  void EmitStringMatcher();

  // Emit a function returning the clang QualType instance associated with
  // the TableGen Record Type.
  void EmitQualTypeFinder();

  // Contains a list of the available signatures, without the name of the
  // function. Each pair consists of a signature and a cumulative index.
  // E.g.:  <<float, float>, 0>,
  //        <<float, int, int, 2>>,
  //        <<float>, 5>,
  //        ...
  //        <<double, double>, 35>.
  std::vector<std::pair<std::vector<Record *>, unsigned>> SignatureSet;

  // Map the name of a builtin function to its prototypes (instances of the
  // TableGen "Builtin" class).
  // Each prototype is registered as a pair of:
  //   <pointer to the "Builtin" instance,
  //    cumulative index of the associated signature in the SignatureSet>
  // E.g.:  The function cos: (float cos(float), double cos(double), ...)
  //        <"cos", <<ptrToPrototype0, 5>,
  //                <ptrToPrototype1, 35>>
  //                <ptrToPrototype2, 79>>
  // ptrToPrototype1 has the following signature: <double, double>
  MapVector<StringRef, std::vector<std::pair<const Record *, unsigned>>>
      OverloadInfo;
};
} // namespace

void BuiltinNameEmitter::Emit() {
  emitSourceFileHeader("OpenCL Builtin handling", OS);

  OS << "#include \"llvm/ADT/StringRef.h\"\n";
  OS << "using namespace clang;\n\n";

  EmitDeclarations();

  GetOverloads();

  EmitSignatureTable();

  EmitBuiltinTable();

  EmitStringMatcher();

  EmitQualTypeFinder();
}

void BuiltinNameEmitter::EmitDeclarations() {
  OS << "enum OpenCLTypeID {\n";
  std::vector<Record *> Types = Records.getAllDerivedDefinitions("Type");
  StringMap<bool> TypesSeen;
  for (const auto *T : Types) {
    if (TypesSeen.find(T->getValueAsString("Name")) == TypesSeen.end())
      OS << "  OCLT_" + T->getValueAsString("Name") << ",\n";
    TypesSeen.insert(std::make_pair(T->getValueAsString("Name"), true));
  }
  OS << "};\n";

  OS << R"(

// Type used in a prototype of an OpenCL builtin function.
struct OpenCLType {
  // A type (e.g.: float, int, ...)
  OpenCLTypeID ID;
  // Size of vector (if applicable)
  unsigned VectorWidth;
  // Address space of the pointer (if applicable)
  LangAS AS;
  // Whether the type is a pointer
  bool isPointer;
};

// One overload of an OpenCL builtin function.
struct OpenCLBuiltinDecl {
  // Number of arguments for the signature
  unsigned NumArgs;
  // Index in the OpenCLSignature table to get the required types
  unsigned ArgTableIndex;
  // Extension to which it belongs (e.g. cl_khr_subgroups)
  const char *Extension;
  // Version in which it was introduced (e.g. CL20)
  unsigned Version;
};

)";
}

void BuiltinNameEmitter::GetOverloads() {
  unsigned CumulativeSignIndex = 0;
  std::vector<Record *> Builtins = Records.getAllDerivedDefinitions("Builtin");
  for (const auto *B : Builtins) {
    StringRef BName = B->getValueAsString("Name");
    if (OverloadInfo.find(BName) == OverloadInfo.end()) {
      OverloadInfo.insert(std::make_pair(
          BName, std::vector<std::pair<const Record *, unsigned>>{}));
    }

    auto Signature = B->getValueAsListOfDefs("Signature");
    auto it =
        std::find_if(SignatureSet.begin(), SignatureSet.end(),
                     [&](const std::pair<std::vector<Record *>, unsigned> &a) {
                       return a.first == Signature;
                     });
    unsigned SignIndex;
    if (it == SignatureSet.end()) {
      SignatureSet.push_back(std::make_pair(Signature, CumulativeSignIndex));
      SignIndex = CumulativeSignIndex;
      CumulativeSignIndex += Signature.size();
    } else {
      SignIndex = it->second;
    }
    OverloadInfo[BName].push_back(std::make_pair(B, SignIndex));
  }
}

void BuiltinNameEmitter::EmitSignatureTable() {
  OS << "OpenCLType OpenCLSignature[] = {\n";
  for (auto &P : SignatureSet) {
    OS << "// " << P.second << "\n";
    for (Record *R : P.first) {
      OS << "{ OCLT_" << R->getValueAsString("Name") << ", "
         << R->getValueAsInt("VecWidth") << ", "
         << R->getValueAsString("AddrSpace") << ", "
         << R->getValueAsBit("IsPointer") << "},";
      OS << "\n";
    }
  }
  OS << "};\n\n";
}

void BuiltinNameEmitter::EmitBuiltinTable() {
  OS << "OpenCLBuiltinDecl OpenCLBuiltins[] = {\n";
  for (auto &i : OverloadInfo) {
    StringRef Name = i.first;
    OS << "// " << Name << "\n";
    for (auto &Overload : i.second) {
      OS << "  { " << Overload.first->getValueAsListOfDefs("Signature").size()
         << ", " << Overload.second << ", " << '"'
         << Overload.first->getValueAsString("Extension") << "\", "
         << Overload.first->getValueAsDef("Version")->getValueAsInt("Version")
         << " },\n";
    }
  }
  OS << "};\n\n";
}

void BuiltinNameEmitter::EmitStringMatcher() {
  std::vector<StringMatcher::StringPair> ValidBuiltins;
  unsigned CumulativeIndex = 1;
  for (auto &i : OverloadInfo) {
    auto &Ov = i.second;
    std::string RetStmt;
    raw_string_ostream SS(RetStmt);
    SS << "return std::make_pair(" << CumulativeIndex << ", " << Ov.size()
       << ");";
    SS.flush();
    CumulativeIndex += Ov.size();

    ValidBuiltins.push_back(StringMatcher::StringPair(i.first, RetStmt));
  }

  OS << R"(
// Return 0 if name is not a recognized OpenCL builtin, or an index
// into a table of declarations if it is an OpenCL builtin.
std::pair<unsigned, unsigned> isOpenCLBuiltin(llvm::StringRef name) {

)";

  StringMatcher("name", ValidBuiltins, OS).Emit(0, true);

  OS << "  return std::make_pair(0, 0);\n";
  OS << "}\n";
}

void BuiltinNameEmitter::EmitQualTypeFinder() {
  OS << R"(

static QualType OCL2Qual(ASTContext &Context, OpenCLType Ty) {
  QualType RT = Context.VoidTy;
  switch (Ty.ID) {
)";

  std::vector<Record *> Types = Records.getAllDerivedDefinitions("Type");
  StringMap<bool> TypesSeen;

  for (const auto *T : Types) {
    // Check we have not seen this Type
    if (TypesSeen.find(T->getValueAsString("Name")) != TypesSeen.end())
      continue;
    TypesSeen.insert(std::make_pair(T->getValueAsString("Name"), true));

    // Check the Type does not have an "abstract" QualType
    auto QT = T->getValueAsDef("QTName");
    if (QT->getValueAsString("Name") == "null")
      continue;

    OS << "  case OCLT_" << T->getValueAsString("Name") << ":\n";
    OS << "    RT = Context." << QT->getValueAsString("Name") << ";\n";
    OS << "    break;\n";
  }
  OS << "  }\n";

  // Special cases
  OS << R"(
  if (Ty.VectorWidth > 0)
    RT = Context.getExtVectorType(RT, Ty.VectorWidth);

  if (Ty.isPointer) {
    RT = Context.getAddrSpaceQualType(RT, Ty.AS);
    RT = Context.getPointerType(RT);
  }

  return RT;
}
)";
}

namespace clang {

void EmitClangOpenCLBuiltins(RecordKeeper &Records, raw_ostream &OS) {
  BuiltinNameEmitter NameChecker(Records, OS);
  NameChecker.Emit();
}

} // end namespace clang