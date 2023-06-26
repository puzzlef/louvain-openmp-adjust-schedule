#pragma once
#include <utility>
#include <algorithm>
#include <vector>
#include "_main.hxx"
#include "Graph.hxx"
#include "duplicate.hxx"
#include "properties.hxx"
#include "csr.hxx"
#include "modularity.hxx"

#ifdef OPENMP
#include <omp.h>
#endif

using std::pair;
using std::vector;
using std::make_pair;
using std::move;
using std::swap;
using std::min;
using std::max;




// LOUVAIN OPTIONS
// ---------------

struct LouvainOptions {
  int    repeat;
  double resolution;
  double tolerance;
  double aggregationTolerance;
  double toleranceDecline;
  int    maxIterations;
  int    maxPasses;

  LouvainOptions(int repeat=1, double resolution=1, double tolerance=1e-2, double aggregationTolerance=0.8, double toleranceDecline=100, int maxIterations=20, int maxPasses=10) :
  repeat(repeat), resolution(resolution), tolerance(tolerance), aggregationTolerance(aggregationTolerance), toleranceDecline(toleranceDecline), maxIterations(maxIterations), maxPasses(maxPasses) {}
};

// Weight to be using in hashtable.
#define LOUVAIN_WEIGHT_TYPE double




// LOUVAIN RESULT
// --------------

template <class K>
struct LouvainResult {
  vector<K> membership;
  int   iterations;
  int   passes;
  float time;
  float preprocessingTime;
  float firstPassTime;
  float localMoveTime;
  float aggregationTime;
  size_t affectedVertices;

  LouvainResult(vector<K>&& membership, int iterations=0, int passes=0, float time=0, float preprocessingTime=0, float firstPassTime=0, float localMoveTime=0, float aggregationTime=0, size_t affectedVertices=0) :
  membership(membership), iterations(iterations), passes(passes), time(time), preprocessingTime(preprocessingTime), firstPassTime(firstPassTime), localMoveTime(localMoveTime), aggregationTime(aggregationTime), affectedVertices(affectedVertices) {}

  LouvainResult(vector<K>& membership, int iterations=0, int passes=0, float time=0, float preprocessingTime=0, float firstPassTime=0, float localMoveTime=0, float aggregationTime=0, size_t affectedVertices=0) :
  membership(move(membership)), iterations(iterations), passes(passes), time(time), preprocessingTime(preprocessingTime), firstPassTime(firstPassTime), localMoveTime(localMoveTime), aggregationTime(aggregationTime), affectedVertices(affectedVertices) {}
};




// LOUVAIN HASHTABLES
// ------------------

/**
 * Allocate a number of hashtables.
 * @param vcs communities vertex u is linked to (updated)
 * @param vcout total edge weight from vertex u to community C (updated)
 * @param S size of each hashtable
 */
template <class K, class W>
inline void louvainAllocateHashtablesW(vector<vector<K>*>& vcs, vector<vector<W>*>& vcout, size_t S) {
  size_t N = vcs.size();
  for (size_t i=0; i<N; ++i) {
    vcs[i]   = new vector<K>();
    vcout[i] = new vector<W>(S);
  }
}


/**
 * Free a number of hashtables.
 * @param vcs communities vertex u is linked to (updated)
 * @param vcout total edge weight from vertex u to community C (updated)
 */
template <class K, class W>
inline void louvainFreeHashtablesW(vector<vector<K>*>& vcs, vector<vector<W>*>& vcout) {
  size_t N = vcs.size();
  for (size_t i=0; i<N; ++i) {
    delete vcs[i];
    delete vcout[i];
  }
}




// LOUVAIN INITIALIZE
// ------------------

/**
 * Find the total edge weight of each vertex.
 * @param vtot total edge weight of each vertex (updated, must be initialized)
 * @param x original graph
 */
template <class G, class W>
inline void louvainVertexWeightsW(vector<W>& vtot, const G& x) {
  x.forEachVertexKey([&](auto u) {
    x.forEachEdge(u, [&](auto v, auto w) {
      vtot[u] += w;
    });
  });
}

#ifdef OPENMP
template <class G, class W>
inline void louvainVertexWeightsOmpW(vector<W>& vtot, const G& x) {
  using  K = typename G::key_type;
  size_t S = x.span();
  #pragma omp parallel for schedule(dynamic, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    x.forEachEdge(u, [&](auto v, auto w) { vtot[u] += w; });
  }
}
#endif


/**
 * Find the total edge weight of each community.
 * @param ctot total edge weight of each community (updated, must be initialized)
 * @param x original graph
 * @param vcom community each vertex belongs to
 * @param vtot total edge weight of each vertex
 */
template <class G, class K, class W>
inline void louvainCommunityWeightsW(vector<W>& ctot, const G& x, const vector<K>& vcom, const vector<W>& vtot) {
  x.forEachVertexKey([&](auto u) {
    K c = vcom[u];
    ctot[c] += vtot[u];
  });
}

#ifdef OPENMP
template <class G, class K, class W>
inline void louvainCommunityWeightsOmpW(vector<W>& ctot, const G& x, const vector<K>& vcom, const vector<W>& vtot) {
  size_t S = x.span();
  #pragma omp parallel for schedule(static, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    K c = vcom[u];
    #pragma omp atomic
    ctot[c] += vtot[u];
  }
}
#endif


/**
 * Initialize communities such that each vertex is its own community.
 * @param vcom community each vertex belongs to (updated, must be initialized)
 * @param ctot total edge weight of each community (updated, must be initialized)
 * @param x original graph
 * @param vtot total edge weight of each vertex
 */
template <class G, class K, class W>
inline void louvainInitializeW(vector<K>& vcom, vector<W>& ctot, const G& x, const vector<W>& vtot) {
  x.forEachVertexKey([&](auto u) {
    vcom[u] = u;
    ctot[u] = vtot[u];
  });
}

#ifdef OPENMP
template <class G, class K, class W>
inline void louvainInitializeOmpW(vector<K>& vcom, vector<W>& ctot, const G& x, const vector<W>& vtot) {
  size_t S = x.span();
  #pragma omp parallel for schedule(static, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    vcom[u] = u;
    ctot[u] = vtot[u];
  }
}
#endif


/**
 * Initialize communities from given initial communities.
 * @param vcom community each vertex belongs to (updated, must be initialized)
 * @param ctot total edge weight of each community (updated, must be initialized)
 * @param x original graph
 * @param vtot total edge weight of each vertex
 * @param q initial community each vertex belongs to
 */
template <class G, class K, class W>
inline void louvainInitializeFromW(vector<K>& vcom, vector<W>& ctot, const G& x, const vector<W>& vtot, const vector<K>& q) {
  x.forEachVertexKey([&](auto u) {
    K c = q[u];
    vcom[u]  = c;
    ctot[c] += vtot[u];
  });
}

#ifdef OPENMP
template <class G, class K, class W>
inline void louvainInitializeFromOmpW(vector<K>& vcom, vector<W>& ctot, const G& x, const vector<W>& vtot, const vector<K>& q) {
  size_t S = x.span();
  #pragma omp parallel for schedule(static, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    K c = q[u];
    vcom[u]  = c;
    #pragma omp atomic
    ctot[c] += vtot[u];
  }
}
#endif




// LOUVAIN CHANGE COMMUNITY
// ------------------------

/**
 * Scan an edge community connected to a vertex.
 * @param vcs communities vertex u is linked to (updated)
 * @param vcout total edge weight from vertex u to community C (updated)
 * @param u given vertex
 * @param v outgoing edge vertex
 * @param w outgoing edge weight
 * @param vcom community each vertex belongs to
 */
template <bool SELF=false, class K, class V, class W>
inline void louvainScanCommunityW(vector<K>& vcs, vector<W>& vcout, K u, K v, V w, const vector<K>& vcom) {
  if (!SELF && u==v) return;
  K c = vcom[v];
  if (!vcout[c]) vcs.push_back(c);
  vcout[c] += w;
}


/**
 * Scan communities connected to a vertex.
 * @param vcs communities vertex u is linked to (updated)
 * @param vcout total edge weight from vertex u to community C (updated)
 * @param x original graph
 * @param u given vertex
 * @param vcom community each vertex belongs to
 */
template <bool SELF=false, class G, class K, class W>
inline void louvainScanCommunitiesW(vector<K>& vcs, vector<W>& vcout, const G& x, K u, const vector<K>& vcom) {
  x.forEachEdge(u, [&](auto v, auto w) { louvainScanCommunityW<SELF>(vcs, vcout, u, v, w, vcom); });
}


/**
 * Clear communities scan data.
 * @param vcs total edge weight from vertex u to community C (updated)
 * @param vcout communities vertex u is linked to (updated)
 */
template <class K, class W>
inline void louvainClearScanW(vector<K>& vcs, vector<W>& vcout) {
  for (K c : vcs)
    vcout[c] = W();
  vcs.clear();
}


/**
 * Choose connected community with best delta modularity.
 * @param x original graph
 * @param u given vertex
 * @param vcom community each vertex belongs to
 * @param vtot total edge weight of each vertex
 * @param ctot total edge weight of each community
 * @param vcs communities vertex u is linked to
 * @param vcout total edge weight from vertex u to community C
 * @param M total weight of "undirected" graph (1/2 of directed graph)
 * @param R resolution (0, 1]
 * @returns [best community, delta modularity]
 */
template <bool SELF=false, class G, class K, class W>
inline auto louvainChooseCommunity(const G& x, K u, const vector<K>& vcom, const vector<W>& vtot, const vector<W>& ctot, const vector<K>& vcs, const vector<W>& vcout, double M, double R) {
  K cmax = K(), d = vcom[u];
  W emax = W();
  for (K c : vcs) {
    if (!SELF && c==d) continue;
    W e = deltaModularity(vcout[c], vcout[d], vtot[u], ctot[c], ctot[d], M, R);
    if (e>emax) { emax = e; cmax = c; }
  }
  return make_pair(cmax, emax);
}


/**
 * Move vertex to another community C.
 * @param vcom community each vertex belongs to (updated)
 * @param ctot total edge weight of each community (updated)
 * @param x original graph
 * @param u given vertex
 * @param c community to move to
 * @param vtot total edge weight of each vertex
 */
template <class G, class K, class W>
inline void louvainChangeCommunityW(vector<K>& vcom, vector<W>& ctot, const G& x, K u, K c, const vector<W>& vtot) {
  K d = vcom[u];
  ctot[d] -= vtot[u];
  ctot[c] += vtot[u];
  vcom[u] = c;
}

#ifdef OPENMP
template <class G, class K, class W>
inline void louvainChangeCommunityOmpW(vector<K>& vcom, vector<W>& ctot, const G& x, K u, K c, const vector<W>& vtot) {
  K d = vcom[u];
  #pragma omp atomic
  ctot[d] -= vtot[u];
  #pragma omp atomic
  ctot[c] += vtot[u];
  vcom[u] = c;
}
#endif




// LOUVAIN MOVE
// ------------

/**
 * Louvain algorithm's local moving phase.
 * @param vcom community each vertex belongs to (initial, updated)
 * @param ctot total edge weight of each community (precalculated, updated)
 * @param vaff is vertex affected flag (updated)
 * @param vcs communities vertex u is linked to (temporary buffer, updated)
 * @param vcout total edge weight from vertex u to community C (temporary buffer, updated)
 * @param x original graph
 * @param vtot total edge weight of each vertex
 * @param M total weight of "undirected" graph (1/2 of directed graph)
 * @param R resolution (0, 1]
 * @param L max iterations
 * @param fc has local moving phase converged?
 * @returns iterations performed (0 if converged already)
 */
template <class G, class K, class W, class B, class FC>
inline int louvainMoveW(vector<K>& vcom, vector<W>& ctot, vector<B>& vaff, vector<K>& vcs, vector<W>& vcout, const G& x, const vector<W>& vtot, double M, double R, int L, FC fc) {
  int l = 0;
  W  el = W();
  for (; l<L;) {
    el = W();
    x.forEachVertexKey([&](auto u) {
      if (!vaff[u]) return;
      louvainClearScanW(vcs, vcout);
      louvainScanCommunitiesW(vcs, vcout, x, u, vcom);
      auto [c, e] = louvainChooseCommunity(x, u, vcom, vtot, ctot, vcs, vcout, M, R);
      if (c)      { louvainChangeCommunityW(vcom, ctot, x, u, c, vtot); x.forEachEdgeKey(u, [&](auto v) { vaff[v] = B(1); }); }
      vaff[u] = B();
      el += e;  // l1-norm
    });
    if (fc(el, l++)) break;
  }
  return l>1 || el? l : 0;
}

#ifdef OPENMP
template <class G, class K, class W, class B, class FC>
inline int louvainMoveOmpW(vector<K>& vcom, vector<W>& ctot, vector<B>& vaff, vector<vector<K>*>& vcs, vector<vector<W>*>& vcout, const G& x, const vector<W>& vtot, double M, double R, int L, FC fc) {
  size_t S = x.span();
  int l = 0;
  W  el = W();
  for (; l<L;) {
    el = W();
    #pragma omp parallel for schedule(dynamic, 2048) reduction(+:el)
    for (K u=0; u<S; ++u) {
      int t = omp_get_thread_num();
      if (!x.hasVertex(u)) continue;
      if (!vaff[u]) continue;
      louvainClearScanW(*vcs[t], *vcout[t]);
      louvainScanCommunitiesW(*vcs[t], *vcout[t], x, u, vcom);
      auto [c, e] = louvainChooseCommunity(x, u, vcom, vtot, ctot, *vcs[t], *vcout[t], M, R);
      if (c)      { louvainChangeCommunityOmpW(vcom, ctot, x, u, c, vtot); x.forEachEdgeKey(u, [&](auto v) { vaff[v] = B(1); }); }
      vaff[u] = B();
      el += e;  // l1-norm
    }
    if (fc(el, l++)) break;
  }
  return l>1 || el? l : 0;
}
#endif




// LOUVAIN COMMUNITY PROPERTIES
// ----------------------------

/**
 * Examine if each community exists.
 * @param a does each community exist (updated)
 * @param x original graph
 * @param vcom community each vertex belongs to
 * @returns number of communities
 */
template <class G, class K, class A>
inline size_t louvainCommunityExistsW(vector<A>& a, const G& x, const vector<K>& vcom) {
  size_t C = 0;
  fillValueU(a, A());
  x.forEachVertexKey([&](auto u) {
    K c = vcom[u];
    if (!a[c]) ++C;
    a[c] = A(1);
  });
  return C;
}

#ifdef OPENMP
template <class G, class K, class A>
inline size_t louvainCommunityExistsOmpW(vector<A>& a, const G& x, const vector<K>& vcom) {
  size_t S = x.span();
  size_t C = 0;
  fillValueOmpU(a, A());
  #pragma omp parallel for schedule(static, 2048) reduction(+:C)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    K c = vcom[u];
    A m = A();
    #pragma omp atomic capture
    { m = a[c]; a[c] = A(1); }
    if (!m) ++C;
  }
  return C;
}
#endif




/**
 * Find the total degree of each community.
 * @param a total degree of each community (updated)
 * @param x original graph
 * @param vcom community each vertex belongs to
 */
template <class G, class K, class A>
inline void louvainCommunityTotalDegreeW(vector<A>& a, const G& x, const vector<K>& vcom) {
  fillValueU(a, A());
  x.forEachVertexKey([&](auto u) {
    K c   = vcom[u];
    a[c] += x.degree(u);
  });
}

#ifdef OPENMP
template <class G, class K, class A>
inline void louvainCommunityTotalDegreeOmpW(vector<A>& a, const G& x, const vector<K>& vcom) {
  size_t S = x.span();
  fillValueOmpU(a, A());
  #pragma omp parallel for schedule(static, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    K c = vcom[u];
    #pragma omp atomic
    a[c] += x.degree(u);
  }
}
#endif




/**
 * Find the number of vertices in each community.
 * @param a number of vertices belonging to each community (updated)
 * @param x original graph
 * @param vcom community each vertex belongs to
 */
template <class G, class K, class A>
inline void louvainCountCommunityVerticesW(vector<A>& a, const G& x, const vector<K>& vcom) {
  fillValueU(a, A());
  x.forEachVertexKey([&](auto u) {
    K c = vcom[u];
    ++a[c];
  });
}

#ifdef OPENMP
template <class G, class K, class A>
inline void louvainCountCommunityVerticesOmpW(vector<A>& a, const G& x, const vector<K>& vcom) {
  size_t S = x.span();
  fillValueOmpU(a, A());
  #pragma omp parallel for schedule(static, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    K c = vcom[u];
    #pragma omp atomic
    ++a[c];
  }
}
#endif




/**
 * Find the vertices in each community.
 * @param coff csr offsets for vertices belonging to each community (updated)
 * @param cdeg number of vertices in each community (updated)
 * @param cedg vertices belonging to each community (updated)
 * @param x original graph
 * @param vcom community each vertex belongs to
 */
template <class G, class K>
inline void louvainCommunityVerticesW(vector<K>& coff, vector<K>& cdeg, vector<K>& cedg, const G& x, const vector<K>& vcom) {
  size_t C = coff.size() - 1;
  louvainCountCommunityVerticesW(coff, x, vcom);
  coff[C] = exclusiveScanW(coff, coff);
  fillValueU(cdeg, K());
  x.forEachVertexKey([&](auto u) {
    K c = vcom[u];
    csrAddEdgeU(cdeg, cedg, coff, c, u);
  });
}

#ifdef OPENMP
template <class G, class K>
inline void louvainCommunityVerticesOmpW(vector<K>& coff, vector<K>& cdeg, vector<K>& cedg, vector<K>& bufk, const G& x, const vector<K>& vcom) {
  size_t S = x.span();
  size_t C = coff.size() - 1;
  louvainCountCommunityVerticesOmpW(coff, x, vcom);
  coff[C] = exclusiveScanOmpW(coff, bufk, coff);
  fillValueOmpU(cdeg, K());
  #pragma omp parallel for schedule(static, 2048)
  for (K u=0; u<S; ++u) {
    if (!x.hasVertex(u)) continue;
    K c = vcom[u];
    csrAddEdgeOmpU(cdeg, cedg, coff, c, u);
  }
}
#endif




// LOUVAIN LOOKUP COMMUNITIES
// --------------------------

/**
 * Update community membership in a tree-like fashion (to handle aggregation).
 * @param a output community each vertex belongs to (updated)
 * @param vcom community each vertex belongs to (at this aggregation level)
 */
template <class K>
inline void louvainLookupCommunitiesU(vector<K>& a, const vector<K>& vcom) {
  for (auto& v : a)
    v = vcom[v];
}

#ifdef OPENMP
template <class K>
inline void louvainLookupCommunitiesOmpU(vector<K>& a, const vector<K>& vcom) {
  size_t S = a.size();
  #pragma omp parallel for schedule(static, 2048)
  for (size_t u=0; u<S; ++u)
    a[u] = vcom[a[u]];
}
#endif




// LOUVAIN AGGREGATE
// -----------------

/**
 * Aggregate outgoing edges of each community.
 * @param ydeg degree of each community (updated)
 * @param yedg vertex ids of outgoing edges of each community (updated)
 * @param ywei weights of outgoing edges of each community (updated)
 * @param vcs communities vertex u is linked to (temporary buffer, updated)
 * @param vcout total edge weight from vertex u to community C (temporary buffer, updated)
 * @param x original graph
 * @param vcom community each vertex belongs to
 * @param coff offsets for vertices belonging to each community
 * @param cedg vertices belonging to each community
 * @param yoff offsets for vertices belonging to each community
 */
template <class G, class K, class W>
inline void louvainAggregateEdgesW(vector<K>& ydeg, vector<K>& yedg, vector<W>& ywei, vector<K>& vcs, vector<W>& vcout, const G& x, const vector<K>& vcom, const vector<K>& coff, const vector<K>& cedg, const vector<size_t>& yoff) {
  size_t C = coff.size() - 1;
  fillValueU(ydeg, K());
  for (K c=0; c<C; ++c) {
    K n = csrDegree(coff, c);
    if (n==0) continue;
    louvainClearScanW(vcs, vcout);
    csrForEachEdgeKey(coff, cedg, c, [&](auto u) {
      louvainScanCommunitiesW<true>(vcs, vcout, x, u, vcom);
    });
    for (auto d : vcs)
      csrAddEdgeU(ydeg, yedg, ywei, yoff, c, d, vcout[d]);
  }
}

#ifdef OPENMP
template <class G, class K, class W>
inline void louvainAggregateEdgesOmpW(vector<K>& ydeg, vector<K>& yedg, vector<W>& ywei, vector<vector<K>*>& vcs, vector<vector<W>*>& vcout, const G& x, const vector<K>& vcom, const vector<K>& coff, const vector<K>& cedg, const vector<size_t>& yoff) {
  size_t C = coff.size() - 1;
  fillValueOmpU(ydeg, K());
  #pragma omp parallel for schedule(dynamic, 2048)
  for (K c=0; c<C; ++c) {
    int t = omp_get_thread_num();
    K   n = csrDegree(coff, c);
    if (n==0) continue;
    louvainClearScanW(*vcs[t], *vcout[t]);
    csrForEachEdgeKey(coff, cedg, c, [&](auto u) {
      louvainScanCommunitiesW<true>(*vcs[t], *vcout[t], x, u, vcom);
    });
    for (auto d : *vcs[t])
      csrAddEdgeU(ydeg, yedg, ywei, yoff, c, d, (*vcout[t])[d]);
  }
}
#endif


/**
 * Re-number communities such that they are numbered 0, 1, 2, ...
 * @param vcom community each vertex belongs to (updated)
 * @param cext does each community exist (updated)
 * @param x original graph
 * @returns number of communities
 */
template <class G, class K>
inline size_t louvainRenumberCommunitiesW(vector<K>& vcom, vector<K>& cext, const G& x) {
  size_t C = exclusiveScanW(cext, cext);
  louvainLookupCommunitiesU(vcom, cext);
  return C;
}

#ifdef OPENMP
template <class G, class K>
inline size_t louvainRenumberCommunitiesOmpW(vector<K>& vcom, vector<K>& cext, vector<K>& bufk, const G& x) {
  size_t C = exclusiveScanOmpW(cext, bufk, cext);
  louvainLookupCommunitiesOmpU(vcom, cext);
  return C;
}
#endif


/**
 * Louvain algorithm's community aggregation phase.
 * @param yoff offsets for vertices belonging to each community (updated)
 * @param ydeg degree of each community (updated)
 * @param yedg vertex ids of outgoing edges of each community (updated)
 * @param ywei weights of outgoing edges of each community (updated)
 * @param vcs communities vertex u is linked to (temporary buffer, updated)
 * @param vcout total edge weight from vertex u to community C (temporary buffer, updated)
 * @param x original graph
 * @param vcom community each vertex belongs to
 * @param coff offsets for vertices belonging to each community
 * @param cedg vertices belonging to each community
 */
template <class G, class K, class W>
inline void louvainAggregateW(vector<size_t>& yoff, vector<K>& ydeg, vector<K>& yedg, vector<W>& ywei, vector<K>& vcs, vector<W>& vcout, const G& x, const vector<K>& vcom, vector<K>& coff, vector<K>& cedg) {
  size_t C = coff.size() - 1;
  louvainCommunityTotalDegreeW(yoff, x, vcom);
  yoff[C] = exclusiveScanW(yoff, yoff);
  louvainAggregateEdgesW(ydeg, yedg, ywei, vcs, vcout, x, vcom, coff, cedg, yoff);
}

#ifdef OPENMP
template <class G, class K, class W>
inline void louvainAggregateOmpW(vector<size_t>& yoff, vector<K>& ydeg, vector<K>& yedg, vector<W>& ywei, vector<size_t>& bufs, vector<vector<K>*>& vcs, vector<vector<W>*>& vcout, const G& x, const vector<K>& vcom, vector<K>& coff, vector<K>& cedg) {
  size_t C = coff.size() - 1;
  louvainCommunityTotalDegreeOmpW(yoff, x, vcom);
  yoff[C] = exclusiveScanOmpW(yoff, bufs, yoff);
  louvainAggregateEdgesOmpW(ydeg, yedg, ywei, vcs, vcout, x, vcom, coff, cedg, yoff);
}
#endif




// LOUVAIN
// -------

/**
 * Find the community each vertex belongs to.
 * @param x original graph
 * @param q initial community each vertex belongs to
 * @param o louvain options
 * @param fm marking affected vertices / preprocessing to be performed (vaff)
 * @returns community each vertex belongs to
 */
template <class FLAG=char, class G, class K, class FM>
auto louvainSeq(const G& x, const vector<K> *q, const LouvainOptions& o, FM fm) {
  using  W = LOUVAIN_WEIGHT_TYPE;
  using  B = FLAG;
  double R = o.resolution;
  int    L = o.maxIterations, l = 0;
  int    P = o.maxPasses, p = 0;
  size_t S = x.span(), naff = 0;
  double M = edgeWeight(x)/2;
  vector<B> vaff(S);
  vector<K> vcom(S), a(S);
  vector<W> vtot(S), ctot(S);
  vector<K> vcs;
  vector<W> vcout(S);
  DiGraphCsr<K, None, None, K> cv(S, S);
  float tm = 0, tp = 0, tl = 0, ta = 0;
  float t  = measureDurationMarked([&](auto mark) {
    double E  = o.tolerance;
    auto   fc = [&](double el, int l) { return el<=E; };
    DiGraphCsr<K, None, W> y(S, x.size());
    DiGraphCsr<K, None, W> z(S, x.size());
    fillValueU(vcom, K());
    fillValueU(vtot, W());
    fillValueU(ctot, W());
    fillValueU(a, K());
    mark([&]() {
      tm += measureDuration([&]() { fm(vaff); });
      naff = sumValues(vaff, size_t());
      auto t0 = timeNow(), t1 = t0;
      louvainVertexWeightsW(vtot, x);
      if (q) louvainInitializeFromW(vcom, ctot, x, vtot, *q);
      else   louvainInitializeW(vcom, ctot, x, vtot);
      for (l=0, p=0; M>0 && p<P;) {
        if (p==1) t1 = timeNow();
        bool isFirst = p==0;
        int m = 0;
        tl += measureDuration([&]() {
          if (isFirst) m = louvainMoveW(vcom, ctot, vaff, vcs, vcout, x, vtot, M, R, L, fc);
          else         m = louvainMoveW(vcom, ctot, vaff, vcs, vcout, y, vtot, M, R, L, fc);
        });
        if (isFirst) copyValuesW(a, vcom);
        else         louvainLookupCommunitiesU(a, vcom);
        l += max(m, 1); ++p;
        if (m<=1 || p>=P) break;
        size_t GN = isFirst? x.order() : y.order();
        size_t GS = isFirst? x.span()  : y.span();
        size_t CN = 0;
        if (isFirst) CN = louvainCommunityExistsW(cv.degrees, x, vcom);
        else         CN = louvainCommunityExistsW(cv.degrees, y, vcom);
        if (double(CN)/GN >= o.aggregationTolerance) break;
        if (isFirst) louvainRenumberCommunitiesW(vcom, cv.degrees, x);
        else         louvainRenumberCommunitiesW(vcom, cv.degrees, y);
        cv.respan(CN); z.respan(CN);
        if (isFirst) louvainCommunityVerticesW(cv.offsets, cv.degrees, cv.edgeKeys, x, vcom);
        else         louvainCommunityVerticesW(cv.offsets, cv.degrees, cv.edgeKeys, y, vcom);
        ta += measureDuration([&]() {
          if (isFirst) louvainAggregateW(z.offsets, z.degrees, z.edgeKeys, z.edgeValues, vcs, vcout, x, vcom, cv.offsets, cv.edgeKeys);
          else         louvainAggregateW(z.offsets, z.degrees, z.edgeKeys, z.edgeValues, vcs, vcout, y, vcom, cv.offsets, cv.edgeKeys);
          swap(y, z);
        });
        fillValueU(vcom, K());
        fillValueU(vtot, W());
        fillValueU(ctot, W());
        fillValueU(vaff, B(1));
        louvainVertexWeightsW(vtot, y);
        louvainInitializeW(vcom, ctot, y, vtot);
        E /= o.toleranceDecline;
      }
      if (p<=1) t1 = timeNow();
      tp += duration(t0, t1);
    });
  }, o.repeat);
  return LouvainResult<K>(a, l, p, t, tm/o.repeat, tp/o.repeat, tl/o.repeat, ta/o.repeat, naff);
}

#ifdef OPENMP
template <class FLAG=char, class G, class K, class FM>
auto louvainOmp(const G& x, const vector<K> *q, const LouvainOptions& o, FM fm) {
  using  W = LOUVAIN_WEIGHT_TYPE;
  using  B = FLAG;
  double R = o.resolution;
  int    L = o.maxIterations, l = 0;
  int    P = o.maxPasses, p = 0;
  size_t S = x.span(), naff = 0;
  double M = edgeWeightOmp(x)/2;
  int    T = omp_get_max_threads();
  vector<B> vaff(S);
  vector<K> vcom(S), a(S);
  vector<W> vtot(S), ctot(S);
  vector<K> bufk(T);
  vector<size_t> bufs(T);
  vector<vector<K>*> vcs(T);
  vector<vector<W>*> vcout(T);
  louvainAllocateHashtablesW(vcs, vcout, S);
  DiGraphCsr<K, None, None, K> cv(S, S);
  DiGraphCsr<K, None, W> y(S, x.size());
  DiGraphCsr<K, None, W> z(S, x.size());
  float tm = 0, tp = 0, tl = 0, ta = 0;
  float t  = measureDurationMarked([&](auto mark) {
    double E  = o.tolerance;
    auto   fc = [&](double el, int l) { return el<=E; };
    fillValueOmpU(vcom, K());
    fillValueOmpU(vtot, W());
    fillValueOmpU(ctot, W());
    fillValueOmpU(a, K());
    cv.respan(S);
    y .respan(S);
    z .respan(S);
    mark([&]() {
      tm += measureDuration([&]() { fm(vaff); });
      naff = sumValuesOmp(vaff, size_t());
      auto t0 = timeNow(), t1 = t0;
      louvainVertexWeightsOmpW(vtot, x);
      if (q) louvainInitializeFromOmpW(vcom, ctot, x, vtot, *q);
      else   louvainInitializeOmpW(vcom, ctot, x, vtot);
      for (l=0, p=0; M>0 && P>0;) {
        if (p==1) t1 = timeNow();
        bool isFirst = p==0;
        int m = 0;
        tl += measureDuration([&]() {
          if (isFirst) m = louvainMoveOmpW(vcom, ctot, vaff, vcs, vcout, x, vtot, M, R, L, fc);
          else         m = louvainMoveOmpW(vcom, ctot, vaff, vcs, vcout, y, vtot, M, R, L, fc);
        });
        l += max(m, 1); ++p;
        if (m<=1 || p>=P) break;
        size_t GN = isFirst? x.order() : y.order();
        size_t GS = isFirst? x.span()  : y.span();
        size_t CN = 0;
        if (isFirst) CN = louvainCommunityExistsOmpW(cv.degrees, x, vcom);
        else         CN = louvainCommunityExistsOmpW(cv.degrees, y, vcom);
        if (double(CN)/GN >= o.aggregationTolerance) break;
        if (isFirst) louvainRenumberCommunitiesOmpW(vcom, cv.degrees, bufk, x);
        else         louvainRenumberCommunitiesOmpW(vcom, cv.degrees, bufk, y);
        if (isFirst) copyValuesOmpW(a, vcom);
        else         louvainLookupCommunitiesOmpU(a, vcom);
        cv.respan(CN); z.respan(CN);
        if (isFirst) louvainCommunityVerticesOmpW(cv.offsets, cv.degrees, cv.edgeKeys, bufk, x, vcom);
        else         louvainCommunityVerticesOmpW(cv.offsets, cv.degrees, cv.edgeKeys, bufk, y, vcom);
        ta += measureDuration([&]() {
          if (isFirst) louvainAggregateOmpW(z.offsets, z.degrees, z.edgeKeys, z.edgeValues, bufs, vcs, vcout, x, vcom, cv.offsets, cv.edgeKeys);
          else         louvainAggregateOmpW(z.offsets, z.degrees, z.edgeKeys, z.edgeValues, bufs, vcs, vcout, y, vcom, cv.offsets, cv.edgeKeys);
        });
        swap(y, z);
        fillValueOmpU(vcom, K());
        fillValueOmpU(vtot, W());
        fillValueOmpU(ctot, W());
        fillValueOmpU(vaff, B(1));
        louvainVertexWeightsOmpW(vtot, y);
        louvainInitializeOmpW(vcom, ctot, y, vtot);
        E /= o.toleranceDecline;
      }
      if (p<=1) copyValuesOmpW(a, vcom);
      else      louvainLookupCommunitiesOmpU(a, vcom);
      if (p<=1) t1 = timeNow();
      tp += duration(t0, t1);
    });
  }, o.repeat);
  louvainFreeHashtablesW(vcs, vcout);
  return LouvainResult<K>(a, l, p, t, tm/o.repeat, tp/o.repeat, tl/o.repeat, ta/o.repeat, naff);
}
#endif




// LOUVAIN-STATIC
// --------------

template <class FLAG=char, class G, class K>
inline auto louvainStaticSeq(const G& x, const vector<K>* q=nullptr, const LouvainOptions& o={}) {
  auto fm = [](auto& vertices) { fillValueU(vertices, FLAG(1)); };
  return louvainSeq<FLAG>(x, q, o, fm);
}

#ifdef OPENMP
template <class FLAG=char, class G, class K>
inline auto louvainStaticOmp(const G& x, const vector<K>* q=nullptr, const LouvainOptions& o={}) {
  auto fm = [](auto& vertices) { fillValueOmpU(vertices, FLAG(1)); };
  return louvainOmp<FLAG>(x, q, o, fm);
}
#endif
