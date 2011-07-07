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
      ErrorSurface& this_surf = AtDir(iDir);
      const ErrorSurface& that_surf = that.AtDir(iDir);
      this_surf.insert(this_surf.end(), that_surf.begin(), that_surf.end());
      sort(this_surf.begin(), this_surf.end(), ErrorSegmentComp());
    }
  }
};

class Clustering {
 public:
  Clustering()
    : score_(0.0),
      recent_merge1_(-1),
      recent_merge2_(-1) {}

  bool operator<(const Clustering& other) const {
    return score_ < other.score_;
  }

  double GetScore() const {
    return score_;
  }

  unsigned Merge(const unsigned i,
		 const unsigned j) {

    recent_merge1_ = i;
    recent_merge2_ = j;

    // TODO: Avoid erasing to make this more efficient?
    counts_.at(i) += counts_.at(j);
    counts_.erase(counts_.begin() + j);

    // create new Score pointer to hold result of adding stats
    ScoreP tmp = stats_.at(i);
    stats_.at(i) = stats_.front()->GetZero();
    stats_.at(i)->PlusEquals(*tmp);
    stats_.at(i)->PlusEquals(*stats_.at(j));
    stats_.erase(stats_.begin() + j);
      
    assert(stats_.at(i).get() != NULL);

    DirErrorSurface& surf1 = surfs_.at(i);
    const DirErrorSurface& surf2 = surfs_.at(j);
    surf1.Append(surf2); // append includes sort
    surfs_.erase(surfs_.begin() + j);

    vector<bool>& clust_i = active_sents_by_branch_.at(i);
    const vector<bool>& clust_j = active_sents_by_branch_.at(j);
    for(size_t k=0; k<clust_i.size(); ++k) {
      clust_i.at(k) = clust_i.at(k) || clust_j.at(k);
    }
    active_sents_by_branch_.erase(active_sents_by_branch_.begin() + j);

    assert(stats_.at(i).get() != NULL);

    // best_dir and best_step are chosen randomly (by the first one)
    // until an optimization algorithm picks a correct value
    best_dir_.erase(best_dir_.begin() + j);
    best_step_.erase(best_step_.begin() + j);

    // i is the index of the merged cluster
    return i;
  }

  size_t Size() const {
    size_t sz = active_sents_by_branch_.size();
    assert(surfs_.size() == sz);
    assert(counts_.size() == sz);
    assert(stats_.size() == sz);
    assert(best_dir_.size() == sz);
    assert(best_step_.size() == sz);
    return sz;
  }

 public:
  float score_;
  size_t recent_merge1_;
  size_t recent_merge2_;
  // the current sufficient stats for all clusters combined
  // this may be approximate while optimization is still in progress
  // this state helps agglomerative clustering be more efficient (less PlusEquals operations)
  ScoreP all_stats_;

  // all indexed by branch:
  vector<DirErrorSurface> surfs_;
  vector<unsigned> counts_;
  vector<vector<bool> > active_sents_by_branch_;
  vector<ScoreP> stats_;
  vector<size_t> best_dir_;
  vector<double> best_step_;
};

inline ostream& operator<<(ostream& out, const Clustering& c) {
  for(unsigned k=0; k<c.Size(); ++k) {
    out << "K=" << c.Size() << "; #" << k << ":";

    const vector<bool>& sents = c.active_sents_by_branch_.at(k);
    const size_t dir = c.best_dir_.at(k);
    const double step = c.best_step_.at(k);
    for(unsigned i=0; i<sents.size(); ++i) {
      if(sents.at(i)) {
	out << " " << i;
      }
    }
    
    out << "; dir=" << dir << " step=" << step << endl;
  }
  return out;
}

#endif
