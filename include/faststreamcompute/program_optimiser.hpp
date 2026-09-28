#pragma once

#include "faststreamcompute/program.hpp"


namespace faststreamcompute {
    struct OptimisationOptions {
        bool remove_dead_nodes = true;
        bool fold_constants = true;
        bool eliminate_common_subexpressions = true;
    };

    // Uses the same floating-point environment as execution; exception flags are not preserved.
    Program optimiseProgram(const Program& original, OptimisationOptions options = {});
}
