#ifndef DTREE_MERGE_H_
#define DTREE_MERGE_H_

#include "dtree.h"

// takes a heavily split "decision tree" and performs
// agglomerative clustering
class DTreeMergeOptimizer : protected DTreeOptBase {
 public:
  DTreeMergeOptimizer(const LineOptimizer::ScoreType opt_type,
		      const double line_epsilon,
		      const vector<SparseVector<double> >& dirs,
		      const size_t beam_size) // how many clusterings should we keep around?
    : DTreeOptBase(dirs, opt_type, line_epsilon, 0), // min_sents_per_node = 0 here
      beam_size_(beam_size) {}

  void MergeNode(const SparseVector<double>& origin,
		 const vector<DTSent>& src_sents,
		 const vector<bool>& active_sents, // usually a vector of size src_sents.size, all true
		 const vector<DirErrorSurface>& sent_surfs,
		 const unsigned num_clusters,
		 DTNode& node) {
    
    size_t cur_clusters = node.question_->Size();

    Clustering init;
    Partition(*node.question_, src_sents, active_sents,
	      &init.counts_, &init.active_sents_by_branch_);

    // Accumulate DES's for each branch, based on how the sentences have been parititioned
    assert(node.question_ != NULL);
    init.surfs_.resize(cur_clusters);
    for(unsigned iBranch=0; iBranch < cur_clusters; ++iBranch) {
      DirErrorSurface& dsurf = init.surfs_.at(iBranch);
      dsurf.resize(dirs_.size());
      for(unsigned iDir=0; iDir < dirs_.size(); ++iDir) {
	ErrorSurface& surf = dsurf.AtDir(iDir);
	for(unsigned iSent=0; iSent < active_sents.size(); ++iSent) {
	  if(init.active_sents_by_branch_.at(iBranch).at(iSent)) {
	    const ErrorSurface& sent_surf = sent_surfs.at(iSent).AtDir(iDir);
	    surf.insert(surf.end(), sent_surf.begin(), sent_surf.end());
	  }
	}
	sort(surf.begin(), surf.end(), ErrorSegmentComp());
      }
    }

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
    init.stats_.resize(src_sents.size());
    UpdateStats(iMatchDir, step, init.surfs_, active_clusters, &init.stats_);

    // now that we know where we stand on the error surface,
    // get the current score of this clustering before optimization
    const float before_score = ScoreStats(init.stats_);
    cerr << "Score before optimization: " << before_score << endl;

    float opt_score;
    size_t opt_dir; // unused
    double opt_update; // unused
    size_t err_verts, dir_err_verts;
    // TODO: Is init.stats_ actually valid as parent stats here?

    OptimizeNode(active_sents, init.surfs_, init.stats_, 0.0, 0, 0.0,
		 &opt_score, &opt_dir, &opt_update, &dir_err_verts, &err_verts);
    cerr << "Projected score after optimizing single node: " << opt_score
	 << " (" << dir_err_verts << " error vertices in best direction, " << err_verts << " total)" << endl;

    // Now optimize all of the splits individually
    init.best_dir_.resize(cur_clusters);
    init.best_step_.resize(cur_clusters);
    OptimizeQuestion(*node.question_, src_sents, active_sents, init.stats_, init.surfs_,
		     opt_score, opt_dir, opt_update,
		     &init.score_, &init.best_dir_, &init.best_step_, &init.stats_);


    // cache sum of stats
    init.all_stats_ = init.stats_.front()->GetZero();
    for(size_t i=0; i<cur_clusters; ++i) {
      init.all_stats_->PlusEquals(*init.stats_.at(i));
    }

    shared_ptr<Beam<Clustering> > prev_beam(new Beam<Clustering>(1));
    assert(prev_beam->Size() == 0);
    prev_beam->Add(init);
    assert(prev_beam->Size() == 1);

    cout << "Init:" << endl;
    cout << init << endl;

    cerr << "k=" << cur_clusters << ": Score=" << init.score_ << endl;

    // k is our target number of clusters for this iteration 
    for(size_t k = cur_clusters - 1; k >= num_clusters; --k) {
      // keep a beam of the best merges
      shared_ptr<Beam<Clustering> > beam(new Beam<Clustering>(beam_size_));

      bool term_early = false;
      for(unsigned iBeam=0; iBeam<prev_beam->Size() && !term_early; ++iBeam) {
	const Clustering& prev_clust = prev_beam->At(iBeam);	

	// iterate over possible pairs of existing "clusters" within this
	// previous n-best clustering
	size_t num_pairs = prev_clust.Size() * (prev_clust.Size() - 1) / 2;
	cerr << "Trying all " << num_pairs << " pairs for k=" << k << ", " << (iBeam+1) << "th best" << endl;
	size_t which_pair = 0;
	for(size_t i=0; i<prev_clust.Size() && !term_early; ++i) {
	  for(size_t j=i+1; j<prev_clust.Size() && !term_early; ++j) {
	    ++which_pair;

	    assert(prev_clust.stats_.at(i) != NULL);
	    assert(prev_clust.stats_.at(j) != NULL);

	    // modify the previous clustering by merging clusters i and j
	    Merge(prev_clust, i, j, *beam);

	    if(which_pair % 1000 == 0) {
	      float pct = (float) which_pair / (float) num_pairs * 100.0;
	      cerr << "Progress: " << which_pair << "/" << num_pairs << "(" << pct << "%)" << endl;
	    }
	    // pessimistic / optimistic loss
	    float opt_loss = prev_beam->Best().score_ - beam->Best().score_;
	    float pess_loss = prev_beam->Best().score_ - beam->Worst().score_;
	    if(which_pair % 10000 == 0) {
	      const Clustering& best = beam->Best();
	      const Clustering& worst = beam->Worst();
	      cerr << "PROGRESS: k=" << k << ": Best Score=" << best.score_
		   << "; optimistic loss = " << opt_loss
		   << "; Worst in Beam=" << worst.score_
		   << "; pessimistic loss = " << pess_loss
		   << "; best merge " << best.recent_merge1_ << " " << best.recent_merge2_ << endl;
	    }

	    // pessimistic loss versus previous k
	    float EPSILON_LOSS = 0.01; // in Metric%
	    if(beam->Size() == beam->Capacity() && pess_loss <= EPSILON_LOSS) {
	      cerr << "Terminating iteration k=" << k
		   << " early due to pessimistic loss being " << pess_loss
		   << " (< " << EPSILON_LOSS << ")" << endl;
	      term_early = true;
	    }
	  }
	}
      } // iBeam

      // print best result for this k
      for(unsigned i=0; i<beam->Size(); ++i) {
	const Clustering& ith = beam->At(i);
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
  void Merge(const Clustering& prev_clust,
	     const size_t iSrc1,
	     const size_t iSrc2,
	     Beam<Clustering>& beam) {

    // accumulate metric stats for sentences outside this DTNode
    ScoreP outside_stats = prev_clust.stats_.front()->GetZero();
    outside_stats->PlusEquals(*prev_clust.all_stats_);
    // subtract off the stats for the clusters we're about to optimize
    outside_stats->PlusEquals(*prev_clust.stats_.at(iSrc1), -1);
    outside_stats->PlusEquals(*prev_clust.stats_.at(iSrc2), -1);
    
    size_t err_verts = 0;
    size_t dir_err_verts = 0;
    size_t iTgt = -1;
    Clustering* clust = NULL; // create only if we will add it to the beam
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
      double x = LineOptimizer::LineOptimize(esv, opt_type_, stats_result, &score,
					     line_epsilon_, outside_stats);
      score *= 100;
      assert(score >= 0.0);
      assert(score <= 100.0);

      if(beam.WillAccept(score) && clust == NULL) {
	clust = beam.Add(score);
	assert(clust != NULL);
	*clust = prev_clust; // expensive!

	clust->all_stats_ = stats_result->GetZero();
	clust->all_stats_->PlusEquals(*stats_result); // TODO: Move this into copy constructor?
	if(DEBUG) cerr << "dtree_merge: Assigned clust->all_stats_ as " << *clust->all_stats_ << endl;

	// also move this into the copy constructor?
	

	iTgt = clust->Merge(iSrc1, iSrc2);
	clust->score_ = 0.0;
      }

      if(clust != NULL && score > clust->score_) {
	// TODO: Print information about how well we did with this direction...
	// TODO: Generalize to best() operator for TER
	// update our current entry, already in the beam
	clust->score_ = score;
	clust->best_dir_.at(iTgt) = dir_id;
	clust->best_step_.at(iTgt) = x;

	// get exactly the stats used for this merge
	clust->stats_.at(iTgt) = stats_result->GetZero();
	clust->stats_.at(iTgt)->PlusEquals(*stats_result);
	clust->stats_.at(iTgt)->PlusEquals(*outside_stats, -1);

	dir_err_verts = points;

	assert(clust->stats_.at(iTgt) != NULL);
      }
      err_verts += points;
    }
    if(DEBUG) {
      if(clust != NULL) {
	cerr << "dtree_merge: Searched " << err_verts << " error vertices overall; " << dir_err_verts << " err vertices in best direction" << endl;
      } else {
	cerr << "dtree_merge: Searched " << err_verts << " error vertices overall; No entries added to beam" << endl;
      }
    }
  }

 private:
  const size_t beam_size_;
};

#endif
