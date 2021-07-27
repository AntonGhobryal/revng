#include "liveness.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"

#include <map>
#include <set>
#include <list>
#include <algorithm>
#include <iterator>


/* required functions live in the llvm namespace from the include
 * files
 */
using namespace llvm;
using namespace std;


/* Anonymous namespace equivalent to "static" word in C.
 * Declaration inside are visible only to the current file.
 */
namespace {
    /* Print out the variables that should be killed before each
     * instruction
     */

    /* Subclass of Function Pass
     * To minimize using expensive C++ runtime
     */
    struct liveness_analysis: public FunctionPass {
	//Pass Identification
	static char ID;
	
	liveness_analysis() : FunctionPass(ID) {}
	
	bool runOnFunction(Function &F) override {
	    /* Here's where we should do our things on the function.
	     * In this case, we're visiting all the basic blocks of
	     * each function in order to identify which variables die
	     * exactly before each instruction.
	     */
	    /* Build a map for Gen/Kill Set and corresponding Basic
	     * Block and iterator on each map
	     */
	    map<BasicBlock*, set<llvm::StringRef>> GEN;
	    map<BasicBlock*, set<llvm::StringRef>> KILL;
	    map<BasicBlock*, set<llvm::StringRef>>::iterator it;
	    map<BasicBlock*, set<llvm::StringRef>>::iterator kit;

	    for(BasicBlock &BB : F){
		// Gen/Kill set initialization
		set<StringRef> GenVar, KillVar;
		GEN.insert(std::pair<BasicBlock*,
			   std::set<llvm::StringRef>>(&BB, GenVar));
		KILL.insert(std::pair<BasicBlock*,
			   std::set<llvm::StringRef>>(&BB, KillVar));
		for(Instruction &I : BB) {
		    //--------------------------------------------
		    /* The opcode is required to identify the
		     * instruction type to populate then the gen/kill
		     * sets (and to do some optimization) which can
		     * be done through the following print-outs or by
		     * looking at llvm/IR/Instruction.def
		     * site: "https://github.com/llvm/llvm-project/
		     * blob/cbde0d9c7be1991751dc3eb5928294d2e00ef26a/
		     * llvm/include/llvm/IR/Instruction.def"
		     */
		    //errs() << "Instruction: " << I << '\n';
		    //errs() << "# of operands: "
		    //	     << I.getNumOperands() << '\n';
		    //errs() << I.getOpcode() << '\n';
		    //--------------------------------------------
		    for(unsigned i = 0; e = I.getNumOperands();
				    i!=e; i++) {
			StringRef varName=I.getOperand(i)->getName();
			/* ignore the most known instructions for
			 * optimization like return, branch (inside
			 * a function), Padding (IR Builder), and
			 * PHI nodes. Such Instructions' operands
			 * cannot be considered in this analysis
			 * since they aren't SSA
			 */
			if(I.getOpcode()==1 || I.getOpcode()==2
			|| I.getOpcode()==51|| I.getOpcode()==54)
			    continue;
			/* Check whether the variable to be killed
			 * is already in the set
			 */
			kit = KILL.find(&BB);
			/* If I find the variable I'm searching for,
			 * I should skip this iteration because the
			 * variable is already in the kill set
			 */
			if(kit->second.find(varName) !=
					kit->second.end())
			    continue;
			//Insert the variable in the GEN set
			it = GEN.find(&BB);
			GenVar = it->second;
			/* Insert the current operand in the gen set
			 * if this instruction is not the store
			 * instruction and at the same time it's not
			 * the second argument in which store
			 * instruction save a new value (LHS).
			 * NB: any other operand should be in the GEN
			 *     set (RHS).
			 */
			if(!(I.getOpcode==31 && i==1))
			    GenVar.insert(varName);
			/* Here we reset the GEN->second out of the
			 * conditional because we initialize the
			 * iterator out of the conditional and
			 * usually there're more gen than kill.
			 */
			GEN.erase(it);
			GEN.insert(std::pair<BasicBlock*
			   ,std::set<llvm::StringRef>>(&BB, GenVar));
			/* Insert the 2nd operand (LHS) of store to
			 * the kill set
			 */
			if(I.getOpcode()==31 && i==1) {
			    kit = KILL.find(&BB);
			    KillVar = kit->second;
			    KillVar.insert(varName);
			    KILL.erase(kit);
			    KILL.insert(std::pair<BasicBlock*,
			    std::set<llvm::StringRef>>(&BB,KillVar));
			}
		    }
		    /*If the instruction is an SSA, the LHS must
		     * be killed
		     */
		    if(!(I.getOpcode()==1 || I.getOpcode()==2 ||
			 I.getOpcode()==31|| I.getOpcode()==51||
			 I.getOpcode()==54)) {
			kit = VK.find(&BB);
			KillVar = kit->second;
			KillVar.insert(I.getName());
			KILL.erase(kit);
			KILL.insert(std::pair<BasicBlock*,
			std::set<llvm::StringRef>>(&BB,KillVar));
		    }
		} //for(Instruction &I: BB)
	    } //for(BasicBlock &BB: F)
	    /* print out the BB map for the Kill Set
	     * errs() << "Kill Set: " << '\n';
	     * for(auto m_it = KILL.begin(); m_it!=KILL.end();
	     * 						m_it++){
	     *     BasicBlock* tmpkey = m_it->first;
	     *     set<StringRef> tmpKillVar = m_it->second;
	     *     errs() << "BasicBlock: " << tmpkey->getName()
	     *     	  << " --> kill: {";
	     *     for(auto s_it = tmpKillVar.begin();
	     *     	s_it != tmpKillVar.end(); s_it++)
	     *         errs() << *s_it << " ";
	     *     errs() << "}" << '\n';
	     * }
	     *
	     * print out the BB map for the GEN set
	     * errs() << "Gen Set: " << '\n';
	     * for(auto m_it=GEN.begin();m_it!=GEN.end();m_it++){
	     *     BasicBlock* tmpkey = m_it->first;
	     *     set<StringRef> tmpGenVar = m_it->second;
	     *     errs() << "BasicBlock: " << tmpkey->getName()
	     *     	  << " --> gen: {";
	     *     for(auto s_it = tmpGenVar.begin();
	     *     	s_it != tmpGenVar.end(); s_it++)
	     *         errs() << *s_it << " ";
	     *     errs() << "}" << '\n';
	     * }
	     */
	    // Liveness computation of variables
	    // Inspired from MFP Solution - Worklist Algorithm
	    // Initialisation
	    map<BasicBlock*, set<llvm::StringRef>> LO;
	    map<BasicBlock*, set<llvm::StringRef>>::iterator l_it;
	    std::list<BasicBlock*> W; //worklist
	    for(BasicBlock &BB: F){
		set<StringRef> live_out;
		LO.insert(std::pair<BasicBlock*,
		std::set<llvm::StringRef>>(&BB, live_out));
		W.push_back(&BB);
	    }
	    while(!W.empty()){
		// A pointer over each element of the worklist
		BasicBlock* tmp = W.front();
		W.pop_front();
		auto it = LO.find(tmp);
		// Refering to the original live out set
		set<llvm::StringRef> live_out_orig = it->second;
		// Liveness computation
		const TerminatorInst *t_inst=tmp->getTerminator();
		// result live-out
		set<llvm::StringRef> live_out_res;
		for(int i=0, n_succ = t_inst->getNumSuccessors();
			i < n_succ; i++){
		    BasicBlock* succ = t_inst->getSuccessor(i);
		    set<llvm::StringRef> live_out =
			    LO.find(succ)->second;
		    set<llvm::StringRef> KillVar =
			    KILL.find(succ)->second;
		    set<llvm::StringRef> GenVar =
			    GEN.find(succ)->second;
		    set<llvm::StringRef> sub_set(live_out);
		    for(auto s_it = KillVar.begin();
			s_it != KillVar.end(); s_it++){
			sub_set.erase(*s_it);
		    }
		    std::set_union(GenVar.begin(), GenVar.end(),
				   sub_set.begin(), sub_set.end(),
				   std::inserter(live_out_res,
					   live_out_res.begin()));
		}
		// update LIVEOUT for this Basic Block
		LO.erase(it);
		LO.insert(std::pair<BasicBlock*,
		    std::set<llvm::StringRef>>(tmp,live_out_res));
		/* if the set changed, all predeccessors must be
		 * added to the worklist to be recontrolled and
		 * reprocessed
		 */
		if(live_out_res != live_out_orig){
		    for(auto pred_it = pred_begin(tmp),
			     pred_last = pred_end(tmp);
			     pred_it != pred_last; pred_it++){
			BasicBlock* pred = *pred_it;
			W.push_back(pred);
		    }
		}
	    } // while(!W.empty())
	    // TODO: print out the liveness as per specs
	    // Waiting for Pietro's answer.
	    retrun false;
	} // bool runOnFunction(Function &F)
    }; // struct liveness_analysis
} // namespace

char liveness_analysis::ID = 0;
static RegisterPass<liveness_analysis> X("liveness",
		"Liveness Set Pass", false, false);
/*The first "false" refers to the fact that this pass looks at the
 * CFG only and the second "false" refers to the fact that this is
 * an Analysis Pass.
 */
