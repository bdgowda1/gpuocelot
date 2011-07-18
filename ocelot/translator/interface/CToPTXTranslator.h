/*! \file CToPTXTranslator.h
	\date Wednesday June 8, 2011
	\author Naila Farooqui <naila@cc.gatech.edu>
	\brief The header file for the CToPTXTranslator class
*/

#ifndef C_TO_PTX_TRANSLATOR_H_INCLUDED
#define C_TO_PTX_TRANSLATOR_H_INCLUDED

#include <string>
#include <map>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <set>

#include <ocelot/ir/interface/PTXOperand.h>
#include <ocelot/ir/interface/PTXInstruction.h>
#include <ocelot/ir/interface/PTXStatement.h>
#include <ocelot/ir/interface/PTXKernel.h>
#include <ocelot/ir/interface/ControlFlowGraph.h>
#include <ocelot/analysis/interface/DataflowGraph.h>

#include "cod.h"
#include "dill.h"
#include "dill_internal.h"

namespace translator {

    class PredicateInfo {
    
        public:
        
            std::string id;
            bool set;
            std::string branchLabel;
            
            enum Cond {
                TAKEN,
                NOT_TAKEN
            };
    
            Cond condition;
    
    };

    /*! \brief A class containing C-to-PTX translation data.
	*/
	class CToPTXData
	{
	    public:
	    
	        typedef std::vector<std::string> RegisterVector;
	        
	        ir::PTXKernel::PTXStatementVector statements;
		    ir::PTXKernel::PTXStatementVector globals;
		    RegisterVector registers;
	
	};


	/*! \brief A class translating C-on-Demand (COD) IR to PTX.
	*/
	class CToPTXTranslator
	{
			
		public:
		    
		    enum SpecialSymbols {
			    clockCounterSymbol,
			    globalThreadIdSymbol,
			    threadIdxXSymbol,
			    threadIdxYSymbol,
			    threadIdxZSymbol,
			    blockThreadIdSymbol,
			    blockDimXSymbol,
			    blockDimYSymbol,
			    blockDimZSymbol,
			    blockDimSymbol,
			    blockIdxXSymbol,
			    blockIdxYSymbol,
			    blockIdxZSymbol,
			    blockIdSymbol,
			    gridDimXSymbol,
			    gridDimYSymbol,
			    gridDimZSymbol,
			    gridDimSymbol,
			    gridIdSymbol,
			    smIdSymbol,
			    syncThreadsSymbol,
			    basicBlockIdSymbol,
                basicBlockInstCountSymbol,
                basicBlockExecInstCountSymbol,
                instructionIdSymbol,
                warpCountSymbol,
                warpIdSymbol,
                predicateEvalAllUniformSymbol
		    };
		    
		    SpecialSymbols symbols;
		
		    typedef std::map<std::string, std::string> RegisterMap;
		    typedef std::map<std::string, size_t> FunctionCallMap;
		    typedef std::vector<PredicateInfo> PredicateList;
		    typedef std::vector<std::string> RegisterVector;
		    
		    ir::PTXKernel *kernel;
		    ir::PTXKernel::PTXStatementVector statements;
		    ir::PTXKernel::PTXStatementVector globals;
		    RegisterMap registerMap;	
		    RegisterMap specialRegisterMap;
		    FunctionCallMap functionCalls;
		    PredicateList predicateList;
		    RegisterVector registers;
		    
		    unsigned int maxRegister;	
		    unsigned int maxPredicate;
		    
		    std::string baseReg;	  
		    
		    private:
		        void generateBlockId(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateSMId(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateWarpId(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateWarpCount(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateClockCounter(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateThreadIndexX(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateBlockDim(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateGlobalThreadId(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generatePredicateEvalAllUniform(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn);
		        void generateSyncThreads(ir::PTXInstruction inst, ir::PTXStatement stmt);
		        void generateStaticAttributes(ir::PTXInstruction inst, ir::PTXStatement stmt, ir::PTXOperand::DataType type, virtual_insn *insn, std::string callName);
		    
		    public: 
		    
		    /* default constructor */
		    CToPTXTranslator();
		    
		    std::string baseAddress() const;
		    void setPredicate(ir::PTXInstruction & instruction);
		    int translate(dill_stream c, void *info_ptr, void *i);
		    translator::CToPTXData generate(std::string resource);
		    
	};

}

#endif
