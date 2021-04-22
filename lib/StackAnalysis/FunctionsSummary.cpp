/// \file FunctionsSummary.cpp
/// \brief Implementation of the classes representing an argument/return value
///        in a register

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "boost/icl/interval_set.hpp"

#include "revng/StackAnalysis/FunctionsSummary.h"
#include "revng/Support/IRHelpers.h"

#include "ASSlot.h"

using llvm::BasicBlock;
using llvm::BlockAddress;
using llvm::CallInst;
using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::GlobalVariable;
using llvm::Instruction;
using llvm::MDNode;
using llvm::MDString;
using llvm::MDTuple;
using llvm::Metadata;
using llvm::Module;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::User;

namespace StackAnalysis {

template class RegisterArgument<true>;
template class RegisterArgument<false>;

using FRA = FunctionRegisterArgument;
using FCRA = FunctionCallRegisterArgument;

template<>
void FRA::combine(const FCRA &Other) {
  // TODO: we're handling this as a special case
  if (Value == No || Other.Value == FunctionCallRegisterArgument::No) {
    Value = No;
    return;
  }

  revng_assert(Other.Value == FunctionCallRegisterArgument::Maybe
               || Other.Value == FunctionCallRegisterArgument::Yes);

  revng_assert(Value == NoOrDead || Value == Maybe || Value == Contradiction
               || Value == Yes || Value == Dead);

  if (Other.Value == FunctionCallRegisterArgument::Yes) {
    switch (Value) {
    case NoOrDead:
      Value = Dead;
      break;
    case Maybe:
      Value = Yes;
      break;
    case Contradiction:
      Value = Contradiction;
      break;
    case Yes:
      Value = Yes;
      break;
    case Dead:
      Value = Dead;
      break;
    case No:
      revng_abort();
    }
  } else {
    switch (Value) {
    case NoOrDead:
      Value = NoOrDead;
      break;
    case Maybe:
      Value = Maybe;
      break;
    case Contradiction:
      Value = Contradiction;
      break;
    case Yes:
      Value = Yes;
      break;
    case Dead:
      Value = Dead;
      break;
    case No:
      revng_abort();
    }
  }
}

template<>
void FCRA::combine(const FRA &Other) {
  // TODO: we're handling this as a special case
  if (Value == No || Other.Value == FunctionRegisterArgument::No) {
    Value = No;
    return;
  }

  revng_assert(Value == Maybe || Value == Yes);

  revng_assert(Other.Value == FunctionRegisterArgument::NoOrDead
               || Other.Value == FunctionRegisterArgument::Maybe
               || Other.Value == FunctionRegisterArgument::Contradiction
               || Other.Value == FunctionRegisterArgument::Yes);

  if (Value == Yes) {
    switch (Other.Value) {
    case FunctionRegisterArgument::NoOrDead:
      Value = Dead;
      break;
    case FunctionRegisterArgument::Maybe:
      Value = Yes;
      break;
    case FunctionRegisterArgument::Contradiction:
      Value = Contradiction;
      break;
    case FunctionRegisterArgument::Yes:
      Value = Yes;
      break;
    default:
      revng_abort();
    }
  } else {
    switch (Other.Value) {
    case FunctionRegisterArgument::NoOrDead:
      Value = NoOrDead;
      break;
    case FunctionRegisterArgument::Maybe:
      Value = Maybe;
      break;
    case FunctionRegisterArgument::Contradiction:
      Value = Contradiction;
      break;
    case FunctionRegisterArgument::Yes:
      Value = Yes;
      break;
    default:
      revng_abort();
    }
  }
}

void FunctionReturnValue::combine(const FunctionCallReturnValue &Other) {
  revng_abort();
}

void FunctionCallReturnValue::combine(const FunctionReturnValue &Other) {
  // TODO: we're handling this as a special case
  if (Value == No || Other.Value == FunctionReturnValue::No) {
    Value = No;
    return;
  }

  // *this has seen only URVOF, which can only have Maybe or Yes value
  revng_assert(Other.Value == FunctionReturnValue::Maybe
               || Other.Value == FunctionReturnValue::YesOrDead);

  // Other is affected by URVOFC and DRVOFC, so that possible states are Maybe,
  // NoOrDead, Yes and Contradiction
  revng_assert(Value == Maybe || Value == NoOrDead || Value == Yes
               || Value == Contradiction);

  if (Other.Value == FunctionReturnValue::YesOrDead) {
    switch (Value) {
    case Maybe:
      Value = Yes;
      break;
    case NoOrDead:
      Value = Dead;
      break;
    case Yes:
      Value = Yes;
      break;
    case Contradiction:
      Value = Contradiction;
      break;
    default:
      revng_abort();
    }
  } else {
    switch (Value) {
    case Maybe:
      Value = Maybe;
      break;
    case NoOrDead:
      Value = NoOrDead;
      break;
    case Yes:
      Value = Yes;
      break;
    case Contradiction:
      Value = Contradiction;
      break;
    default:
      revng_abort();
    }
  }
}

template<typename T, typename F>
static auto sort(const T &Range, const F &getKey) {
  using pointer = decltype(&*Range.begin());
  std::vector<pointer> Sorted;

  for (auto &E : Range) {
    Sorted.push_back(&E);
  }

  auto Comparator = [&getKey](pointer &LHS, pointer &RHS) {
    return getKey(LHS) < getKey(RHS);
  };

  std::sort(Sorted.begin(), Sorted.end(), Comparator);

  return Sorted;
}

template<typename T>
static auto sortByCSVName(const T &Range) {
  using pointer = decltype(&*Range.begin());
  auto SortKey = [](pointer P) { return P->first->getName(); };
  return sort(Range, SortKey);
}

void FunctionsSummary::dumpInternal(const Module *M,
                                    StreamWrapperBase &&Stream) const {
  std::stringstream Output;

  // Register the range of addresses covered by each basic block
  using interval_set = boost::icl::interval_set<MetaAddress>;
  using interval = boost::icl::interval<MetaAddress>;
  std::map<BasicBlock *, interval_set> Coverage;
  for (User *U : M->getFunction("newpc")->users()) {
    auto *Call = dyn_cast<CallInst>(U);
    if (Call == nullptr)
      continue;

    BasicBlock *BB = Call->getParent();
    auto Address = MetaAddress::fromConstant(Call->getOperand(0));
    uint64_t Size = getLimitedValue(Call->getOperand(1));
    revng_assert(Address.isValid() && Size > 0);

    Coverage[BB] += interval::right_open(Address, Address + Size);
  }

  // Sort the functions by name, for extra determinism!
  using Pair = std::pair<BasicBlock *, const FunctionDescription *>;
  std::vector<Pair> SortedFunctions;
  for (auto &P : Functions)
    SortedFunctions.push_back({ P.first, &P.second });
  auto Compare = [](const Pair &A, const Pair &B) {
    return getName(A.first) < getName(B.first);
  };
  std::sort(SortedFunctions.begin(), SortedFunctions.end(), Compare);

  const char *FunctionDelimiter = "";
  Output << "[";
  for (auto &P : SortedFunctions) {
    Output << FunctionDelimiter << "\n  {\n";
    BasicBlock *Entry = P.first;
    const FunctionDescription &Function = *P.second;

    Output << "    \"entry_point\": \"";
    if (Entry != nullptr)
      Output << getName(Entry);
    Output << "\",\n";
    Output << "    \"entry_point_address\": \"";
    if (Entry != nullptr)
      Output << std::hex << "0x" << getBasicBlockPC(Entry).address();
    Output << "\",\n";

    Output << "    \"jt-reasons\": [";
    if (Entry != nullptr) {
      const char *JTReasonsDelimiter = "";
      Instruction *T = Entry->getTerminator();
      revng_assert(T != nullptr);
      MDNode *Node = T->getMetadata("revng.jt.reasons");
      SmallVector<StringRef, 4> Reasons;
      if (auto *Tuple = cast_or_null<MDTuple>(Node)) {
        // Collect reasons
        for (Metadata *ReasonMD : Tuple->operands())
          Reasons.push_back(cast<MDString>(ReasonMD)->getString());

        // Sort the output to make it more deterministic
        std::sort(Reasons.begin(), Reasons.end());

        // Print out
        for (StringRef Reason : Reasons) {
          Output << JTReasonsDelimiter << "\"" << Reason.data() << "\"";
          JTReasonsDelimiter = ", ";
        }
      }
    }
    Output << "],\n";

    Output << "    \"type\": \"" << getName(Function.Type) << "\",\n";

    interval_set FunctionCoverage;

    const char *BasicBlockDelimiter = "";
    Output << "    \"basic_blocks\": [";

    // Sort basic blocks by name
    using Pair = std::pair<BasicBlock *const, BranchType::Values>;
    std::vector<const Pair *> SortedBasicBlocks;
    for (const Pair &P : Function.BasicBlocks)
      SortedBasicBlocks.push_back(&*Function.BasicBlocks.find(P.first));
    auto Compare = [](const Pair *P, const Pair *Q) {
      return P->first->getName() < Q->first->getName();
    };
    std::sort(SortedBasicBlocks.begin(), SortedBasicBlocks.end(), Compare);

    for (const Pair *P : SortedBasicBlocks) {
      BasicBlock *BB = P->first;
      BranchType::Values Type = P->second;
      const char *TypeName = BranchType::getName(Type);
      Output << BasicBlockDelimiter;
      Output << "{\"name\": \"" << getName(BB) << "\", ";
      Output << "\"type\": \"" << TypeName << "\", ";
      auto It = Coverage.find(BB);
      if (It != Coverage.end()) {
        const interval_set &IntervalSet = It->second;
        FunctionCoverage += IntervalSet;
        revng_assert(IntervalSet.iterative_size() == 1);
        const auto &Range = *(IntervalSet.begin());
        Output << "\"start\": \"";
        Output << std::hex << "0x" << Range.lower().address();
        Output << "\", \"end\": \"";
        Output << std::hex << "0x" << Range.upper().address();
        Output << "\"";
      } else {
        Output << "\"start\": \"\", \"end\": \"\"";
      }
      Output << "}";
      BasicBlockDelimiter = ", ";
    }
    Output << "],\n";

    Output << "    \"slots\": [";
    const char *SlotDelimiter = "";
    for (auto *P : sortByCSVName(Function.RegisterSlots)) {
      auto &[CSV, RD] = *P;
      Output << SlotDelimiter;
      Output << "{\"slot\": \"" << CSV->getName().data() << "\", ";

      Output << "\"argument\": \"";
      RD.Argument.dump(Output);
      Output << "\", ";
      Output << "\"return_value\": \"";
      RD.ReturnValue.dump(Output);
      Output << "\"}";
      SlotDelimiter = ", ";
    }
    Output << "],\n";

    Output << "    \"clobbered\": [";
    const char *ClobberedDelimiter = "";
    for (const GlobalVariable *CSV : Function.ClobberedRegisters) {
      Output << ClobberedDelimiter;
      Output << "\"" << CSV->getName().data() << "\"";
      ClobberedDelimiter = ", ";
    }
    Output << "],\n";

    const char *CoverageDelimiter = "";
    Output << "    \"coverage\": [";
    for (const auto &Range : FunctionCoverage) {
      Output << CoverageDelimiter;
      Output << "{";
      Output << "\"start\": \"" << std::hex << "0x" << Range.lower().address();
      Output << "\", ";
      Output << "\"end\": \"" << std::hex << "0x" << Range.upper().address();
      Output << "\"}";
      CoverageDelimiter = ", ";
    }
    Output << "],\n";

    const char *FunctionCallDelimiter = "";
    Output << "    \"function_calls\": [";
    for (const CallSiteDescription &CallSite : Function.CallSites) {
      Output << FunctionCallDelimiter << "\n";
      Output << "      {\n";
      Output << "        \"caller\": ";
      Output << "\"" << getName(CallSite.Call) << "\",\n";
      Output << "        \"callee\": ";
      Output << "\"" << getName(CallSite.Callee) << "\",\n";
      // TODO: caller address
      // TODO: callee address
      Output << "        \"slots\": [";
      const char *FunctionCallSlotsDelimiter = "";
      for (auto *P : sortByCSVName(CallSite.RegisterSlots)) {
        auto &[CSV, RD] = *P;
        Output << FunctionCallSlotsDelimiter;
        Output << "{\"slot\": \"" << CSV->getName().data() << "\", ";

        Output << "\"argument\": \"";
        RD.Argument.dump(Output);
        Output << "\", ";
        Output << "\"return_value\": \"";
        RD.ReturnValue.dump(Output);
        Output << "\"}";

        FunctionCallSlotsDelimiter = ", ";
      }
      Output << "]\n";

      Output << "      }";
      FunctionCallDelimiter = ",";
    }
    Output << "\n    ]\n";

    Output << "  }";
    FunctionDelimiter = ",";

    Stream.flush(Output);
  }
  Output << "\n]\n";

  Stream.flush(Output);
}

using CSD = FunctionsSummary::CallSiteDescription;
GlobalVariable *
CSD::isCompatibleWith(const FunctionDescription &Function) const {
  std::set<GlobalVariable *> Slots;
  for (auto &P : RegisterSlots)
    Slots.insert(P.first);
  for (auto &P : Function.RegisterSlots)
    Slots.insert(P.first);

  for (GlobalVariable *CSV : Slots) {
    FunctionCallRegisterDescription FCRD = getOrDefault(RegisterSlots, CSV);
    FunctionRegisterDescription FRD = getOrDefault(Function.RegisterSlots, CSV);
    if (not FCRD.isCompatibleWith(FRD))
      return CSV;
  }

  return nullptr;
}

} // namespace StackAnalysis
