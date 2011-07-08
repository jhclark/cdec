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

class ClusteringCache {
 public:
  ClusteringCache() {}

  ClusteringCache(const ClusteringCache& that)
    : best_partner_cache_(that.best_partner_cache_),
      fronteir_cache_(that.fronteir_cache_) {}

  ClusteringCache& operator=(const ClusteringCache& that) {
    best_partner_cache_ = that.best_partner_cache_;
    fronteir_cache_ = that.fronteir_cache_;
  }
  
  void Merge(size_t i, size_t j) {
    // update cache and invalidate necessary components
    best_partner_cache_.at(i) = 0;
    fronteir_cache_.at(i) = 0; // <-- indicates we have no data
    //best_dir_cache_.at(i) = 0;
    //best_step_cache_.at(i) = 0;
    //best_stats_cache_.at(i).reset();
    
    best_partner_cache_.erase(best_partner_cache_.begin() + j);
    fronteir_cache_.erase(fronteir_cache_.begin() + j);
    //best_dir_cache_.erase(best_dir_cache_.begin() + j);
    //best_step_cache_.erase(best_step_cache_.begin() + j);
    //best_stats_cache_.erase(best_stats_cache_.begin() + j);

    // update partner locations since some indices might now be off by one
    for(size_t k=0; k<best_partner_cache_.size(); ++k) {
      if(best_partner_cache_.at(k) == j) {
	best_partner_cache_.at(k) = 0;
	fronteir_cache_.at(k) = 0;
	//best_dir_cache_.at(k) = 0;
	//best_step_cache_.at(k) = 0;
	//best_stats_cache_.at(k).reset();

      } else if(best_partner_cache_.at(k) > j) {
	--best_partner_cache_.at(k);
      }
      
      // if it was equal, just keep searching
      if(fronteir_cache_.at(k) > j) {
	--fronteir_cache_.at(k);
      }
    }
  }

  // cached best partners from considered merge possibilities of previous iterations
  vector<size_t> best_partner_cache_; // index of the best partner we know of for this branch/cluster
  vector<size_t> fronteir_cache_; // index of the next branch to continue exploring as partner (0 indicates no progress so far, Size() indicates completion)
  //vector<size_t> best_dir_cache_;
  //vector<double> best_step_cache_;
  // vector<ScoreP> best_stats_cache;_
};

class Clustering {
 public:
 Clustering(const SparseVector<double>& origin, const vector<SparseVector<double> >& dirs)
   : origin_(origin),
     dirs_(dirs),
     score_(0.0),
     recent_merge1_(-1),
     recent_merge2_(-1) {}

 Clustering(const Clustering& that)
   : origin_(that.origin_),
     dirs_(that.dirs_) {

    score_ = that.score_;
    recent_merge1_ = that.recent_merge1_;
    recent_merge2_ = that.recent_merge2_;

    // create deep copy of all_stats
    all_stats_ = that.all_stats_->GetZero();
    all_stats_->PlusEquals(*that.all_stats_);

    surfs_ = that.surfs_;

    // create shallow copy of stats_
    counts_ = that.counts_;
    active_sents_by_branch_ = that.active_sents_by_branch_;
    stats_ = that.stats_;
    best_dir_ = that.best_dir_;
    best_step_ = that.best_step_;
  }

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
  // for help formatting
  const SparseVector<double>& origin_;
  const vector<SparseVector<double> >& dirs_;

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

  // this must be merged and set *after* the iteration is complete
  ClusteringCache cache_;
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
    
    out << "; dir=" << dir << " step=" << step;

    SparseVector<double> weights = c.origin_;
    weights += c.dirs_.at(dir);
    out << "; weights: " << weights << endl;
  }
  return out;
}

#endif
