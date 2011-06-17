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
    : DTreeOptBase(opt_type, line_epsilon, 0), // min_sents_per_node = 0 here
      dirs_(dirs),
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
    SolveForDirectionAndStep(node.weights_, origin, dirs_, &iMatchDir, &step);

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
    cout << "Score before optimization: " << before_score << endl;

    float opt_score;
    size_t opt_dir; // unused
    double opt_update; // unused
    size_t err_verts, dir_err_verts;
    // TODO: Is init.stats_ actually valid as parent stats here?
    OptimizeNode(dirs_, active_sents, init.surfs_, init.stats_,
		 &opt_score, &opt_dir, &opt_update, &dir_err_verts, &err_verts);
    cerr << "Projected score after optimizing pre-merged node: " << opt_score
	 << " (" << dir_err_verts << " error vertices in best direction, " << err_verts << " total)" << endl;

    // cache sum of stats
    // and initialize best_dir and best_step to dummy values (what's optimal for the entire node)
    init.all_stats_ = init.stats_.front()->GetZero();
    init.best_dir_.resize(cur_clusters);
    init.best_step_.resize(cur_clusters);
    for(size_t i=0; i<cur_clusters; ++i) {
      init.all_stats_->PlusEquals(*init.stats_.at(i));
      init.best_dir_.at(i) = opt_dir;
      init.best_step_.at(i) = opt_update;
    }

    shared_ptr<Beam<Clustering> > prev_beam(new Beam<Clustering>(1));
    prev_beam->Add(init);
    cout << "k=" << cur_clusters << ": Score=" << opt_score << endl;

    // k is our target number of clusters for this iteration 
    for(size_t k = cur_clusters - 1; k >= num_clusters; ++k) {
      // keep a beam of the best merges
      shared_ptr<Beam<Clustering> > beam(new Beam<Clustering>(beam_size_));

      for(unsigned iBeam=0; iBeam<prev_beam->Size(); ++iBeam) {
	const Clustering& prev_clust = prev_beam->At(iBeam);	

	// iterate over possible pairs of existing "clusters" within this
	// previous n-best clustering
	size_t num_pairs = prev_clust.Size() * (prev_clust.Size() - 1) / 2;
	cerr << "Trying all " << num_pairs << " pairs for k=" << k << ", " << (iBeam+1) << "th best" << endl;
	for(size_t i=0; i<prev_clust.Size(); ++i) {
	  for(size_t j=i; j<prev_clust.Size(); ++j) {
	    size_t which_pair = (i+1)*(j+1);
	    if(which_pair % 10 == 0) {
	      float pct = (float) which_pair / (float) num_pairs * 100.0;
	      cerr << "Progress: " << which_pair << "/" << num_pairs << "(" << pct << "%)" << endl;
	    }

	    // modify the previous clustering by merging clusters i and j
	    Clustering clust;
	    Merge(prev_clust, i, j, &clust);
	    beam->Add(clust);
	  }
	}
      }

      // print best result for this k
      const Clustering& best = beam->Best();
      cout << "k=" << k << ": Best Score=" << best.score_ << endl;
      cout << "k=" << k << ": " << beam->Size() << "-best Score=" << beam->Worst().score_ << endl;

      prev_beam = beam;
    }
  }

 private:
  void Merge(const Clustering& prev_clust,
	     const unsigned iSrc1,
	     const unsigned iSrc2,
	     Clustering* clust) {

    /*
    // Well... this looks expensive...
    *clust = prev_clust;
    // merge the 2 source clusters indices and get the target cluster index
    const unsigned iTgt = clust->Merge(iSrc1, iSrc2);
    */

    // accumulate metric stats for sentences outside this DTNode
    //const ScoreP merged_stats = clust->stats_.at(iTgt);
    //outside_stats->PlusEquals(*merged_stats, -1);
    ScoreP outside_stats = prev_clust.stats_.front()->GetZero();
    outside_stats->PlusEquals(*prev_clust.all_stats_);
    // subtract off the stats for the clusters we're about to optimize
    outside_stats->PlusEquals(*prev_clust.stats_.at(iSrc1), -1);
    outside_stats->PlusEquals(*prev_clust.stats_.at(iSrc2), -1);
    
    size_t err_verts = 0;
    size_t dir_err_verts = 0;

    clust->score_ = 0.0;
    for(size_t dir_id = 0; dir_id < dirs_.size(); ++dir_id) {

      const ErrorSurface& e1 = prev_clust.surfs_.at(iSrc1).AtDir(dir_id);
      const ErrorSurface& e2 = prev_clust.surfs_.at(iSrc2).AtDir(dir_id);
      vector<ErrorSurface> esv(2);
      esv.push_back(e1);
      esv.push_back(e2);
      const size_t points = e1.size() + e2.size();

      float score;
      ScoreP stats_result;
      double x = LineOptimizer::LineOptimize(esv, opt_type_, stats_result, &score,
					     line_epsilon_, outside_stats);
      score *= 100;

      // we're not even returning a proper clustering...
      
      // TODO: Print information about how well we did with this direction...
      // TODO: Generalize to best() operator for TER
      
      if(score > clust->score_) {
	clust->score_ = score;
	/*
	clust->best_dir_.at(iTgt) = dir_id;
	clust->best_step_.at(iTgt) = x;
	clust->stats_.at(iTgt) = stats_result;
	dir_err_verts = points;
	*/
      }
      err_verts += points;
    }

    cerr << "Searched " << err_verts << " error vertices overall; " << dir_err_verts << " err vertices in best direction" << endl;
  }

 private:
  const vector<SparseVector<double> >& dirs_;
  const size_t beam_size_;
};

#endif
