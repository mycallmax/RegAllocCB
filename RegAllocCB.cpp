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
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"

#include "llvm/CodeGen/LiveStackAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SparseSet.h"

#include <cstdlib>
#include <queue>
#include <map>
#include <list>
#include <stack>
#include <math.h>

using namespace llvm;

static RegisterRegAlloc basicRegAlloc("chaitin_briggs", "Chaitin-Briggs register allocator",
                                      createChaitinBriggsRegisterAllocator);

namespace {
  struct CompSpillWeight {
    bool operator()(LiveInterval *A, LiveInterval *B) const {
      return A->weight < B->weight;
    }
  };

  // Everything we know about a live virtual register.
  class LiveReg {
  public:
    static unsigned PhyRegsNum;
    unsigned Reg;

    explicit LiveReg(unsigned v)
      : Reg(v) {}

    unsigned getSparseSetIndex() const {
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        return PhyRegsNum + TargetRegisterInfo::virtReg2Index(Reg);
      } else {
        return Reg;
      }
    }

    bool operator<(const LiveReg& b) const {
      return Reg < b.Reg;
    }
  };
  typedef SparseSet<LiveReg> LiveRegMap;
  unsigned LiveReg::PhyRegsNum = 1000;
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

  //SlotIndexes *Indexes;
  //
  const TargetInstrInfo *TII;

  const MachineLoopInfo * loopInfo;
 
 // static  unsigned  Allocate_Phys_Start_index; //= 10; 
  static  unsigned  Reserved_Phys_Splitting_1; //= 8;
  static  unsigned  Reserved_Phys_Splitting_2; //= 9;
  static  unsigned  K_color;
  // state
  std::auto_ptr<Spiller> SpillerInstance;
  std::priority_queue<LiveInterval*, std::vector<LiveInterval*>,
                      CompSpillWeight> Queue;

  // Scratch space.  Allocated here to avoid repeated malloc calls in
  // selectOrSplit().
  BitVector UsableRegs;

  // Interference Graph (adjacency list)
  typedef std::vector< std::list< unsigned > > InterferenceGraph;
  InterferenceGraph IG;

  std::map<unsigned, double> SpillCost;

  //std::map<unsigned, unsigned> defnum;
  //std::map<unsigned, unsigned> usenum;
  std::map<unsigned, std::vector<unsigned> > def_nestdepth;
  std::map<unsigned, std::vector<unsigned> > use_nestdepth;

  std::stack<unsigned> Color_Node_Stack;

  //0-(K-1) colors and K means spilling.
  std::map<unsigned, unsigned> Color_Result;

  std::map<unsigned, unsigned> Color_2_PhysReg;
  // the call instruction maps to a vector of virtual live registers
  std::map< MachineInstr*, std::vector< LiveReg > > LiveRegVecMap;

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
  void addInterference(LiveReg DefReg, LiveRegMap &LiveRegs);

  //k-color the interference graph by graph prunning
  void kcolorbygraphprunning(unsigned K_color);
  void spillcostcalculus();
  void assignvir2phy(MachineFunction &mf);
  void manageregisterXcall(MachineFunction &mf);
  //int pick_spill_candidate();
  static char ID;


};

char RAChaitinBriggs::ID = 0;
//unsigned  RAChaitinBriggs::Allocate_Phys_Start_index= 10; 
unsigned  RAChaitinBriggs::Reserved_Phys_Splitting_1= 8;
unsigned  RAChaitinBriggs::Reserved_Phys_Splitting_2= 9;
unsigned  RAChaitinBriggs::K_color = 14;

} // end anonymous namespace

//INITIALIZE_PASS_DEPENDENCY(SlotIndexes)

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
  //AU.addRequired<SlotIndexes>();
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
  TII = mf.getTarget().getInstrInfo();
  loopInfo = &getAnalysis<MachineLoopInfo>();

  RegAllocBase::init(getAnalysis<VirtRegMap>(),
                     getAnalysis<LiveIntervals>(),
                     getAnalysis<LiveRegMatrix>());
  SpillerInstance.reset(createSpiller(*this, *MF, *VRM));


  //Indexes = &getAnalysis<SlotIndexes>();

  // Intialization
  LiveRegVecMap.clear();

  // Build the interference graph
  buildInterferenceGraph(mf);
  
  // Estimate the spill cost
  spillcostcalculus();
  // k-coloring
  //K_color = 14;
  //K_color = 14;
  kcolorbygraphprunning(K_color);
  
  //edit VRM
  //
  assignvir2phy(mf);
  manageregisterXcall(mf);
  Color_Result.clear();
  IG.clear();

  SpillCost.clear();


  Color_2_PhysReg.clear();
  LiveRegVecMap.clear();
  //defnum.clear();
  //usenum.clear();
  def_nestdepth.clear();
  use_nestdepth.clear();
  //

 
  // Diagnostic output before rewriting
  DEBUG(dbgs() << "Post alloc VirtRegMap:\n" << *VRM << "\n");

  releaseMemory();
  return true;
}

void RAChaitinBriggs::spillcostcalculus()
{
  //calculate the spillcost
  //std::map<int, int> SpillCost;
  for (InterferenceGraph::iterator IGi = IG.begin(), IGe = IG.end();  IGi != IGe; IGi++)
  {
    //TBD: using formulas to calculate the spill cost
    unsigned virtReg = IGi-IG.begin();
    //compute the spill cost
    //unsigned defs = defnum[virtReg];
    //unsigned uses = usenum[virtReg];
    std::vector<unsigned> defnestdepthVec = def_nestdepth[virtReg];
    std::vector<unsigned> usenestdepthVec = use_nestdepth[virtReg];
    std::vector<unsigned>::iterator it;
    double defcost = 0;
    double usecost = 0;
    for (it=defnestdepthVec.begin(); it<defnestdepthVec.end(); it++) 
    {
       DEBUG(dbgs() <<"Register "<<virtReg<<" def depth is "<<*it<<"\n");
       defcost = defcost + pow(10.0, *it);
    }
    //DEBUG(dbgs() <<"Register "<<virtReg<<" def cost is "<<defcost<<"\n");
    
    for (it=usenestdepthVec.begin(); it<usenestdepthVec.end(); it++) 
    {
       DEBUG(dbgs() <<"Register "<<virtReg<<" use depth is "<<*it<<"\n");
       usecost = usecost + pow(10.0, *it);
    }
    //DEBUG(dbgs() <<"Register "<<virtReg<<" use cost is "<<usecost<<"\n");
    double totalcost = defcost + usecost; 
    DEBUG(dbgs() << "Register "<<virtReg<<".  The total cost is " << totalcost << "\n");
    
    SpillCost.insert(std::pair<unsigned, unsigned>(virtReg ,totalcost)); 
  }  
}


void RAChaitinBriggs::kcolorbygraphprunning(unsigned K_color)
{
   //Input: 
   //typedef std::vector< std::list< unsigned > > InterferenceGraph;
   //InterferenceGraph IG;
   //Output:
   //std::map<unsigned, int> Color_Result;

   //std::map<int, unsigned> Color_2_PhysReg;
   Color_2_PhysReg[0] = 16;  //S0  
   Color_2_PhysReg[1] = 17;  //S1
   Color_2_PhysReg[2] = 18;  //S2
   Color_2_PhysReg[3] = 19;  //S3
   Color_2_PhysReg[4] = 20;  //S4
   Color_2_PhysReg[5] = 21;  //S5
   Color_2_PhysReg[6] = 22;  //S6
   Color_2_PhysReg[7] = 23;  //S7
   Color_2_PhysReg[8] = 10;  //T2
   Color_2_PhysReg[9] = 11;  //T3
   Color_2_PhysReg[10] =12;  //T4 
   Color_2_PhysReg[11] =13;  //T5
   Color_2_PhysReg[12] =14;  //T6
   Color_2_PhysReg[13] =15;  //T7

   //make another copy of IG for future color step 
   DEBUG(dbgs() << "Start to color all registers \n");
   std::map<unsigned, std::list<unsigned> > IG_copy;
      
    unsigned i=0;
    for (InterferenceGraph::iterator IGi = IG.begin(), IGe = IG.end();  IGi != IGe; IGi++,i++) 
    {
      if((*IGi).size()==0)
       Color_Node_Stack.push(i);
        
      for (std::list< unsigned >::iterator IGLi = IGi->begin(), IGLe = IGi->end();IGLi != IGLe; IGLi++) 
         IG_copy[i].push_back(*IGLi);
    }
   
   //recording the removed nodes from IG
   //std::set<unsigned> removed_node;
   
   while(IG_copy.size()!=0)
   {
     int cur_node = -1;
     std::map<unsigned, std::list<unsigned> >::iterator itmap;
     for ( itmap=IG_copy.begin() ; itmap != IG_copy.end(); itmap++ )
     {
       if((*itmap).second.size() < K_color)
       {
         cur_node = (*itmap).first;
         break;
       }    
     } 
     //pick a spill candidate
     if(cur_node ==-1)
     {
       //cur_node =  pick_spill_candidate();
       //compute the current degree for every remaining node
       double spill_cost = -1;
    
       for ( itmap=IG_copy.begin() ; itmap != IG_copy.end(); itmap++ )
       {
          //current degree
          double cost = SpillCost[(*itmap).first];  
          int degree = (*itmap).second.size();
          double tmp_spill_cost =  (double)cost/(double)degree;
          if(spill_cost == -1)
          {
             spill_cost = tmp_spill_cost;
             cur_node = (*itmap).first;         
          }
          else if(tmp_spill_cost<spill_cost)
          {
             spill_cost = tmp_spill_cost;
             cur_node = (*itmap).first;         
          }
       }
     }

     Color_Node_Stack.push(cur_node);
     //remove all adjacent edges from IG
     std::list<unsigned>::iterator it;
     std::list<unsigned> cur_list = IG_copy[cur_node]; 
     for(it = cur_list.begin(); it!=cur_list.end(); it++)
     //for(it = IG_copy[cur_node].begin(); it!=IG_copy[cur_node].end(); it++)
     {
       unsigned reachnode =(*it);
       IG_copy[reachnode].remove(cur_node);    
     } 
     //remove the Node
     IG_copy[cur_node].clear();
     IG_copy.erase(cur_node);
   }

   //std::stack<unsigned> Color_Node_Stack;
   //start to color the IG graph
   //std::map<unsigned, int> Color_Result;
   std::set<unsigned> Color_Set;
   while(!Color_Node_Stack.empty())
   {
     unsigned cur_color_node = Color_Node_Stack.top();
     //DEBUG(dbgs() << "cur_color node is  " << cur_color_node <<" reg is "<<PrintReg(TargetRegisterInfo::index2VirtReg(cur_color_node), TRI) << "\n");
     
     Color_Node_Stack.pop();
     //fill the full color set 
     //std::set<int>::iterator it; 
     for(unsigned i=0; i<K_color; i++)
       Color_Set.insert(i); 
     //delete available color by checking its adjacent color 
     InterferenceGraph::iterator IGi = IG.begin() + cur_color_node; 
     for (std::list< unsigned >::iterator IGLi = IGi->begin(), IGLe = IGi->end();IGLi != IGLe; IGLi++) 
     {
       if((Color_Result.find(*IGLi)!=Color_Result.end())&&(Color_Set.count(Color_Result[*IGLi])!=0))
         Color_Set.erase(Color_Result[*IGLi]);
     } 
     //Choose color from the color set
     if(Color_Set.empty())
     {
       //Spill 
       Color_Result.insert(std::pair<unsigned, unsigned>(cur_color_node, K_color));
       //Color_Result.insert(std::pair<unsigned, int>(cur_color_node, K_color));
       DEBUG(dbgs() << "<spill>register " << cur_color_node << "\n");
     }
     else
     {
       
       //DEBUG(dbgs() << "register " << cur_color_node << "\n");
       Color_Result.insert(std::pair<unsigned, unsigned>(cur_color_node, *(Color_Set.begin())));
       //DEBUG(dbgs() << "register " << PrintReg(TargetRegisterInfo::index2VirtReg(cur_color_node), TRI) << " is colored " << *(Color_Set.begin()) << "\n");
       DEBUG(dbgs() << "register " << cur_color_node << " is colored " << *(Color_Set.begin()) << "\n");
       //std::map<unsigned, int>::iterator it;
       //for(it=Color_Result.begin(); it!=Color_Result.end(); it++)
       //   DEBUG(dbgs() << "Virt register "<<(*it).first<<" Phys Register  " << (*it).second << "\n");
       //DEBUG(dbgs() << "\n");
       
     }
     Color_Set.clear();
     
   }
   
   //output color result 
   //std::map<unsigned, int>::iterator it;
   //for(it=Color_Result.begin(); it!=Color_Result.end(); it++)
   //    DEBUG(dbgs() << "Virt register "<<(*it).first<<" Phys Register  " << (*it).second << "\n");
      
   //assert(0 && "intentional stop");
}

void RAChaitinBriggs::assignvir2phy(MachineFunction &mf)
{
  //virtual register to phy register
  //std::map<unsigned, int> Color_Result;
  std::map<unsigned, unsigned>::iterator it;
  const TargetRegisterClass * gp_class = TRI->getRegClass(0);
  //const TargetInstrInfo *TII = mf.getTarget().getInstrInfo();
  //const TargetRegisterClass *trc = mri->getRegClass();
  for(it=Color_Result.begin(); it!=Color_Result.end(); it++)
  {
     //
     //VRM->assignVirt2Phys(VirtReg.reg, PhysReg);
     unsigned VirtReg = TargetRegisterInfo::index2VirtReg((*it).first);
     if((*it).second!=K_color)
     {
        //BitVector regvec = TRI->getAllocatableSet(mf, TRI->getRegClass(10));
        //DEBUG(dbgs() << "Phy class 0 num is  " << cur_class->getNumRegs() << " "<<cur_class->getName()<<"\n");
        //DEBUG(dbgs() << "Phy class 1 num is  " << TRI->getRegClass(1)->getNumRegs() << "\n");
        //DEBUG(dbgs() << "Is allocable "<<cur_class->isAllocatable()<<"\n"); 
        //DEBUG(dbgs() << "Phy class 0 #0 is  " << gp_class->getRegister(0) << "\n");
        //---DEBUG(dbgs() << "Phy register 1 is  " << TRI->getName(gp_class->getRegister(31)) << "\n"); //24-T8
        //unsigned PhysReg = gp_class->getRegister((unsigned)(*it).second+8);
        
        //unsigned PhysReg = gp_class->getRegister((unsigned)(*it).second+16);
        unsigned PhysReg = gp_class->getRegister(Color_2_PhysReg[(*it).second]);
        //unsigned PhysReg = gp_class->getRegister((unsigned)(*it).second+Allocate_Phys_Start_index);
        //DEBUG(dbgs() <<"Phys number is "<<(unsigned)(*it).second <<"  Virt register "<<PrintReg(VirtReg, TRI)<<" is colored to  " << PrintReg(PhysReg, TRI) << "\n"); //24-T8
 
        VRM->assignVirt2Phys(VirtReg, PhysReg);
    
        //assert(0 && "intentional stop");
     }
     else
     {
        //Spill
        //DEBUG(dbgs() << "-------------SPILL "  << "\n"); //24-T8
        unsigned ss = VRM->assignVirt2StackSlot(VirtReg);
        unsigned index_flag = 0; 
        for (MachineRegisterInfo::reg_iterator regItr = MRI->reg_begin(VirtReg); regItr != MRI->reg_end();)  
        {
           MachineInstr *mi = &*regItr;  
           do {
                 ++regItr;
           } while (regItr != MRI->reg_end() && (&*regItr == mi));
 
           SmallVector<unsigned, 2> indices;
           bool hasUse = false;
           bool hasDef = false;
           for (unsigned i = 0; i != mi->getNumOperands(); ++i) 
           {
              MachineOperand &op = mi->getOperand(i);
              if (!op.isReg() || op.getReg() != VirtReg)
                continue;
              hasUse |= mi->getOperand(i).isUse();
              hasDef |= mi->getOperand(i).isDef();
              indices.push_back(i);
              index_flag = i;
           }
           
           for (unsigned i = 0; i < indices.size(); ++i) 
           {
              unsigned mopIdx = indices[i];
              MachineOperand &mop = mi->getOperand(mopIdx);
              if(index_flag == 2)
                //mop.setReg(gp_class->getRegister(23));
                mop.setReg(gp_class->getRegister(Reserved_Phys_Splitting_2));
              else
                //mop.setReg(gp_class->getRegister(22));
                mop.setReg(gp_class->getRegister(Reserved_Phys_Splitting_1));
              if (mop.isUse() && !mi->isRegTiedToDefOperand(mopIdx)) 
              {
                mop.setIsKill(true);
              }
           }
           assert(hasUse || hasDef);
           MachineBasicBlock::iterator miItr(mi);
           const TargetRegisterClass *trc = MRI->getRegClass(VirtReg);
           if (hasUse) 
           {
              
              //if(index_flag == 2)
              //  DEBUG(dbgs() << "next register--LLY  "  << "\n"); //24-T8
                  
              if(index_flag == 2)
                //TII->loadRegFromStackSlot(*mi->getParent(), miItr, gp_class->getRegister(23), ss, trc,TRI);
                TII->loadRegFromStackSlot(*mi->getParent(), miItr, gp_class->getRegister(Reserved_Phys_Splitting_2), ss, trc,TRI);
              else
                //TII->loadRegFromStackSlot(*mi->getParent(), miItr, gp_class->getRegister(22), ss, trc,TRI);
                TII->loadRegFromStackSlot(*mi->getParent(), miItr, gp_class->getRegister(Reserved_Phys_Splitting_1), ss, trc,TRI);
              //MachineInstr *loadInstr(prior(miItr));
              //SlotIndex loadIndex =
              //  lis->InsertMachineInstrInMaps(loadInstr).getRegSlot();
              //SlotIndex endIndex = loadIndex.getNextIndex();
              //VNInfo *loadVNI =
              //  newLI->getNextValue(loadIndex, lis->getVNInfoAllocator());
              //newLI->addRange(LiveRange(loadIndex, endIndex, loadVNI));
           }
           if (hasDef) 
           {
              //TII->storeRegToStackSlot(*mi->getParent(), llvm::next(miItr), gp_class->getRegister(22), true, ss, trc, TRI);
              TII->storeRegToStackSlot(*mi->getParent(), llvm::next(miItr), gp_class->getRegister(Reserved_Phys_Splitting_1), true, ss, trc, TRI);
           }

        }
        DEBUG(dbgs() << "Virt register  " <<(*it).first<<" is assigned stack slot "<<ss<< "\n");
     }
  }
  //Color_Result.clear();
  //assert(0 && "intentional stop");
}

  
void RAChaitinBriggs::manageregisterXcall(MachineFunction &mf)
{
  //This function will insert instruction to save/restore registers around function call 
  DEBUG(dbgs() << " Insert instruction to save/restore registers cross function call "<< "\n");
  const TargetRegisterClass * RC = TRI->getRegClass(0);
  //get a JALR instruction
  //MachineInstr *mi = ;  
  int SS = mf.getFrameInfo()->CreateSpillStackObject(RC->getSize(),RC->getAlignment());
  DEBUG(dbgs() << "allocated stack slot is  " <<SS<< "\n");
  //std::map< MachineInstr*, std::vector< LiveReg > > LiveRegVecMap;
  //
  std::map<MachineInstr *, std::vector<LiveReg> >::iterator it;
  std::vector<LiveReg> LiveUnpreservedReg;
  for ( it=LiveRegVecMap.begin() ; it != LiveRegVecMap.end(); it++ )
  {
     //get the instruction
     MachineInstr *MI = (*it).first;
     DEBUG(dbgs()<<"Call instruction is " << *MI << "\n");

     //get the unpreserved registers in the liveset
     for (unsigned i=0; i<(*it).second.size(); i++) 
     {
       unsigned idxVirt = TargetRegisterInfo::virtReg2Index((*it).second[i].Reg);
       //check whether the register is non preserved
       DEBUG(dbgs()<<"The virtual live regiser is  " << idxVirt << " the register is "<< PrintReg((*it).second[i].Reg,TRI)  << "\n");
       //get the mapped phys register
       //std::map<unsigned, int> Color_Result;
       unsigned idxPhys = Color_Result[idxVirt];
       DEBUG(dbgs()<<"The mapped Phys regiser is  " << idxPhys  << "\n");
       //determine whether the Phys register is non preserved
       if((idxPhys!=K_color)&&(Color_2_PhysReg[idxPhys]<=15) && (Color_2_PhysReg[idxPhys]>=10))
       {
          MachineBasicBlock::iterator miItr(MI);
          DEBUG(dbgs()<<"Need to save and restore registers "  << "\n");
          int SS = mf.getFrameInfo()->CreateSpillStackObject(RC->getSize(),RC->getAlignment());
          TII->loadRegFromStackSlot(*MI->getParent(), llvm::next(miItr), RC->getRegister(Color_2_PhysReg[idxPhys]), SS, RC,TRI);
          TII->storeRegToStackSlot(*MI->getParent(), miItr, RC->getRegister(Color_2_PhysReg[idxPhys]), true, SS, RC, TRI);
          //TII->loadRegFromStackSlot(*MI->getParent(), llvm::next(miItr), RC->getRegister(idxVirt), SS, RC,TRI);
          //TII->storeRegToStackSlot(*MI->getParent(), miItr, RC->getRegister(idxVirt), false, SS, RC, TRI);
       }
     }
  }
 
  //std::map<SlotIndex, std::vector< LiveReg > > LiveRegVecMap;
  //      std::vector< LiveReg > regs(LiveRegs.begin(), LiveRegs.end());
  //      SlotIndex call_instr_slot = LIS->getInstructionIndex(&*MIi);
  //      LiveRegVecMap[call_instr_slot] = regs;
  //
  //======std::map<SlotIndex, std::vector<LiveReg> >::iterator it;
  //======std::vector<LiveReg> LiveUnpreservedReg;
  //======for ( it=LiveRegVecMap.begin() ; it != LiveRegVecMap.end(); it++ )
  //======{
  //======   //get the instruction
  //======   MachineInstr *MI = Indexes->getInstructionFromIndex((*it).first);
  //======   DEBUG(dbgs()<<"Call instruction is " << *MI << "\n");

  //======   //get the unpreserved registers in the liveset
  //======   for (unsigned i=0; i<(*it).second.size(); i++) 
  //======   {
  //======     unsigned idxVirt = TargetRegisterInfo::virtReg2Index((*it).second[i].Reg);
  //======     //check whether the register is non preserved
  //======     DEBUG(dbgs()<<"The virtual live regiser is  " << idxVirt << " the register is "<< (*it).second[i].Reg  << "\n");
  //======     //get the mapped phys register
  //======     //std::map<unsigned, int> Color_Result;
  //======     int idxPhys = Color_Result[idxVirt];
  //======     DEBUG(dbgs()<<"The mapped Phys regiser is  " << idxPhys  << "\n");
  //======     //determine whether the Phys register is non preserved
  //======     if(idxPhys<8)
  //======     {
  //======        MachineBasicBlock::iterator miItr(MI);
  //======        DEBUG(dbgs()<<"Need to save and restore registers "  << "\n");
  //======        int SS = mf.getFrameInfo()->CreateSpillStackObject(RC->getSize(),RC->getAlignment());
  //======        TII->loadRegFromStackSlot(*MI->getParent(), llvm::next(miItr), RC->getRegister(idxPhys+8), SS, RC,TRI);
  //======        TII->storeRegToStackSlot(*MI->getParent(), miItr, RC->getRegister(idxPhys+8), true, SS, RC, TRI);
  //======     }
  //======   }
  //======}
}


void RAChaitinBriggs::addInterference(LiveReg DefReg, LiveRegMap &LiveRegs) {
  assert(TargetRegisterInfo::isVirtualRegister(DefReg.Reg) &&
         "The register to be added into interference graph is not virtual");
  for (LiveRegMap::iterator LRi = LiveRegs.begin(), LRe = LiveRegs.end();
       LRi != LRe; LRi++) {
    unsigned idx1 = TargetRegisterInfo::virtReg2Index(DefReg.Reg);
    unsigned idx2 = TargetRegisterInfo::virtReg2Index(LRi->Reg);
    if (idx1 == idx2) continue;
    DEBUG(dbgs() << PrintReg(DefReg.Reg, TRI) << " interferes with " << PrintReg(LRi->Reg, TRI) << "\n");
    IG[idx1].push_back(idx2);
    IG[idx2].push_back(idx1);
  }
}

void RAChaitinBriggs::buildInterferenceGraph(MachineFunction &mf) {
  LiveReg::PhyRegsNum = TRI->getNumRegs();
  IG.resize(MRI->getNumVirtRegs());
  // Loop over all of the basic blocks

  //initialize the defnum and usenum
  //////for(unsigned i=0; i<MRI->getNumVirtRegs(); i++)
  //////{
  //////  def_nestdepth[i]=0;
  //////  use_nestdepth[i]=0;
  //////}
  for (MachineFunction::iterator MBBi = mf.begin(), MBBe = mf.end();
       MBBi != MBBe; ++MBBi) {
    DEBUG(dbgs() << "Building interference graph on " << *MBBi << "\n");
   

    MachineBasicBlock *MBB = &*MBBi; 
    unsigned loopDepth = loopInfo->getLoopDepth(MBB);
    // Calculate the Live Out Virtual Registers (TODO: include physical ones)
    LiveRegMap LiveRegs;
    LiveRegs.setUniverse(MRI->getNumVirtRegs() + LiveReg::PhyRegsNum);
    for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
      unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
      if (MRI->reg_nodbg_empty(Reg))
        continue;
      if(LIS->isLiveOutOfMBB(LIS->getInterval(Reg), MBBi)) {
        LiveRegs.insert(LiveReg(Reg));
      }
    }
    
    // Traverse backward to calculate the LiveNow for each instruction in
    // the basic block, and then build the interference graph
    DEBUG(dbgs() << "Start traversing the basic block backward\n");
    for (MachineBasicBlock::reverse_iterator MIi = MBBi->rbegin(), MIe = MBBi->rend();
         MIi != MIe; ++MIi) {

      // Print the current instruction
      DEBUG(dbgs() << *MIi << "\n");

      // When the instruction is a call, save the caller-saved registers
      if (MIi->isCall()) {
        std::vector< LiveReg > regs(LiveRegs.begin(), LiveRegs.end());
        LiveRegVecMap[&*MIi] = regs;
        DEBUG(dbgs() << "This is a call instruction" << "\n");
        DEBUG(dbgs() << "Live Register Candidate: ");
        for (std::vector< LiveReg >::iterator LRVi = regs.begin(), LRVe = regs.end(); LRVi != LRVe; ++LRVi) {
          DEBUG(dbgs() << PrintReg(LRVi->Reg, TRI) << " ");
        }
        DEBUG(dbgs() << "\n");
        continue;
      }

      // Print out the LiveNow registers
      DEBUG(dbgs() << " LiveNow Registers:\n");
      for (LiveRegMap::iterator I = LiveRegs.begin(), E = LiveRegs.end();
           I != E; I++) {
        DEBUG(dbgs() << "   " << PrintReg(I->Reg, TRI) << "\n");
      }
      DEBUG(dbgs() << "\n");

      // Look at all operands of the current instruction
      for (unsigned i = 0; i != MIi->getNumOperands(); ++i) {
        MachineOperand &op = MIi->getOperand(i);

        // Continue if it's not a register
        if (!op.isReg())
          continue;
        
        // If the register is defined,
        // it interferes with all registers within LiveRegs
        if (op.isDef()) {
          DEBUG(dbgs() << "[def] op" << i << ": " << op << "\n");
          unsigned def_reg = op.getReg();
          LiveReg DefReg(def_reg);
          if (TRI->isVirtualRegister(def_reg)) { // TODO: remove this check
            //add to the nestdepth of def; added by Lingyi
            unsigned idx = TargetRegisterInfo::virtReg2Index(def_reg);
            def_nestdepth[idx].push_back(loopDepth);
            //DEBUG(dbgs() << "SPILL COST: def reg is " << def_reg << "\n");
            this->addInterference(DefReg, LiveRegs);
            LiveRegs.erase(DefReg.getSparseSetIndex());
          }
        }

        // Update the LiveRegs for the next instruction
        if (op.isUse()) {
          DEBUG(dbgs() << "[use] op" << i << ": " << op << "\n");
          unsigned use_reg = op.getReg();
          if (TRI->isVirtualRegister(use_reg)) { // TODO: remove this check
          //add to the usenum map; added by Lingyi
            //DEBUG(dbgs() << "SPILL COST use reg is " << use_reg << "\n");
            unsigned idx = TargetRegisterInfo::virtReg2Index(use_reg);
            use_nestdepth[idx].push_back(loopDepth);
            LiveRegs.insert(LiveReg(use_reg));
          }
        }
      }
    }

    // Print out the interference graph after visiting all basic blocks
    for (InterferenceGraph::iterator IGi = IG.begin(), IGe = IG.end();
         IGi != IGe; IGi++) {
      DEBUG(dbgs() << PrintReg(TargetRegisterInfo::index2VirtReg(IGi - IG.begin()), TRI) << " interferes with ");
      for (std::list< unsigned >::iterator IGLi = IGi->begin(), IGLe = IGi->end();
           IGLi != IGLe; IGLi++) {
        DEBUG(dbgs() << " " << PrintReg(TargetRegisterInfo::index2VirtReg(*IGLi), TRI));
      }
      DEBUG(dbgs() << "\n");
    }
  }
  //assert(0 && "intentional stop");
  return;
}

FunctionPass* llvm::createChaitinBriggsRegisterAllocator()
{
  return new RAChaitinBriggs();
}
