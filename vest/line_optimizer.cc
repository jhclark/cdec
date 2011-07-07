#include "line_optimizer.h"

#include <limits>
#include <algorithm>

#include "sparse_vector.h"
#include "scorer.h"
#include "exception.h"

using namespace std;

double LineOptimizer::LineOptimize(
    const vector<ErrorSurface>& surfaces,
    const LineOptimizer::ScoreType type,
    ScoreP best_score_stats,
    float* best_score,
    const double epsilon,
    const ScoreP outside_stats /*=NULL*/) {

  // TODO: Debugging logger
  bool DEBUG = false;

  // cerr << "MIN=" << MINIMIZE_SCORE << " MAX=" << MAXIMIZE_SCORE << "  MINE=" << type << endl;
  // concatenate error surfaces
  // CHRIS: Using ErrorIter as pointer to ErrorSegment's so that we don't have to copy? ErrorSegments aren't that big though
  vector<ErrorIter> all_ints;
  for (vector<ErrorSurface>::const_iterator i = surfaces.begin();
       i != surfaces.end(); ++i) {
    const ErrorSurface& surface = *i;
    for (ErrorIter j = surface.begin(); j != surface.end(); ++j)
      all_ints.push_back(j);
  }
  
  // sort by point on (weight) line where each ErrorSegment induces a change in the error rate
  sort(all_ints.begin(), all_ints.end(), ErrorIntervalComp());
  double last_boundary = all_ints.front()->x; // this should be -inf
  ScoreP accp = all_ints.front()->delta->GetZero();
  Score *acc=accp.get();

  // if user provided some "outside" sufficient stats for a portion of the tuning set that
  // we are not currently optimizing, apply that
  if(outside_stats != NULL) {
    acc->PlusEquals(*outside_stats);
  }

  float worst_score = (type == MAXIMIZE_SCORE ?
    -numeric_limits<float>::max() : numeric_limits<float>::max());
  float neg_inf = -numeric_limits<float>::infinity();
  float cur_best_score = worst_score;
  bool left_edge = true;
  double pos = 0.0; // default to disabling this feature, unless we find that it improves the score
  if(DEBUG) cerr << "LineOptimizer: INIT POS " << pos << endl;

  // how far do we step off into infinity if we are at one of the far edges of this error surface?
  // NOTE: Was originally 0.1 for left edge and 1000 for right edge
  const float EXPLORE_DIST = 1.0;

  try {
    vector<ErrorIter>::iterator next_i = all_ints.begin();
    for (vector<ErrorIter>::iterator i = all_ints.begin();
	 i != all_ints.end(); ++i) {

      const ErrorSegment& seg = **i;
      if(DEBUG) cerr << "LineOptimizer: VERTEX: " << seg.x << endl;
      assert(seg.delta);
      
      // merge stats that occur at the same boundary vertex
      // if the next segment has the same position along this line, skip this segment (after updating acc)
      ++next_i;
      if (next_i != all_ints.end() && seg.x == (*next_i)->x) {
	if(DEBUG) cerr << "LineOptimizer: SKIP DUE TO MERGE: " << seg.x << endl;
	
      } else if(seg.x == neg_inf) {
	// we haven't actually walked onto a line segment yet. we only know about a single point,
	// so don't attempt to assign an optimal position on this line yet --
	// we'll do that the next time through this loop
	;

      // don't waste time examining extremely small changes in the weights
      // unless this is our first time through this loop (i.e. score is worst_score)
      // or unless we were just at the left edge
      } else if(last_boundary == neg_inf || cur_best_score == worst_score || seg.x - last_boundary > epsilon) {

	float sco = acc->ComputeScore();
	if ((type == MAXIMIZE_SCORE && sco > cur_best_score) ||
	    (type == MINIMIZE_SCORE && sco < cur_best_score) ) {
	  cur_best_score = sco;
	  if (best_score_stats.get() != NULL) {
	    best_score_stats->Set(*accp);
	  }
	  assert(cur_best_score >= 0.0);
	  assert(cur_best_score <= 100.0);
	  
	  if (left_edge) {
	    // seg.x is our first_boundary
	    pos = seg.x - EXPLORE_DIST;
	    if(DEBUG) cerr << "LineOptimizer: NEW BEST: FIRST NON -INF EDGE (LEFT EDGE) POS: " << pos << endl;
	    assert(!isnan(pos));
	    assert(!isinf(pos));
	    
	    left_edge = false;
	  } else {
	    // subtracting -inf (possible value of last_boundary) will result in NaN
	    // but we guarded for this above
	    pos = last_boundary + (seg.x - last_boundary) / 2;
	    if(DEBUG) cerr << "LineOptimizer: NEW BEST: NON LEFT EDGE POS: " << pos << " " << last_boundary << " " << seg.x << endl;
	    assert(!isnan(pos));
	    assert(!isinf(pos));
	  }
	  if(DEBUG) cerr << "LineOptimizer: (NEW BEST): UPD POS: NEW BEST: " << pos << "  (score=" << cur_best_score << ")\n";
	}
	// string xx; acc->ScoreDetails(&xx); cerr << "---- " << xx;
	// cerr << "---- s=" << sco << "\n";
	// keep track of the last boundary we actually evaluated
	last_boundary = seg.x;
      } else {
	if(DEBUG) cerr << "LineOptimizer: SKIPPED DUE TO LAST_BOUNDARY < EPSILON: " << seg.x << endl;
      }
      acc->PlusEquals(*seg.delta);
      // cerr << "x-boundary=" << seg.x << "\n";
    }

    float sco = acc->ComputeScore();
    if ((type == MAXIMIZE_SCORE && sco > cur_best_score) ||
	(type == MINIMIZE_SCORE && sco < cur_best_score) ) {
      cur_best_score = sco;
      if (best_score_stats.get() != NULL) {
	best_score_stats->Set(*accp);
      }
      assert(cur_best_score >= 0.0);
      assert(cur_best_score <= 100.0);

      // if we're still at the left_edge, we never found
      // a second point to create our first line segment
      // in this case, we stick with pos's default value of 0.0, which disables the feature
      if (!left_edge) {
	pos = last_boundary + EXPLORE_DIST;
	assert(!isinf(pos));
	assert(!isnan(pos));
      }
      if(DEBUG) cerr << "LineOptimizer: FIN POS " << pos << " " << sco << endl;
    }
    assert(cur_best_score >= 0.0);
    assert(cur_best_score <= 100.0);

    *best_score = cur_best_score;
    assert(!isnan(pos));
    assert(!isinf(pos)); // -inf is not acceptable even as left edge
    return pos;
  } catch(IllegalStateException& e) {
    accp = all_ints.front()->delta->GetZero();
    e << "\n LineOptimize(): IllegalState while running LineOptimize on " << all_ints.size() << " error vertices: {";
    for(unsigned i=0; i<all_ints.size(); ++i) {
      const ErrorSegment& seg = *all_ints.at(i);
      e << seg.x << " -> ";
      if(seg.delta.get() != NULL) {
	e << *seg.delta;
	accp->PlusEquals(*seg.delta);
	e << " ((acc=" << *accp << "))";
      }
      e << ", ";
    }
    e << "}; outside_stats = ";
    if(outside_stats != NULL) {
      e << *outside_stats;
    }
    throw;
  }
}

void LineOptimizer::RandomUnitVector(const vector<int>& features_to_optimize,
                                     SparseVector<double>* axis,
                                     RandomNumberGenerator<boost::mt19937>* rng) {
  axis->clear();
  for (int i = 0; i < features_to_optimize.size(); ++i)
    axis->set_value(features_to_optimize[i], rng->next() - 0.5);
  (*axis) /= axis->l2norm();
}

void LineOptimizer::CreateOptimizationDirections(
     const vector<int>& features_to_optimize,
     int additional_random_directions,
     RandomNumberGenerator<boost::mt19937>* rng,
     vector<SparseVector<double> >* dirs
     , bool include_orthogonal
  ) {
  dirs->clear();
  typedef SparseVector<double> Dir;
  vector<Dir> &out=*dirs;
  int i=0;
  if (include_orthogonal)
    for (;i<features_to_optimize.size();++i) {
      Dir d;
      d.set_value(features_to_optimize[i],1.);
      out.push_back(d);
    }
  out.resize(i+additional_random_directions);
  for (;i<out.size();++i)
     RandomUnitVector(features_to_optimize, &out[i], rng);
  cerr << "Generated " << out.size() << " total axes to optimize along.\n";
}
