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
	
	/* subclass of FunctionPass
	 * operates on a single function at a time
	 */
	struct Hello : public FunctionPass {
		/* Declaration of pass identifier used by llvm
		 * to avoid using expensive C++ runtime
		 * information
		 */
		static char ID;

		Hello() : FunctionPass(ID) {}
		
		bool runOnFunction(Function &F) override{
			/* Here's where we should do our things
			 * on the function. In this case, we're
			 * only printing out the function name
			 */
			int i = 1;
			errs() << "Hello: ";
			errs().write_escaped(F.getName()) << '\n';
			
			/*counting the number of Basic Blocks and
			 * the number of instructions for each
			 * Basic Block
			 */
			errs() << "There are " << F.size() << " Basic Blocks"
				<< '\n';
			for(BasicBlock &BB : F){
				errs() << "Basic Block n" << i << " has " 
					<< BB.size() << " instructions" << '\n';
				i++;
			}
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

