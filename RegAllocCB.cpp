//===-- RegAllocBasic.cpp - Basic Register Allocator ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the RAChaitinBriggs function pass, which provides a minimal
// implementation of the basic register allocator.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "regalloc"
#include "AllocationOrder.h"
#include "RegAllocBase.h"
#include "LiveDebugVariables.h"
#include "Spiller.h"
#include "VirtRegMap.h"
#include "LiveRegMatrix.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/PassAnalysisSupport.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveStackAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <queue>
#include <set>
#include <list>

using namespace llvm;

static RegisterRegAlloc basicRegAlloc("chaitin_briggs", "Chaitin-Briggs register allocator",
                                      createChaitinBriggsRegisterAllocator);

namespace {
  struct CompSpillWeight {
    bool operator()(LiveInterval *A, LiveInterval *B) const {
      return A->weight < B->weight;
    }
  };

//  // Everything we know about a live virtual register.
//  class LiveReg {
//  public:
//    static unsigned PhyRegsNum;
//    unsigned Reg;
//
//    explicit LiveReg(unsigned v)
//      : Reg(v) {}
//
//    unsigned getSparseSetIndex() const {
//      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
//        return PhyRegsNum + TargetRegisterInfo::virtReg2Index(Reg);
//      } else {
//        return Reg;
//      }
//    }
//
//    bool operator<(const LiveReg& b) const {
//      return Reg < b.Reg;
//    }
//  };
//  typedef SparseSet<LiveReg> LiveRegMap;
//  unsigned LiveReg::PhyRegsNum = 1000;

  class LiveRegRange {
    unsigned reg;
    LiveRange *range;

  public:
    static const TargetRegisterInfo *TRI;

    LiveRegRange(unsigned reg, LiveRange *range) : reg(reg), range(range) {};

    unsigned getReg() const { return reg; }
    LiveRange* getLiveRange() const { return range; }

    bool operator==(const LiveRegRange &b) const {
      return reg == b.reg && *range == *(b.range);
    }

    bool operator<(const LiveRegRange &b) const {
      if (reg != b.reg)
        return reg < b.reg;
      else
        return *range < *(b.range);
    }

    void print(raw_ostream &os) const {
      if (TRI) {
        os << "  (" << PrintReg(reg, TRI) << ",\t" << *range << ") ";
      }
    }
  };

  raw_ostream& operator<<(raw_ostream& os, const LiveRegRange &LRR) {
    LRR.print(os);
    return os;
  }

  typedef std::set< LiveRegRange > LiveRegSet;
  const TargetRegisterInfo *LiveRegRange::TRI = NULL;
}

namespace {
/// RAChaitinBriggs provides a minimal implementation of the basic register allocation
/// algorithm. It prioritizes live virtual registers by spill weight and spills
/// whenever a register is unavailable. This is not practical in production but
/// provides a useful baseline both for measuring other allocators and comparing
/// the speed of the basic algorithm against other styles of allocators.
class RAChaitinBriggs : public MachineFunctionPass, public RegAllocBase
{
  // context
  MachineFunction *MF;

  // state
  std::auto_ptr<Spiller> SpillerInstance;
  std::priority_queue<LiveInterval*, std::vector<LiveInterval*>,
                      CompSpillWeight> Queue;

  // Scratch space.  Allocated here to avoid repeated malloc calls in
  // selectOrSplit().
  BitVector UsableRegs;

  // Interference Graph (adjacency list)
  typedef std::map< LiveRegRange, std::list< LiveRegRange > > InterferenceGraph;
  InterferenceGraph IG;

public:
  RAChaitinBriggs();

  /// Return the pass name.
  virtual const char* getPassName() const {
    return "Chaitin-Briggs Register Allocator";
  }

  /// RAChaitinBriggs analysis usage.
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  virtual void releaseMemory();

  virtual Spiller &spiller() { return *SpillerInstance; }

  virtual float getPriority(LiveInterval *LI) { return LI->weight; }

  virtual void enqueue(LiveInterval *LI) {
    Queue.push(LI);
  }

  virtual LiveInterval *dequeue() {
    if (Queue.empty())
      return 0;
    LiveInterval *LI = Queue.top();
    Queue.pop();
    return LI;
  }

  virtual unsigned selectOrSplit(LiveInterval &VirtReg,
                                 SmallVectorImpl<LiveInterval*> &SplitVRegs);

  /// Perform register allocation.
  virtual bool runOnMachineFunction(MachineFunction &mf);

  // Helper for spilling all live virtual registers currently unified under preg
  // that interfere with the most recently queried lvr.  Return true if spilling
  // was successful, and append any new spilled/split intervals to splitLVRs.
  bool spillInterferences(LiveInterval &VirtReg, unsigned PhysReg,
                          SmallVectorImpl<LiveInterval*> &SplitVRegs);

  // Build the interference graph
  void buildInterferenceGraph(MachineFunction &mf);
  // Add interference caused by the definition of DefReg
  void addInterference(LiveRegRange DefReg, LiveRegSet &LiveRegs);

  static char ID;
};

char RAChaitinBriggs::ID = 0;

} // end anonymous namespace

RAChaitinBriggs::RAChaitinBriggs(): MachineFunctionPass(ID) {
  initializeLiveDebugVariablesPass(*PassRegistry::getPassRegistry());
  initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
  initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
  initializeRegisterCoalescerPass(*PassRegistry::getPassRegistry());
  initializeMachineSchedulerPass(*PassRegistry::getPassRegistry());
  initializeCalculateSpillWeightsPass(*PassRegistry::getPassRegistry());
  initializeLiveStacksPass(*PassRegistry::getPassRegistry());
  initializeMachineDominatorTreePass(*PassRegistry::getPassRegistry());
  initializeMachineLoopInfoPass(*PassRegistry::getPassRegistry());
  initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
  initializeLiveRegMatrixPass(*PassRegistry::getPassRegistry());
}

void RAChaitinBriggs::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<AliasAnalysis>();
  AU.addPreserved<AliasAnalysis>();
  AU.addRequired<LiveIntervals>();
  AU.addPreserved<LiveIntervals>();
  AU.addPreserved<SlotIndexes>();
  AU.addRequired<LiveDebugVariables>();
  AU.addPreserved<LiveDebugVariables>();
  AU.addRequired<CalculateSpillWeights>();
  AU.addRequired<LiveStacks>();
  AU.addPreserved<LiveStacks>();
  AU.addRequiredID(MachineDominatorsID);
  AU.addPreservedID(MachineDominatorsID);
  AU.addRequired<MachineLoopInfo>();
  AU.addPreserved<MachineLoopInfo>();
  AU.addRequired<VirtRegMap>();
  AU.addPreserved<VirtRegMap>();
  AU.addRequired<LiveRegMatrix>();
  AU.addPreserved<LiveRegMatrix>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void RAChaitinBriggs::releaseMemory() {
  SpillerInstance.reset(0);
}


// Spill or split all live virtual registers currently unified under PhysReg
// that interfere with VirtReg. The newly spilled or split live intervals are
// returned by appending them to SplitVRegs.
bool RAChaitinBriggs::spillInterferences(LiveInterval &VirtReg, unsigned PhysReg,
                                 SmallVectorImpl<LiveInterval*> &SplitVRegs) {
  // Record each interference and determine if all are spillable before mutating
  // either the union or live intervals.
  SmallVector<LiveInterval*, 8> Intfs;

  // Collect interferences assigned to any alias of the physical register.
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, *Units);
    Q.collectInterferingVRegs();
    if (Q.seenUnspillableVReg())
      return false;
    for (unsigned i = Q.interferingVRegs().size(); i; --i) {
      LiveInterval *Intf = Q.interferingVRegs()[i - 1];
      if (!Intf->isSpillable() || Intf->weight > VirtReg.weight)
        return false;
      Intfs.push_back(Intf);
    }
  }
  DEBUG(dbgs() << "spilling " << TRI->getName(PhysReg) <<
        " interferences with " << VirtReg << "\n");
  assert(!Intfs.empty() && "expected interference");

  // Spill each interfering vreg allocated to PhysReg or an alias.
  for (unsigned i = 0, e = Intfs.size(); i != e; ++i) {
    LiveInterval &Spill = *Intfs[i];

    // Skip duplicates.
    if (!VRM->hasPhys(Spill.reg))
      continue;

    // Deallocate the interfering vreg by removing it from the union.
    // A LiveInterval instance may not be in a union during modification!
    Matrix->unassign(Spill);

    // Spill the extracted interval.
    LiveRangeEdit LRE(&Spill, SplitVRegs, *MF, *LIS, VRM);
    spiller().spill(LRE);
  }
  return true;
}

// Driver for the register assignment and splitting heuristics.
// Manages iteration over the LiveIntervalUnions.
//
// This is a minimal implementation of register assignment and splitting that
// spills whenever we run out of registers.
//
// selectOrSplit can only be called once per live virtual register. We then do a
// single interference test for each register the correct class until we find an
// available register. So, the number of interference tests in the worst case is
// |vregs| * |machineregs|. And since the number of interference tests is
// minimal, there is no value in caching them outside the scope of
// selectOrSplit().
unsigned RAChaitinBriggs::selectOrSplit(LiveInterval &VirtReg,
                                SmallVectorImpl<LiveInterval*> &SplitVRegs) {
//  // Populate a list of physical register spill candidates.
//  SmallVector<unsigned, 8> PhysRegSpillCands;
//
//  // Check for an available register in this class.
//  AllocationOrder Order(VirtReg.reg, *VRM, RegClassInfo);
//  while (unsigned PhysReg = Order.next()) {
//    // Check for interference in PhysReg
//    switch (Matrix->checkInterference(VirtReg, PhysReg)) {
//    case LiveRegMatrix::IK_Free:
//      // PhysReg is available, allocate it.
//      return PhysReg;
//
//    case LiveRegMatrix::IK_VirtReg:
//      // Only virtual registers in the way, we may be able to spill them.
//      PhysRegSpillCands.push_back(PhysReg);
//      continue;
//
//    default:
//      // RegMask or RegUnit interference.
//      continue;
//    }
//  }
//
//  // Try to spill another interfering reg with less spill weight.
//  for (SmallVectorImpl<unsigned>::iterator PhysRegI = PhysRegSpillCands.begin(),
//       PhysRegE = PhysRegSpillCands.end(); PhysRegI != PhysRegE; ++PhysRegI) {
//    if (!spillInterferences(VirtReg, *PhysRegI, SplitVRegs))
//      continue;
//
//    assert(!Matrix->checkInterference(VirtReg, *PhysRegI) &&
//           "Interference after spill.");
//    // Tell the caller to allocate to this newly freed physical register.
//    return *PhysRegI;
//  }

  // No other spill candidates were found, so spill the current VirtReg.
  DEBUG(dbgs() << "spilling: " << VirtReg << '\n');
  if (!VirtReg.isSpillable()) {
    // Check for an available register in this class.
    AllocationOrder Order(VirtReg.reg, *VRM, RegClassInfo);
    while (unsigned PhysReg = Order.next()) {
      // Check for interference in PhysReg
      switch (Matrix->checkInterference(VirtReg, PhysReg)) {
      case LiveRegMatrix::IK_Free:
        // PhysReg is available, allocate it.
        return PhysReg;

      case LiveRegMatrix::IK_VirtReg:
      default:
        // RegMask or RegUnit interference.
        continue;
      }
    }
    return ~0u;
  }

  LiveRangeEdit LRE(&VirtReg, SplitVRegs, *MF, *LIS, VRM);
  spiller().spill(LRE);

  // The live virtual register requesting allocation was spilled, so tell
  // the caller not to allocate anything during this round.
  return 0;
}

bool RAChaitinBriggs::runOnMachineFunction(MachineFunction &mf) {
  DEBUG(dbgs() << "********** CHAITIN-BRIGGS REGISTER ALLOCATION **********\n"
               << "********** Function: "
               << mf.getName() << '\n');

  MF = &mf;
  RegAllocBase::init(getAnalysis<VirtRegMap>(),
                     getAnalysis<LiveIntervals>(),
                     getAnalysis<LiveRegMatrix>());
  SpillerInstance.reset(createSpiller(*this, *MF, *VRM));

  // Build the interference graph
  buildInterferenceGraph(mf);
  
  // Estimate the spill cost
  
  // k-coloring

  // Diagnostic output before rewriting
  DEBUG(dbgs() << "Post alloc VirtRegMap:\n" << *VRM << "\n");

  releaseMemory();
  return true;
}

void RAChaitinBriggs::addInterference(LiveRegRange DefReg, LiveRegSet &LiveRegs) {
  assert(TargetRegisterInfo::isVirtualRegister(DefReg.getReg()) &&
         "The register to be added into interference graph is not virtual");
  for (LiveRegSet::iterator LRi = LiveRegs.begin(), LRe = LiveRegs.end();
       LRi != LRe; LRi++) {
      if (DefReg == *LRi) {
        continue;
      }
    DEBUG(dbgs() << DefReg << " interferes with " << *LRi << "\n");
    IG[DefReg].push_back(*LRi);
    IG[*LRi].push_back(DefReg);
  }
}

void RAChaitinBriggs::buildInterferenceGraph(MachineFunction &mf) {

  // Initialization
  LiveRegRange::TRI = TRI;

  for (TargetRegisterInfo::regclass_iterator RCi = TRI->regclass_begin(), RCe = TRI->regclass_end(); RCi != RCe; ++RCi) {
    DEBUG(dbgs() << (*RCi)->getNumRegs() << "\n");
  }

  assert(0 && "Intentional stop");

  // Loop over all of the basic blocks
  for (MachineFunction::iterator MBBi = mf.begin(), MBBe = mf.end();
       MBBi != MBBe; ++MBBi) {
    DEBUG(dbgs() << "Building interference graph on " << *MBBi << "\n");
    
    // Calculate the Live Out Virtual Register Set (TODO: include physical ones)
    LiveRegSet LiveRegs;
    SlotIndex LiveOutIdx = LIS->getMBBEndIdx(MBBi).getPrevSlot();
    for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
      unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
      if (MRI->reg_nodbg_empty(Reg)) {
        DEBUG(dbgs() << "Dropping unused " << PrintReg(Reg, TRI) << '\n');
        LIS->removeInterval(Reg);
        continue;
      }

      // See if Reg is in the LivOut Set of the MBBi
      LiveInterval &LI = LIS->getInterval(Reg);
//      DEBUG(dbgs() << "Live Interval: " << LI << "\n");
//      for (LiveInterval::iterator LRi= LI.begin(), LRe = LI.end(); LRi != LRe; ++LRi) {
//        DEBUG(dbgs() << "  Live Range: " << *LRi << "\n");
//      }
      if (LI.liveAt(LiveOutIdx)) {
        LiveInterval::iterator LR = LI.find(LiveOutIdx);
        assert(LIS->isLiveOutOfMBB(LI, MBBi) == true);
        DEBUG(dbgs() << "Reg: " << PrintReg(Reg, TRI) << " Range: " << *LR << "\n");
        LiveRegs.insert(LiveRegRange(Reg, &*LR));
      }
    }
    
    // Traverse backward to calculate the LiveNow for each instruction in
    // the basic block, and then build the interference graph
    DEBUG(dbgs() << "Start traversing the basic block backward\n");
    for (MachineBasicBlock::reverse_iterator MIi = MBBi->rbegin(), MIe = MBBi->rend();
         MIi != MIe; ++MIi) {

      // Print the current instruction
      DEBUG(dbgs() << *MIi << "\n");

      // Print out the LiveNow registers
      DEBUG(dbgs() << " LiveNow Registers:\n");
      for (LiveRegSet::iterator I = LiveRegs.begin(), E = LiveRegs.end();
           I != E; I++) {
        DEBUG(dbgs() << *I << "\n");
      }
      DEBUG(dbgs() << "\n");

      // Look at all operands of the current instruction
      SlotIndex InstrIdx = LIS->getInstructionIndex(&*MIi);
      for (unsigned i = 0; i != MIi->getNumOperands(); ++i) {
        MachineOperand &op = MIi->getOperand(i);

        // Continue if it's not a register
        if (!op.isReg() || !TRI->isVirtualRegister(op.getReg()))
          continue;

        unsigned reg = op.getReg();
        LiveInterval &LI = LIS->getInterval(reg);
        LiveInterval::iterator LR = LI.find(InstrIdx);
        assert(LR != LI.end() && "No live range for the register");
        
        // If the register is defined,
        // it interferes with all registers within LiveRegs
        if (op.isDef()) {
          DEBUG(dbgs() << "[def] op" << i << ": " << op << "\n");
          LiveRegRange def_reg(reg, LR);
          this->addInterference(def_reg, LiveRegs);
          LiveRegs.erase(def_reg);
        }

        // Update the LiveRegs for the next instruction
        if (op.isUse()) {
          DEBUG(dbgs() << "[use] op" << i << ": " << op << "\n");
          LiveRegRange use_reg(reg, LR);
          LiveRegs.insert(use_reg);
        }
      }
    }

    // Print out the interference graph after visiting all basic blocks
    DEBUG(dbgs() << "Interference Graph Result:\n");
    int counter = 0;
    for (InterferenceGraph::iterator IGi = IG.begin(), IGe = IG.end();
         IGi != IGe; IGi++) {
      DEBUG(dbgs() << IGi->first << " interferes with\n");
      for (std::list< LiveRegRange >::iterator IGLi = (IGi->second).begin(), IGLe = (IGi->second).end();
           IGLi != IGLe; IGLi++) {
        counter++;
        DEBUG(dbgs() << *IGLi);
        if (counter % 4 != 0)
          DEBUG(dbgs() << "\t");
        else
          DEBUG(dbgs() << "\n");
      }
      DEBUG(dbgs() << "\n");
    }
  }
  assert(0 && "intentional stop");
  return;
}

FunctionPass* llvm::createChaitinBriggsRegisterAllocator()
{
  return new RAChaitinBriggs();
}
