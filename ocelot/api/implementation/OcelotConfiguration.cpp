/*! \file OcelotConfiguration.cpp
	\author Andrew Kerr <arkerr@gatech.edu>
	\brief configuration class for GPU Ocelot
*/

// Ocelot includes
#include <ocelot/api/interface/OcelotConfiguration.h>

#include <ocelot/executive/interface/ReconvergenceMechanism.h>
#include <ocelot/ir/interface/Instruction.h>
#include <ocelot/translator/interface/Translator.h>
#include <ocelot/analysis/interface/KernelPartitioningPass.h>

// Hydrazine includes
#include <hydrazine/implementation/json.h>
#include <hydrazine/interface/Version.h>
#include <hydrazine/implementation/Exception.h>
#include <hydrazine/implementation/debug.h>
#include <hydrazine/interface/math.h>

// C stdlib includes
#include <cassert>

// Boost incldues
#include <boost/lexical_cast.hpp>

#ifdef REPORT_BASE
#undef REPORT_BASE
#endif

#define REPORT_BASE 0

////////////////////////////////////////////////////////////////////////////////

static api::OcelotConfiguration* ocelotConfiguration = 0;

const api::OcelotConfiguration & api::OcelotConfiguration::get() {
	if (!ocelotConfiguration) {
		const char *configName = "configure.ocelot";
		if (const char *cfgOverride = getenv("OCELOT_CONFIG")) {
			configName = cfgOverride;
		}
		ocelotConfiguration = new OcelotConfiguration(configName);
	}
	return *ocelotConfiguration;
}

const api::OcelotConfiguration & api::configuration() {
	return OcelotConfiguration::get();
}

void api::OcelotConfiguration::destroy() {
        delete ocelotConfiguration;
        ocelotConfiguration = 0;
}

////////////////////////////////////////////////////////////////////////////////

api::OcelotConfiguration::Checkpoint::Checkpoint():
	enabled(false), path("./"), prefix("check"), suffix(".binary"), verify(false)
{
	
}

static void initializeCheckpoint(api::OcelotConfiguration::Checkpoint &check,
	hydrazine::json::Visitor config) {

	check.enabled = config.parse<bool>("enabled", false);
	check.path = config.parse<std::string>("path", "trace/");
	check.prefix = config.parse<std::string>("prefix", "check");
	check.suffix = config.parse<std::string>("suffix", ".binary");
	check.verify = config.parse<bool>("verify", false);
}

////////////////////////////////////////////////////////////////////////////////

api::OcelotConfiguration::TraceGeneration::RaceDetector::RaceDetector():
        enabled(false),
        ignoreIrrelevantWrites(true)
{

}
api::OcelotConfiguration::TraceGeneration::Debugger::Debugger():
        enabled(false),
        kernelFilter(""),
        alwaysAttach(false)
{

}

api::OcelotConfiguration::TraceGeneration::MemoryChecker::MemoryChecker():
	enabled(true),
	checkInitialization(false)
{

}

api::OcelotConfiguration::TraceGeneration::KernelTimer::KernelTimer(): enabled(false),
	outputFile("applicationKernelRuntime.json") {

}


api::OcelotConfiguration::TraceGeneration::PerformanceCounters::PerformanceCounters():
	enabled(false), set(Cache_L1D) {

}

api::OcelotConfiguration::TraceGeneration::TraceGeneration()
{

}

static void initializeTrace(api::OcelotConfiguration::TraceGeneration &trace, 
	hydrazine::json::Visitor config) {
	
  hydrazine::json::Visitor memoryCheckerConfig = config["memoryChecker"];
  if (!memoryCheckerConfig.is_null()) {
          trace.memoryChecker.enabled = 
          	memoryCheckerConfig.parse<bool>("enabled", true);
          trace.memoryChecker.checkInitialization = 
          	memoryCheckerConfig.parse<bool>("checkInitialization", false);
  }
  
  hydrazine::json::Visitor raceConfig = config["raceDetector"];
  if (!raceConfig.is_null()) {
          trace.raceDetector.enabled = 
          	raceConfig.parse<bool>("enabled", false);
          trace.raceDetector.ignoreIrrelevantWrites = 
          	raceConfig.parse<bool>("ignoreIrrelevantWrites", true);
  }

  hydrazine::json::Visitor debugConfig = config["debugger"];
  if (!debugConfig.is_null()) {
    trace.debugger.enabled = debugConfig.parse<bool>("enabled", false);
    trace.debugger.kernelFilter = 
    	debugConfig.parse<std::string>("kernelFilter", "");
    trace.debugger.alwaysAttach = 
    	debugConfig.parse<bool>("alwaysAttach", false);
  }
  
  hydrazine::json::Visitor kernelTimer = config["kernelTimer"];
  if (!kernelTimer.is_null()) {
  	trace.kernelTimer.enabled = kernelTimer.parse<bool>("enabled", false);
  	trace.kernelTimer.outputFile = kernelTimer.parse<std::string>("outputFile", 
  		"applicationKernelRuntime.json");
  }
  
  hydrazine::json::Visitor perfCounter = config["performanceCounters"];
  if (!perfCounter.is_null()) {
  	trace.performanceCounters.enabled = perfCounter.parse<bool>("enabled", false);
  	std::string set = perfCounter.parse<std::string>("set", "unknown");
  	if (set == "L1D") {
  		trace.performanceCounters.set = 
  			api::OcelotConfiguration::TraceGeneration::PerformanceCounters::Cache_L1D;
		}
		else if (set == "L1I") {
  		trace.performanceCounters.set = 
  			api::OcelotConfiguration::TraceGeneration::PerformanceCounters::Cache_L1I;
		}
		else if (set == "L2") {
  		trace.performanceCounters.set = 
  			api::OcelotConfiguration::TraceGeneration::PerformanceCounters::Cache_L2;
		}
		else if (set == "L3") {
  		trace.performanceCounters.set = 
  			api::OcelotConfiguration::TraceGeneration::PerformanceCounters::Cache_L3;
		}
		else if (set == "IPC") {
  		trace.performanceCounters.set = 
  			api::OcelotConfiguration::TraceGeneration::PerformanceCounters::IPC;
		}
		else if (set == "branchMispredictions") {
  		trace.performanceCounters.set = 
  			api::OcelotConfiguration::TraceGeneration::PerformanceCounters::BranchMispredictions;
}
		else {
			trace.performanceCounters.enabled = false;
		}
  	report("configure.ocelot DOES contain performanceCounters");
  	report("  enabled = " << trace.performanceCounters.enabled);
  }
  else {
  	report("configure.ocelot DOES NOT contain performanceCounters");
  }
}

api::OcelotConfiguration::CudaRuntimeImplementation::CudaRuntimeImplementation():
	implementation("CudaRuntime"),
	runtimeApiTrace("trace/CudaAPI.trace")
{

}

static void initializeCudaRuntimeImplementation(
	api::OcelotConfiguration::CudaRuntimeImplementation &cuda, 
	hydrazine::json::Visitor config) {

	cuda.implementation = config.parse<std::string>(
		"implementation", "CudaRuntime");
	cuda.runtimeApiTrace = config.parse<std::string>(
		"runtimeApiTrace", "trace/CudaAPI.trace");
}

api::OcelotConfiguration::Executive::Executive():
	defaultDeviceID(0),
	preferredISA(ir::Instruction::Emulated),
	optimizationLevel(translator::Translator::FullOptimization),
	required(false),
	enableLLVM(true),
	enableEmulated(true),
	enableNVIDIA(true),
	enableAMD(true),
	enableRemote(false),
	enableDynamicMulticore(false),
	port(2011),
	host("127.0.0.1"),
	workerThreadLimit(-1),
	warpSize(1)
{

}

api::OcelotConfiguration::Optimizations::Optimizations():
	subkernelSize(50),
	structuralTransform(false),
	predicateToSelect(false),
	linearScanAllocation(false),
	mimdThreadScheduling(false),
	syncElimination(false),
	vectorizeConvergent(false),
	vectorizeAffineMemory(false),
	lazyDynamicCompilation(true)
{

}

static void initializeExecutive(api::OcelotConfiguration::Executive &executive, 
	hydrazine::json::Visitor config) {

	std::string strPrefISA = config.parse<std::string>(
		"preferredISA", "emulated");
	std::string strOptLevel = config.parse<std::string>(
		"optimizationLevel", "full");
	std::string strReconvMech = config.parse<std::string>(
		"reconvergenceMechanism", "ipdom");
	std::string strPartitioning = config.parse<std::string>(
		"partitioningHeuristic", "minimumWithBarriers");
		
	executive.preferredISA = (int)ir::Instruction::Emulated;
	if (strPrefISA == "emulated" || strPrefISA == "Emulated") {
		executive.preferredISA = (int)ir::Instruction::Emulated;
	}
	else if (strPrefISA == "llvm" || strPrefISA == "LLVM") {
		executive.preferredISA = (int)ir::Instruction::LLVM;
	}
	else if (strPrefISA == "nvidia" || strPrefISA == "NVIDIA") {
		executive.preferredISA = (int)ir::Instruction::SASS;
	}
	else if (strPrefISA == "amd" || strPrefISA == "AMD") {
		executive.preferredISA = (int)ir::Instruction::CAL;
	}
	else if (strPrefISA == "remote" || strPrefISA == "Remote") {
		executive.preferredISA = (int)ir::Instruction::Remote;
	}
	else {
		report("Unknown preferredISA - using Emulated");
	}

	executive.optimizationLevel = (int)translator::Translator::NoOptimization;
	if (strOptLevel == "full") {
		executive.optimizationLevel = 
			(int)translator::Translator::FullOptimization;
	}
	else if (strOptLevel == "debug") {
		executive.optimizationLevel = 
			(int)translator::Translator::DebugOptimization;
	}
	else if (strOptLevel == "report") {
		executive.optimizationLevel = 
			(int)translator::Translator::ReportOptimization;
	}
	else if (strOptLevel == "basic") {
		executive.optimizationLevel = 
			(int)translator::Translator::BasicOptimization;
	}
	else if (strOptLevel == "aggressive") {
		executive.optimizationLevel = 
			(int)translator::Translator::AggressiveOptimization;
	}
	else if (strOptLevel == "space") {
		executive.optimizationLevel = 
			(int)translator::Translator::SpaceOptimization;
	}
	else if (strOptLevel == "instrument") {
		executive.optimizationLevel = 
			(int)translator::Translator::InstrumentOptimization;
	}
	else if (strOptLevel == "memcheck") {
		executive.optimizationLevel = 
			(int)translator::Translator::MemoryCheckOptimization;
	}
	else if (strOptLevel == "none") {
		executive.optimizationLevel = 
			(int)translator::Translator::NoOptimization;
	}
	else {
		report("Unknown optimization level - using none");
	}
	
	analysis::KernelPartitioningPass::PartitioningHeuristic partitioning =
		analysis::KernelPartitioningPass::fromString(strPartitioning);
	if (partitioning == analysis::KernelPartitioningPass::PartitioningHeuristic_invalid) {
		report("Unknown partitioning heuristic. Using minimumWithBarriers");
		partitioning = analysis::KernelPartitioningPass::Partition_minimumWithBarriers;
	}
	executive.partitioningHeuristic = (int)partitioning;

	executive.reconvergenceMechanism
		= (int)executive::ReconvergenceMechanism::Reconverge_IPDOM;
	if (strReconvMech == "ipdom") {
		executive.reconvergenceMechanism
			= (int)executive::ReconvergenceMechanism::Reconverge_IPDOM;
	}
	else if (strReconvMech == "barrier") {
		executive.reconvergenceMechanism
			= (int)executive::ReconvergenceMechanism::Reconverge_Barrier;
	}
	else if (strReconvMech == "tf-gen6") {
		executive.reconvergenceMechanism
			= (int)executive::ReconvergenceMechanism::Reconverge_TFGen6;
	}
	else if (strReconvMech == "tf-stack") {
		executive.reconvergenceMechanism
			= (int)executive::ReconvergenceMechanism::Reconverge_TFSortedStack;
	}
	else {
		report("Unknown reconvergence mechanism - ipdom");
	}

	executive.defaultDeviceID = config.parse<int>("defaultDeviceID", 0);
	executive.required = config.parse<bool>("required", false);
	executive.enableLLVM = config.parse<bool>("enableLLVM", true);
	executive.enableEmulated = config.parse<bool>("enableEmulated", true);
	executive.enableNVIDIA = config.parse<bool>("enableNVIDIA", true);
	executive.enableAMD = config.parse<bool>("enableAMD", true);
	executive.enableRemote = config.parse<bool>("enableRemote", false);
	executive.enableDynamicMulticore = config.parse<bool>("enableDynamic", false);
	executive.port = config.parse<int>("port", 2011);
	executive.host = config.parse<std::string>("host", "127.0.0.1");
	executive.workerThreadLimit = config.parse<int>("workerThreadLimit", -1);
	
	
	if (config.find("devices")) {
		hydrazine::json::Visitor devices = config["devices"];
		hydrazine::json::Array *array = 
			static_cast<hydrazine::json::Array *>(devices.value);
		
		executive.enableLLVM = false;
		executive.enableEmulated = false;
		executive.enableNVIDIA = false;
		executive.enableAMD = false;
		executive.enableRemote = false;
		executive.enableDynamicMulticore = false;
		
		for (hydrazine::json::Array::ValueVector::iterator it = array->begin();
			it != array->end(); ++it) {
			hydrazine::json::Visitor dev(*it);
			
			if ((std::string)dev == "llvm") {
				executive.enableLLVM = true;
			}
			else if ((std::string)dev == "nvidia") {
				executive.enableNVIDIA = true;
			}
			else if ((std::string)dev == "amd") {
				executive.enableAMD = true;
			}
			else if ((std::string)dev == "emulated") {
				executive.enableEmulated = true;
			}
			else if ((std::string)dev == "remote") {
				executive.enableRemote = true;
			}
			else if ((std::string)dev == "dynamic") {
				executive.enableDynamicMulticore = true;
			}
		}
	}
	
	executive.warpSize = config.parse<int>("warpSize", 1);
	if ((executive.enableDynamicMulticore || executive.enableLLVM) &&
		!hydrazine::isPowerOfTwo(executive.warpSize)) {
		int oldWs = executive.warpSize;
		executive.warpSize = 1;
		throw hydrazine::Exception("Warp size must be power of two. Encountered " + 
			boost::lexical_cast<std::string,int>(oldWs) + ". Defaulting to 1.");
	}
}

static void initializeOptimizations(
	api::OcelotConfiguration::Optimizations &optimizations, 
	hydrazine::json::Visitor config) {

	optimizations.subkernelSize = config.parse<int>("subkernelSize", 50);

	optimizations.structuralTransform =
		config.parse<bool>("structuralTransform", false);
			
	optimizations.predicateToSelect =
		config.parse<bool>("predicateToSelect", false);
			
	optimizations.linearScanAllocation =
		config.parse<bool>("linearScanAllocation", false);
			
	optimizations.mimdThreadScheduling =
		config.parse<bool>("mimdThreadScheduling", false);
	
	optimizations.syncElimination =
		config.parse<bool>("syncElimination", false);
		
	optimizations.vectorizeConvergent =
		config.parse<bool>("vectorizeConvergent", false);
	
	optimizations.vectorizeAffineMemory =
		config.parse<bool>("vectorizeAffineMemory", false);
	
	optimizations.lazyDynamicCompilation =
		config.parse<bool>("lazyDynamicCompilation", true);
}

api::OcelotConfiguration::OcelotConfiguration() {

}

api::OcelotConfiguration::OcelotConfiguration(std::istream &stream) {
	initialize(stream);
}

api::OcelotConfiguration::OcelotConfiguration(
	const std::string &_path): path(_path) {
	std::ifstream file(path.c_str());
	initialize(file);
}

//! \brief parses and returns configuration object
void *api::OcelotConfiguration::configuration() const {
	std::ifstream file(path.c_str());
	hydrazine::json::Parser parser;
	hydrazine::json::Object *config = 0;
	try {
		config = parser.parse_object(file);
	}
	catch (hydrazine::Exception exp) {
		std::cerr << "==Ocelot== WARNING: Could not parse config file '" 
			<< path << "', loading defaults.\n" << std::endl;
			
		std::cerr << "exception:\n" << exp.what() << std::endl;
	}
	return config;
}

void *api::OcelotConfiguration::initialize(std::istream &stream, bool preserve) {
	hydrazine::json::Parser parser;
	hydrazine::json::Object *config = 0;
	try {
		config = parser.parse_object(stream);

		hydrazine::json::Visitor main(config);
		if (main.find("trace")) {
			initializeTrace(trace, main["trace"]);
		}
		if (main.find("cuda")) {
			initializeCudaRuntimeImplementation(cuda, main["cuda"]);
		}
		if (main.find("executive")) {
			initializeExecutive(executive, main["executive"]);
		}
		if (main.find("checkpoint")) {
			initializeCheckpoint(checkpoint, main["checkpoint"]);
		}
		if (main.find("optimizations")) {
			initializeOptimizations(optimizations, main["optimizations"]);
		}
		version = main.parse<std::string>("version", 
			hydrazine::Version().toString());
		ocelot = main.parse<std::string>("ocelot", "ocelot");
	}
	catch (hydrazine::Exception exp) {
		std::cerr << "==Ocelot== WARNING: Could not parse config file '" 
			<< path << "', loading defaults.\n" << std::endl;
			
		std::cerr << "exception:\n" << exp.what() << std::endl;
	}
	if (!preserve) {
		delete config;
		config = 0;
	}
	return config;
}

////////////////////////////////////////////////////////////////////////////////

