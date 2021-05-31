//We're writing a Pass
#include "llvm/Pass.h"
//Operating on a function
#include "llvm/IR/Function.h"
//That will be doing some printing
#include "llvm/Support/raw_ostream.h"


using namespace llvm;
/* required functions live in the llvm namespace from the 
 * include files
 */

/* Anonymous namespace equivalent to "static" word in C.
 * Declaration inside are visible only to the current file.
 */
namespace {
	//count how many functions are read
	static int ins_count = 0;
	//count how many basic blocks
	static int bb_count = 0;
	/* subclass of FunctionPass
	 * operates on a single function at a time
	 */
	/* TODO: 1. implement the printing of the number of basic blocks of such function
	 * 	 2. implement the printing of the number of instructions for such function
	 */
	struct Hello : public FunctionPass {
		/* Declaration of pass identifier used by llvm
		 * to avoid using expensive C++ runtime
		 * information
		 */
		static char ID;
		int ins_count = 0;
		int bb_count = 0;
		Hello() : FunctionPass(ID) {}
		bool runOnFunction(Function &F) override{
			/* Here's where we should do our things
			 * on the function. In this case, we're
			 * only printing out the function name
			 */
			errs() << "Hello: ";
			errs().write_escaped(F.getName()) << '\n';
			/*counting the number of Basic Blocks and
			 * the number of instructions for each
			 * Basic Block
			 */
			for(BasicBlock &BB : F){
				bb_count++;
				errs() << "Basic Block n" <<
					bb_count << ": ";
				for(Instruction &I : BB)
					ins_count++;
				errs() << 
				"the number of instructions is"
				<< ins_count << "instructions\n";
				ins_count = 0;
			}
			errs() << "The number of basic blocks is "
			       << bb_count << '\n';
			return false;
		}
	};
}

/* LLVM uses ID's address to identify a pass, so initialization
 * value is not important
 */
char Hello::ID = 0;

/* The first argument represents a command line "hello" to 
 * represent our class Hello, the second argument is the name of
 * our pass, the third argument indicates whether the pass is
 * walking through the CFG without modifying it, the fourth
 * argument indicates whether this pass is an analysis pass.
 * This function registers/defines our pass.
 */
static RegisterPass<Hello> X("hello", "Hello World Pass",
			      false /* Only looks at CFG */,
			      false /* Analysis Pass */);

/* If there exists a pipeline of passes we might choose to
 * execute the specified one before any optimization through
 * PassManagerBuilder::EP_EarlyAsPossible, or after Link Time
 * Optimizations through PassManagerBuilder::EP_FullLinkTime-
 * OptimizationLast
 */
/*
static llvm::RegisterStandardPasses Y(
		llvm::PassManagerBuilder::EP_EarlyAsPossible,
		[](const llvm::PassManagerBuilder &Builder,
		   llvm::legacy::PassManagerBase &PM) {
		   PM.add(new Hello());});
*/
static RegisterStandardPasses Y(
    PassManagerBuilder::EP_EarlyAsPossible,
    [](const PassManagerBuilder &Builder,
       legacy::PassManagerBase &PM) { PM.add(new Hello()); });
