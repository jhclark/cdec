#ifndef CLUSTER_H_
#define CLUSTER_H_

#include <vector>
using namespace std;

#include "error_surface.h"

class DirErrorSurface : public vector<ErrorSurface> {
 public:
  DirErrorSurface() {}
  DirErrorSurface(size_t sz) : vector<ErrorSurface>(sz) {  
   resize(sz);
  }

  ErrorSurface& AtDir(const size_t i) {
    return at(i);
  }

  const ErrorSurface& AtDir(const size_t i) const {
    return at(i);
  }

  void Append(const DirErrorSurface& that) {
    assert(size() == that.size());
    for(size_t iDir=0; iDir < size(); ++iDir) {
      AtDir(iDir).insert(AtDir(iDir).end(), that.AtDir(iDir).begin(), that.AtDir(iDir).end());
    }
  }
};

class Clustering {
 public:
  bool operator<(const Clustering& other) const {
    return score_ < other.score_;
  }

  unsigned Merge(const unsigned i,
		 const unsigned j) {

    // TODO: Avoid erasing to make this more efficient?
    counts_.at(i) += counts_.at(j);
    //    counts_.erase(counts_.begin() + j);

    stats_.at(i)->PlusEquals(*stats_.at(j));
    //    stats_.erase(stats_.begin() + j);
      
    assert(stats_.at(i).get() != NULL);

    DirErrorSurface& surf1 = surfs_.at(i);
    const DirErrorSurface& surf2 = surfs_.at(j);
    surf1.Append(surf2);
    //surfs_.erase(surfs_.begin() + j);

    vector<bool>& clust_i = active_sents_by_branch_.at(i);
    const vector<bool>& clust_j = active_sents_by_branch_.at(j);
    for(size_t k=0; k<clust_i.size(); ++k) {
      clust_i.at(k) = clust_i.at(k) || clust_j.at(k);
    }
    //active_sents_by_branch_.erase(active_sents_by_branch_.begin() + j);

    assert(stats_.at(i).get() != NULL);

    // best_dir and best_step are chosen randomly (by the first one)
    // until an optimization algorithm picks a correct value
    //best_dir_.erase(best_dir_.begin() + j);
    //best_step_.erase(best_step_.begin() + j);

    // i is the index of the merged cluster
    return i;
  }

  size_t Size() const {
    return active_sents_by_branch_.size();
  }

 public:
  float score_;
  // the current sufficient stats for all clusters combined
  // this may be approximate while optimization is still in progress
  // this state helps agglomerative clustering be more efficient (less PlusEquals operations)
  ScoreP all_stats_;

  // all indexed by branch:
  vector<DirErrorSurface> surfs_;
  vector<unsigned> counts_;
  vector<vector<bool> > active_sents_by_branch_;
  vector<ScoreP> stats_;
  vector<unsigned> best_dir_;
  vector<double> best_step_;
};

#endif
