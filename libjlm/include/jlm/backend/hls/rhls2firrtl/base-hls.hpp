//
// Created by david on 7/5/21.
//

#ifndef JLM_BACKEND_HLS_RHLS2FIRRTL_BASE_HLS_HPP
#define JLM_BACKEND_HLS_RHLS2FIRRTL_BASE_HLS_HPP

#include <jlm/ir/rvsdg-module.hpp>
#include <fstream>
#include <jlm/ir/operators/lambda.hpp>

namespace jlm {
	namespace hls {
		bool
		isForbiddenChar(char c);

		class BaseHLS {
		public:
			void
			run(jlm::rvsdg_module &rm) {
				assert(node_map.empty());
				// ensure consistent naming across runs
				create_node_names(get_hls_lambda(rm)->subregion());
				auto text = get_text(rm);
				auto source_file_name = rm.source_filename().name();
				std::string file_name = source_file_name.substr(0, source_file_name.find_last_of('.')) + extension();
				std::ofstream out_file;
				out_file.open(file_name);
				out_file << text;
				out_file.close();
			}

		private:
			virtual std::string
			extension() = 0;

		protected:
			std::unordered_map<const jive::node *, std::string> node_map;
			std::unordered_map<jive::output *, std::string> output_map;

			std::string
			get_node_name(const jive::node *node);

			static std::string
			get_port_name(jive::input *port);

			static std::string
			get_port_name(jive::output *port);

			const jlm::lambda::node *
			get_hls_lambda(jlm::rvsdg_module &rm);

			void
			create_node_names(jive::region *r);

			virtual std::string
			get_text(jlm::rvsdg_module &rm) = 0;
		};
	}
}


#endif //JLM_BACKEND_HLS_RHLS2FIRRTL_BASE_HLS_HPP
