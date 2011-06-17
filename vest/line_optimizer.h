#ifndef LINE_OPTIMIZER_H_
#define LINE_OPTIMIZER_H_

#include <vector>

#include "sparse_vector.h"
#include "error_surface.h"
#include "sampler.h"

class Weights;

// sort by increasing x-ints
struct ErrorIntervalComp {
  bool operator() (const ErrorIter& a, const ErrorIter& b) const {
    return a->x < b->x;
  }
};

// sort by increasing x-ints
struct ErrorSegmentComp {
  bool operator() (const ErrorSegment& a, const ErrorSegment& b) const {
    return a.x < b.x;
  }
};

struct LineOptimizer {

  // use MINIMIZE_SCORE for things like TER, WER
  // MAXIMIZE_SCORE for things like BLEU
  enum ScoreType { MAXIMIZE_SCORE, MINIMIZE_SCORE };

  // merge all the error surfaces together into a global
  // error surface and find (the middle of) the best segment
  static double LineOptimize(
     const std::vector<ErrorSurface>& envs,
     const LineOptimizer::ScoreType type,
     ScoreP best_score_stats,
     float* best_score,
     const double epsilon = 1.0/65536.0,
     const ScoreP outside_stats = NULL);

  // return a random vector of length 1 where all dimensions
  // not listed in dimensions will be 0.
  static void RandomUnitVector(const std::vector<int>& dimensions,
                               SparseVector<double>* axis,
                               RandomNumberGenerator<boost::mt19937>* rng);

  // generate a list of directions to optimize; the list will
  // contain the orthogonal vectors corresponding to the dimensions in
  // primary and then additional_random_directions directions in those
  // dimensions as well.  All vectors will be length 1.
  static void CreateOptimizationDirections(
     const std::vector<int>& primary,
     int additional_random_directions,
     RandomNumberGenerator<boost::mt19937>* rng,
     std::vector<SparseVector<double> >* dirs,
     bool include_primary=true
    );

  static LineOptimizer::ScoreType GetOptType(::ScoreType type) {
    ScoreType opt_type = LineOptimizer::MAXIMIZE_SCORE;
    if (type == TER || type == AER) {
      opt_type = LineOptimizer::MINIMIZE_SCORE;
    }
    return opt_type;
  }
};

#endif
