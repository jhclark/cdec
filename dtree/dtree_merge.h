#ifndef DTREE_MERGE_H_
#define DTREE_MERGE_H_

#include "dtree.h"

typedef shared_ptr<Clustering> ClusteringP;

// dammit C++: typedef shared_ptr<Beam<T>> BeamP<T>;
template<class U> struct BeamP {
  typedef shared_ptr<Beam<U> > T;
};
// usage: BeamP<ClusteringP>::T beam;

// takes a heavily split "decision tree" and performs
// agglomerative clustering
class DTreeMergeOptimizer : protected DTreeOptBase {
 public:
  DTreeMergeOptimizer(const LineOptimizer::ScoreType opt_type,
		      const double line_epsilon,
		      const float epsilon_loss,
		      const vector<SparseVector<double> >& dirs,
		      const size_t beam_size) // how many clusterings should we keep around?
    : DTreeOptBase(dirs, opt_type, line_epsilon, 0), // min_sents_per_node = 0 here
      beam_size_(beam_size),
      epsilon_loss_(epsilon_loss) {}

  void MergeNode(const SparseVector<double>& origin,
		 const vector<DTSent>& src_sents,
		 const vector<bool>& active_sents, // usually a vector of size src_sents.size, all true
		 const vector<DirErrorSurface>& sent_surfs,
		 const unsigned num_clusters,
		 DTNode& node) {
    
    size_t cur_clusters = node.question_->Size();

    ClusteringP init(new Clustering(origin, dirs_));
    Partition(*node.question_, src_sents, active_sents,
	      &init->counts_, &init->active_sents_by_branch_);

    // Accumulate DES's for each branch, based on how the sentences have been parititioned
    assert(node.question_ != NULL);
    init->surfs_.resize(cur_clusters);
    for(unsigned iBranch=0; iBranch < cur_clusters; ++iBranch) {
      DirErrorSurface& dsurf = init->surfs_.at(iBranch);
      dsurf.resize(dirs_.size());
      for(unsigned iDir=0; iDir < dirs_.size(); ++iDir) {
	ErrorSurface& surf = dsurf.AtDir(iDir);
	for(unsigned iSent=0; iSent < active_sents.size(); ++iSent) {
	  if(init->active_sents_by_branch_.at(iBranch).at(iSent)) {
	    const ErrorSurface& sent_surf = sent_surfs.at(iSent).AtDir(iDir);
	    surf.insert(surf.end(), sent_surf.begin(), sent_surf.end());
	  }
	}
	sort(surf.begin(), surf.end(), ErrorSegmentComp());
      }
    }

    // presize caches
    init->cache_.fronteir_cache_.resize(cur_clusters);
    init->cache_.best_partner_cache_.resize(cur_clusters);

    // first, find out where we're starting on the error surface
    // we'll usually be starting at the origin, so dir doesn't matter and step will be 0.0
    int iMatchDir;
    double step;
    SolveForDirectionAndStep(node.weights_, origin, &iMatchDir, &step);

    // get the sufficient statistics at that position on the error surface
    vector<bool> active_clusters(cur_clusters);
    active_clusters.resize(cur_clusters);
    for(unsigned i=0; i<cur_clusters; ++i) {
      active_clusters.at(i) = true;
    }
    init->stats_.resize(src_sents.size());
    UpdateStats(iMatchDir, step, init->surfs_, active_clusters, &init->stats_);

    // now that we know where we stand on the error surface,
    // get the current score of this clustering before optimization
    const float before_score = ScoreStats(init->stats_);
    cerr << "Score before optimization: " << before_score << endl;

    float opt_score;
    size_t opt_dir; // unused
    double opt_update; // unused
    size_t err_verts, dir_err_verts;
    // TODO: Is init.stats_ actually valid as parent stats here?

    OptimizeNode(active_sents, init->surfs_, init->stats_, 0.0, 0, 0.0,
		 &opt_score, &opt_dir, &opt_update, &dir_err_verts, &err_verts);
    cerr << "Projected score after optimizing single node: " << opt_score
	 << " (" << dir_err_verts << " error vertices in best direction, " << err_verts << " total)" << endl;

    // Now optimize all of the splits individually
    init->best_dir_.resize(cur_clusters);
    init->best_step_.resize(cur_clusters);
    OptimizeQuestion(*node.question_, src_sents, active_sents, init->stats_, init->surfs_,
		     opt_score, opt_dir, opt_update,
		     &init->score_, &init->best_dir_, &init->best_step_, &init->stats_);


    // cache sum of stats
    init->all_stats_ = init->stats_.front()->GetZero();
    for(size_t i=0; i<cur_clusters; ++i) {
      init->all_stats_->PlusEquals(*init->stats_.at(i));
    }

    BeamP<ClusteringP>::T prev_beam(new Beam<ClusteringP>(1));
    assert(prev_beam->Size() == 0);
    prev_beam->Add(init);
    assert(prev_beam->Size() == 1);

    cout << "Init:" << endl;
    cout << *init << endl;

    cerr << "k=" << cur_clusters << ": Score=" << init->score_ << endl;

    // k is our target number of clusters for this iteration 
    for(size_t k = cur_clusters - 1; k >= num_clusters; --k) {
      // keep a beam of the best merges
      BeamP<ClusteringP>::T beam(new Beam<ClusteringP>(beam_size_));

      // iterate over previous beam entries
      bool term_early = false;
      for(unsigned iPrevBeam=0; iPrevBeam < prev_beam->Size() && !term_early; ++iPrevBeam) {
	Clustering& prev_clust = *prev_beam->At(iPrevBeam);	 // non-const due to internal cache

	// iterate over possible pairs of existing "clusters" within this
	// previous n-best clustering
	size_t num_pairs = prev_clust.Size() * (prev_clust.Size() - 1) / 2;
	cerr << "Trying all " << num_pairs << " pairs for k=" << k << ", " << (iPrevBeam+1) << "th best" << endl;
	size_t which_pair = 0;
	for(size_t i=0; i<prev_clust.Size() && !term_early; ++i) {

	  // check if we have a cached partner for this cluster or if we must search for it
	  // we only apply this (slightly) approximate cache when there are many pairs left to be searched
	  unsigned approx_threshold = 1000;
	  size_t fronteir_j = prev_clust.cache_.fronteir_cache_.at(i);
	  if(num_pairs > approx_threshold && fronteir_j == prev_clust.Size()) {
	    // we know (approximately) the best partner
	    // NOTE: This is an approximation since the best partner can change due to things like length effects

	    size_t cached_j = prev_clust.cache_.best_partner_cache_.at(i);
	    cerr << "Cache hit for " << i << ": " << cached_j << endl;
	    // just recover the Clustering for this partner and add it to the beam
	    Merge(prev_clust, i, cached_j, *beam);

	  } else {

	    float best_partner_score = 0.0;
	    size_t best_partner = 0;
	    size_t start = i+1;
	    if(fronteir_j > 0) {
	      // pick up wherever we left off searching (often, we pick up at the beginning)
	      best_partner = prev_clust.cache_.best_partner_cache_.at(i);
	      cerr << "Resuming for " << i << " at " << fronteir_j << " with best index: " << best_partner << endl;
	      // just recover the Clustering for this partner and add it to the beam
	      best_partner_score = Merge(prev_clust, i, best_partner, *beam);

	      start = fronteir_j;
	    } else {
	      cerr << "Fronteir is " << fronteir_j << endl;
	    }
	    
	    size_t j;
	    for(j=start; j<prev_clust.Size() && !term_early; ++j) {
	      ++which_pair; // TODO: Fix this for when we resume
	      
	      assert(prev_clust.stats_.at(i) != NULL);
	      assert(prev_clust.stats_.at(j) != NULL);
	      
	      // TODO: Producer-consumer threading
	      
	      // modify the previous clustering by merging clusters i and j
	      float merge_score = Merge(prev_clust, i, j, *beam);
	      if(merge_score > best_partner_score) {
		best_partner_score = merge_score;
		best_partner = j;
	      }
	      
	      if(which_pair % 1000 == 0) {
		float pct = (float) which_pair / (float) num_pairs * 100.0;
		cerr << "Progress: " << which_pair << "/" << num_pairs << "(" << pct << "%)" << endl;
	      }
	      // pessimistic / optimistic loss
	      float opt_loss = prev_beam->Best()->score_ - beam->Best()->score_;
	      float pess_loss = prev_beam->Best()->score_ - beam->Worst()->score_;
	      if(which_pair % 10000 == 0) {
		const Clustering& best = *beam->Best();
		const Clustering& worst = *beam->Worst();
		cerr << "PROGRESS: k=" << k << ": Best Score=" << best.score_
		     << "; optimistic loss = " << opt_loss
		     << "; Worst in Beam=" << worst.score_
		     << "; pessimistic loss = " << pess_loss
		     << "; best merge " << best.recent_merge1_ << " " << best.recent_merge2_ << endl;
	      }
	      
	      // pessimistic loss versus previous k
	      if(beam->Size() == beam->Capacity() && pess_loss <= epsilon_loss_) {
		cerr << "Terminating iteration k=" << k
		     << " early due to pessimistic loss being " << pess_loss
		     << " (< " << epsilon_loss_ << ")" << endl;
		term_early = true;
	      }
	    } // for partners

	    // update the cache with the best match we found and how far we got
	    prev_clust.cache_.fronteir_cache_.at(i) = j-1;
	    prev_clust.cache_.best_partner_cache_.at(i) = best_partner;

	  } // check for cache hit
	}

	// update search cache of entries in current resulting clusters
	for(size_t iBeam=0; iBeam < beam->Size(); ++iBeam) {
	  cerr << "Storing cache... best_partner for 0 is " << prev_clust.cache_.best_partner_cache_.at(0) << "; fronteir is " << prev_clust.cache_.best_partner_cache_.at(0) << endl;

	  Clustering& clust = *beam->At(iBeam);
	  clust.cache_ = prev_clust.cache_;
	  clust.cache_.Merge(clust.recent_merge1_, clust.recent_merge2_);
	  cerr << "Stored cache... best_partner for 0 is " << clust.cache_.best_partner_cache_.at(0) << "; fronteir is " << clust.cache_.best_partner_cache_.at(0) << endl;
	}
	
      } // iPrevBeam

      // print best result for this k
      for(unsigned i=0; i<beam->Size(); ++i) {
	const Clustering& ith = *beam->At(i);
	cerr << "Finished for k=" << k << endl;

	cout << (i+1) << "-best:" << endl;
	cout << ith << endl;

	cerr << "k=" << k << ": " << (i+1) << "-best Score=" << ith.score_ << "; merged " << ith.recent_merge1_ << " " << ith.recent_merge2_ << endl;
      }

      prev_beam = beam;
    } // k
  }

 private:
  // we need the beam to determine if we need to create a new Clustering object
  // -- an expensive operation
  // returns the score of the best solution for this merge (used for caching purposes)
  float Merge(Clustering& prev_clust, // not const due to internal cache
	     const size_t iSrc1,
	     const size_t iSrc2,
	     Beam<ClusteringP>& beam) {

    // accumulate metric stats for sentences outside this DTNode
    ScoreP outside_stats = prev_clust.stats_.front()->GetZero();
    outside_stats->PlusEquals(*prev_clust.all_stats_);
    if(!outside_stats->HasValidStats()) UTIL_THROW(IllegalStateException, "Invalid prev_clust.all_stats_: " << *prev_clust.all_stats_);
    // subtract off the stats for the clusters we're about to optimize
    outside_stats->PlusEquals(*prev_clust.stats_.at(iSrc1), -1);
    outside_stats->PlusEquals(*prev_clust.stats_.at(iSrc2), -1);
    if(!outside_stats->HasValidStats()) {
      UTIL_THROW(IllegalStateException, "Merge(): Invalid outside_stats: " << *outside_stats
		 << "; from prev_clust.all_stats_: " << *prev_clust.all_stats_
		 << "; after subtracting iSrc1=" << iSrc1 << " (" << *prev_clust.stats_.at(iSrc1) << ")"
		 << " and iSrc2=" << iSrc2 << " (" << *prev_clust.stats_.at(iSrc2) << ")");
    }

    float merge_score = 0.0;
    size_t err_verts = 0;
    size_t dir_err_verts = 0;
    size_t iTgt = -1;
    ClusteringP clust; // create only if we will add it to the beam
    for(size_t dir_id = 0; dir_id < dirs_.size(); ++dir_id) {

      const ErrorSurface& e1 = prev_clust.surfs_.at(iSrc1).AtDir(dir_id);
      const ErrorSurface& e2 = prev_clust.surfs_.at(iSrc2).AtDir(dir_id);
      vector<ErrorSurface> esv(2);
      esv.push_back(e1);
      esv.push_back(e2);
      const size_t points = e1.size() + e2.size();

      if(DEBUG) cerr << "dtree_merge: Running line optimization on " << points << " error vertices in direction " << dir_id << "... " << endl;
      if(DEBUG) cerr << "dtree_merge: outside_stats are " << *outside_stats << endl;
      float score;
      ScoreP stats_result = prev_clust.stats_.front()->GetZero();
      double x;
      try {
	x = LineOptimizer::LineOptimize(esv, opt_type_, stats_result, &score,
					     line_epsilon_, outside_stats);
      } catch(IllegalStateException& e) {
	e << "\n Merge(): Illegal State while running line optimizer in direction " << dir_id << " on " << points << " error vertices";
	throw;
      }

      score *= 100;
      assert(score >= 0.0);
      assert(score <= 100.0);

      merge_score = std::max(score, merge_score);

      if(clust.get() == NULL && beam.WillAccept(score)) {
	clust.reset(new Clustering(prev_clust));
	clust->score_ = 0.0;
	iTgt = clust->Merge(iSrc1, iSrc2);
      }

      if(clust.get() != NULL && score > clust->score_) {
	// TODO: Print information about how well we did with this direction...
	// TODO: Generalize to best() operator for TER
	// update our current entry, already in the beam
	clust->score_ = score;
	clust->best_dir_.at(iTgt) = dir_id;
	clust->best_step_.at(iTgt) = x;

	// get exactly the stats used for this merge (the result minus the outside stats)
	clust->stats_.at(iTgt) = stats_result->GetZero();
	clust->stats_.at(iTgt)->PlusEquals(*stats_result);
	clust->stats_.at(iTgt)->PlusEquals(*outside_stats, -1);

	dir_err_verts = points;
      }
      err_verts += points;
    }
    if(clust.get() != NULL && beam.WillAccept(clust->score_)) {
      beam.Add(clust);
    }
    if(DEBUG) {
      if(clust != NULL) {
	cerr << "dtree_merge: Searched " << err_verts << " error vertices overall; " << dir_err_verts << " err vertices in best direction" << endl;
      } else {
	cerr << "dtree_merge: Searched " << err_verts << " error vertices overall; No entries added to beam" << endl;
      }
    }
    return merge_score;
  }

 private:
  const size_t beam_size_;
  const float epsilon_loss_; // in metric%
};

#endif
