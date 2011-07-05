#include "line_optimizer.h"

#include <limits>
#include <algorithm>

#include "sparse_vector.h"
#include "scorer.h"

using namespace std;

double LineOptimizer::LineOptimize(
    const vector<ErrorSurface>& surfaces,
    const LineOptimizer::ScoreType type,
    ScoreP best_score_stats,
    float* best_score,
    const double epsilon,
    const ScoreP outside_stats /*=NULL*/) {

  // TODO: Debugging logger
  bool DEBUG = true;

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
  double last_boundary = all_ints.front()->x;
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
  float first_boundary = neg_inf;
  double pos = numeric_limits<double>::quiet_NaN();
  if(DEBUG) cerr << "LineOptimizer: INIT POS " << pos << endl;

  vector<ErrorIter>::iterator next_i = all_ints.begin();
  for (vector<ErrorIter>::iterator i = all_ints.begin();
       i != all_ints.end(); ++i) {

    const ErrorSegment& seg = **i;
    assert(seg.delta);
    acc->PlusEquals(*seg.delta);

    // merge stats that occur at the same boundary vertex
    // if the next segment has the same position along this line, skip this segment (after updating acc)
    ++next_i;
    if(DEBUG) cerr << "LineOptimizer: VERTEX: " << seg.x << endl;
    if (next_i != all_ints.end()) {
      const ErrorSegment& next_seg = **next_i;
      if (seg.x == next_seg.x) {
	if(DEBUG) cerr << "LineOptimizer: SKIP DUE TO MERGE: " << seg.x << endl;
	continue;
      } else {
	if(DEBUG) cerr << "LineOptimizer: NO MERGE: " << seg.x << endl;
      }
    }    

    if (first_boundary == neg_inf && !isnan(pos) && !isinf(pos)) {
      if(DEBUG) cerr << "LineOptimizer: FOUND FIRST (NON-INF) BOUNDARY: " << seg.x << endl;
      first_boundary = seg.x;
    }

    // don't waste time examining extremely small changes in the weights
    // unless this is our first time through this loop (i.e. score is worst_score)
    // or unless we were just at the left edge
    if(last_boundary == neg_inf || cur_best_score == worst_score || seg.x - last_boundary > epsilon) {

      float sco = acc->ComputeScore();
      if ((type == MAXIMIZE_SCORE && sco > cur_best_score) ||
          (type == MINIMIZE_SCORE && sco < cur_best_score) ) {
        cur_best_score = sco;
	assert(cur_best_score >= 0.0);
	assert(cur_best_score <= 100.0);

	// CHRIS: does left_edge mean the same thing as -inf?
	// assuming so for now...
	if (seg.x == neg_inf) {
	  pos = neg_inf;
	  if(DEBUG) cerr << "LineOptimizer: NEW BEST: LEFT EDGE POS: " << pos << endl;
	  assert(!isnan(pos));
	} else if (last_boundary == neg_inf) {
	  pos = seg.x - 0.1;
	  if(DEBUG) cerr << "LineOptimizer: NEW BEST: NEAR LEFT EDGE POS: " << pos << endl;
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
    // cerr << "x-boundary=" << seg.x << "\n";
  }

  float sco = acc->ComputeScore();
  if ((type == MAXIMIZE_SCORE && sco > cur_best_score) ||
      (type == MINIMIZE_SCORE && sco < cur_best_score) ) {
    cur_best_score = sco;
    assert(cur_best_score >= 0.0);
    assert(cur_best_score <= 100.0);

    if (left_edge) {
      // CHRIS: when would this ever be true?
      // shouldn't score always be better than the Float.MIN_VALUE?
      // or does this indicate there were no points in the error surface?
      pos = 0;
    } else {
      pos = last_boundary + 1000.0;
      assert(!isinf(pos));
      assert(!isnan(pos));
    }
    if(DEBUG) cerr << "LineOptimizer: FIN POS " << pos << " " << sco << endl;
  } else {
    assert(cur_best_score != worst_score);
  }
  assert(cur_best_score >= 0.0);
  assert(cur_best_score <= 100.0);

  // -inf indicates that the best score is just to the left of the first boundary
  if(pos == neg_inf) {
    // TODO: How will this interact with UpdateStat?
    if(first_boundary == neg_inf) {
      // this should only happen in pathological cases where we're only dealing
      // with around 1 sentence having very little variety, but returning -inf
      // could cause Terrible Things to happen in the decoder, so just disable the feature
      pos = 0.0;
    } else {
      pos = first_boundary - 1000.0;
    }
    if(DEBUG) cerr << "LineOptimizer: CORRECTED -INF BOUNDARY TO: " << pos << endl;
    assert(!isnan(pos));
  }

  best_score_stats->Set(*accp);
  *best_score = cur_best_score;
  assert(!isnan(pos));
  assert(!isinf(pos)); // -inf is not acceptable even as left edge
  return pos;
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
