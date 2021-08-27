/*
 * Copyright 2017 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_OPT_INLINE_HPP
#define JLM_OPT_INLINE_HPP

#include <jlm/opt/optimization.hpp>
#include <jive/rvsdg.hpp>

namespace jlm {

class rvsdg_module;
class stats_descriptor;

/**
* \brief Function Inlining
*/
class fctinline final : public optimization {
public:
	virtual
	~fctinline();

	virtual void
	run(rvsdg_module & module, const stats_descriptor & sd) override;
};

jive::output *
find_producer(jive::input * input);

void
inline_apply(const jive::structural_node * lambda, jive::simple_node * apply);

}

#endif
