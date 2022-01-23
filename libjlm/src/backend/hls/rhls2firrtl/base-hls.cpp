//
// Created by david on 7/5/21.
//

#include <algorithm>
#include <jlm/ir/hls/hls.h>
#include <jlm/ir/operators/lambda.hpp>
#include "jlm/backend/hls/rhls2firrtl/base-hls.hpp"
#include "jive/rvsdg/structural-node.hpp"
#include "jive/rvsdg/simple-node.hpp"


bool
jlm::hls::isForbiddenChar(char c) {
	if (('A' <= c && c <= 'Z') ||
		('a' <= c && c <= 'z') ||
		('0' <= c && c <= '9') ||
		'_' == c) {
		return false;
	}
	return true;
}

std::string
jlm::hls::BaseHLS::get_node_name(const jive::node *node) {
	auto found = node_map.find(node);
	if (found != node_map.end()) {
		return found->second;
	}
	auto new_name = jive::detail::strfmt("node_", node->operation().debug_string(), "_", node_map.size());
	// remove chars that are not valid in firrtl module names
	std::replace_if(new_name.begin(), new_name.end(), isForbiddenChar, '_');
	node_map[node] = new_name;
	return new_name;
}

std::string
jlm::hls::BaseHLS::get_port_name(jive::input *port) {
	std::string result;
	if (auto p = dynamic_cast<const jive::node_input *>(port)) {
		result += "i";
	} else if (auto p = dynamic_cast<const jive::result *>(port)) {
		result += "r";
	} else {
		throw std::logic_error(port->debug_string() + " not implemented!");
	}
	result += jive::detail::strfmt(port->index());
	return result;
}

std::string
jlm::hls::BaseHLS::get_port_name(jive::output *port) {
	if (port == nullptr) {
		throw std::logic_error("nullptr!");
	}
	std::string result;
	if (auto p = dynamic_cast<const jive::argument *>(port)) {
		result += "a";
	} else if (auto p = dynamic_cast<const jive::node_output *>(port)) {
		result += "o";
	} else if (auto p = dynamic_cast<const jive::structural_output *>(port)) {
		result += "so";
	} else {
		throw std::logic_error(port->debug_string() + " not implemented!");
	}
	result += jive::detail::strfmt(port->index());
	return result;
}

void
jlm::hls::BaseHLS::create_node_names(jive::region *r) {
	for (auto &node : r->nodes) {
		if (auto sn = dynamic_cast<jive::simple_node *>(&node)) {
			get_node_name(&node);
		} else if (auto oln = dynamic_cast<jlm::hls::loop_node *>(&node)) {
			create_node_names(oln->subregion());
		} else {
			throw jlm::error("Unimplemented op (unexpected structural node) : " + node.operation().debug_string());
		}
	}
}

const jlm::lambda::node *
jlm::hls::BaseHLS::get_hls_lambda(jlm::RvsdgModule &rm) {
	auto region = rm.Rvsdg().root();
	auto ln = dynamic_cast<const jlm::lambda::node *>(region->nodes.begin().ptr());
	if (region->nnodes() == 1 && ln) {
		return ln;
	} else {
		throw jlm::error("Root should have only one lambda node now");
	}
}

std::string
jlm::hls::BaseHLS::get_base_file_name(const jlm::RvsdgModule &rm) {
	auto source_file_name = rm.SourceFileName().name();
	auto base_file_name = source_file_name.substr(0, source_file_name.find_last_of('.'));
	return base_file_name;
}
