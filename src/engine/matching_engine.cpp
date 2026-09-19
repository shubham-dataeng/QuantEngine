#include "quantengine/engine/generic_matching_engine.hpp"

namespace quantengine::engine {

// Explicit template instantiations for compilation unit verification
template class GenericMatchingEngine<reference::ReferenceOrderBook>;
template class GenericMatchingEngine<optimized::OptimizedOrderBook>;

}  // namespace quantengine::engine
