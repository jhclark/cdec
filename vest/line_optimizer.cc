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

  // cerr << "MIN=" << MINIMIZE_SCORE << " MAX=" << MAXIMIZE_SCORE << "  MINE=" << type << endl;
  // concatenate error surfaces
  // CHRIS: Why is this a vector of ErrorIter's (ErrorSegment pointers) instead of ErrorSegments? -JON
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
  float cur_best_score = worst_score;
  bool left_edge = true;
  double pos = numeric_limits<double>::quiet_NaN();

  for (vector<ErrorIter>::iterator i = all_ints.begin();
       i != all_ints.end(); ++i) {
    const ErrorSegment& seg = **i;
    assert(seg.delta);
    // don't waste time examining extremely small changes in the weights
    // provided we have found some reasonable solution already
    if (cur_best_score == worst_score || seg.x - last_boundary > epsilon) {
      float sco = acc->ComputeScore();
      if ((type == MAXIMIZE_SCORE && sco > cur_best_score) ||
          (type == MINIMIZE_SCORE && sco < cur_best_score) ) {
        cur_best_score = sco;
	assert(cur_best_score >= 0.0);
	assert(cur_best_score <= 100.0);

	if (left_edge) {
	  pos = seg.x - 0.1;
	  left_edge = false;
	} else {
	  pos = last_boundary + (seg.x - last_boundary) / 2;
	}
	// cerr << "NEW BEST: " << pos << "  (score=" << cur_best_score << ")\n";
      }
      // string xx; acc->ScoreDetails(&xx); cerr << "---- " << xx;
      // cerr << "---- s=" << sco << "\n";
      last_boundary = seg.x;
    }
    // cerr << "x-boundary=" << seg.x << "\n";
    acc->PlusEquals(*seg.delta);
    float s = acc->ComputeScore();
  }

  float sco = acc->ComputeScore();
  if ((type == MAXIMIZE_SCORE && sco > cur_best_score) ||
      (type == MINIMIZE_SCORE && sco < cur_best_score) ) {
    cur_best_score = sco;
    assert(cur_best_score >= 0.0);
    assert(cur_best_score <= 100.0);

    if (left_edge) {
      pos = 0;
    } else {
      pos = last_boundary + 1000.0;
    }
  } else {
    assert(cur_best_score != worst_score);
  }
  assert(cur_best_score >= 0.0);
  assert(cur_best_score <= 100.0);

  best_score_stats->Set(*accp);
  *best_score = cur_best_score;
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
