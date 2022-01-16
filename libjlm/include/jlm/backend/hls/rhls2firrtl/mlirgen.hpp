//
// Created by Magnus Sjalander 26 December 2021
//

#ifndef JLM_BACKEND_HLS_RHLS2FIRRTL_MLIRGEN_HPP
#define JLM_BACKEND_HLS_RHLS2FIRRTL_MLIRGEN_HPP

#include <jive/types/bitstring/type.hpp>
#include <jlm/ir/operators/operators.hpp>
#include <jive/types/bitstring/comparison.hpp>
#include <jlm/ir/operators/sext.hpp>
#include <jlm/ir/operators/getelementptr.hpp>
#include <jlm/ir/operators/load.hpp>
#include <jlm/ir/operators/store.hpp>
#include <jlm/ir/hls/hls.h>
#include <jive/rvsdg/traverser.hpp>

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Translation.h"

#include "circt/Dialect/FIRRTL/FIREmitter.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/FIRRTL/Namespace.h"
#include "circt/Support/LLVM.h"

#include "base-hls.hpp"

namespace jlm {
	namespace hls{
		class MLIRGenImpl : public BaseHLS {
			std::string
			extension() override {
				return ".fir";
				}
		public:
			std::string get_text(jlm::rvsdg_module &rvsdg_module) {return "MLIR/FIRRTL generator";}
			MLIRGenImpl(mlir::MLIRContext &context) : builder(&context) {}
			mlir::ModuleOp mlirGen(jlm::rvsdg_module &rvsdg_module);
		private:
			// Variables
			mlir::OpBuilder builder;
			// We don't have any locations (i.e., the originating line in the
			// source file) in RVSDG, so we set all operations to unkown
			mlir::Location location = builder.getUnknownLoc();

			// FIRRTL generating functions
			circt::firrtl::CircuitOp mlirGen(const jlm::lambda::node *lamda_node);
			circt::firrtl::FModuleOp mlirGen(hls::loop_node *looNode, mlir::Block *circuitBody);
			circt::firrtl::FModuleOp mlirGen(jive::region *subRegion, mlir::Block *circuitBody);
			circt::firrtl::FModuleOp mlirGen(const jive::simple_node *node);
			// Operations
			circt::firrtl::FModuleOp mlirGenSink(const jive::simple_node *node);
			circt::firrtl::FModuleOp mlirGenFork(const jive::simple_node *node);
			circt::firrtl::FModuleOp mlirGenSimpleNode(const jive::simple_node *node);

			// Helper functions
			void addClockPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports);
			void addResetPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports);
			void addMemReqPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports);
			void addMemResPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports);
			void addBundlePort(
					llvm::SmallVector<circt::firrtl::PortInfo> *ports,
					circt::firrtl::Direction direction,
					std::string name,
					circt::firrtl::FIRRTLType type);
			circt::firrtl::SubfieldOp getSubfield(
					mlir::Block *body,
					mlir::Value value,
					int index);
			void connect(mlir::Block *body, mlir::Value sink, mlir::Value source);
			void partialConnect(mlir::Block *body, mlir::Value sink, mlir::Value source);
			circt::firrtl::ConstantOp getConstant(mlir::Block *body, int size, int value);
			void initializeMemReq(circt::firrtl::FModuleOp module);
			circt::firrtl::BundleType::BundleElement getReadyElement();
			circt::firrtl::BundleType::BundleElement getValidElement();
			mlir::BlockArgument getClockSignal(circt::firrtl::FModuleOp module);
			mlir::BlockArgument getResetSignal(circt::firrtl::FModuleOp module);
			circt::firrtl::FModuleOp nodeToModule(const jive::simple_node *node);
			circt::firrtl::IntType getIntType(int size);
			circt::firrtl::IntType getIntType(const jive::type *type, int extend = 0);
			int jlmSize(const jive::type *type);
			mlir::StringAttr getModuleName(const jive::node *node);
		};

		class MLIRGen : public BaseHLS {
			std::string
			extension() override {
				return ".fir";
			}

			std::string get_text(jlm::rvsdg_module &rm) {return "";}
		public:
			void
			run(jlm::rvsdg_module &rm) {
				assert(node_map.empty());
				// Ensure consistent naming across runs
				create_node_names(get_hls_lambda(rm)->subregion());

				mlir::MLIRContext context;
				
				// Load the FIRRTLDialect
				context.getOrLoadDialect<circt::firrtl::FIRRTLDialect>();

				// Convert RVSDG handshake to MLIR
				mlir::ModuleOp module = MLIRGenImpl(context).mlirGen(rm);
				//mlir::ModuleOp module = MLIRGenImpl().mlirGen(rm);
				if (!module)
					return;

				std::basic_string<char,
						  std::char_traits<char>,
						  std::allocator<char>> base_file_name =
					get_base_file_name(rm);
				std::string file_name = base_file_name + extension();
				std::error_code EC;

				llvm::raw_fd_ostream output(file_name, EC);
				auto status = circt::firrtl::exportFIRFile(module, output);
				if (status.failed())
					throw std::logic_error("Exporting of firrtl failed");					
				output.close();
			}
			private:
		};

	} // namespace hls
} // namespace jlm

#endif // JLM_BACKEND_HLS_RHLS2FIRRTL_MLIRGEN_HPP
