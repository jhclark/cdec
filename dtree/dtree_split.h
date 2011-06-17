#ifndef DTREE_SPLIT_H_
#define DTREE_SPLIT_H_

#include "dtree.h"

class DTreeSplitOptimizer : protected DTreeOptBase {

 public:
 DTreeSplitOptimizer(const LineOptimizer::ScoreType opt_type,
		     const double line_epsilon,
		     const double dt_epsilon,
		     const unsigned min_sents_per_node,
		     const vector<shared_ptr<Question> >& questions) 
   : DTreeOptBase(opt_type, line_epsilon, min_sents_per_node),
     dt_epsilon_(dt_epsilon),
     questions_(questions)
    {}

  // note: all surfaces must be relative to the same origin
  // surfaces may be sorted in place
  // returns the score of the new tree
  float GrowTree(const SparseVector<double>& origin,
		 const vector<SparseVector<double> >& dirs,
		 const vector<DTSent>& src_sents,
		 vector<DirErrorSurface>& sent_surfs,
		 const vector<bool>& active_sents,
		 DTNode& dtree) {
    
    // game plan:
    // 1) implement dt algorithm here
    // 2) modify surrounding binaries and configs to pass in required data
    // 3) test
    // 4) modify dist_vest.pl to support this

    // first, traverse the current dtree to its ends
    if(!dtree.IsLeaf()) {
      // need to keep recursing
      vector<unsigned> counts_by_branch;
      vector<vector<bool> > active_sents_by_branch; 
      Partition(*dtree.question_, src_sents, active_sents, &counts_by_branch, &active_sents_by_branch);

      // grow the left side, then the right
      // TODO: XXX: Combine scores for trees with height > 1 !!!
      size_t num_branches = active_sents_by_branch.size();
      for(size_t iBranch=0; iBranch<num_branches; ++iBranch) {
	assert(dtree.branches_.size() == num_branches);
	GrowTree(origin, dirs, src_sents, sent_surfs, active_sents_by_branch.at(iBranch), dtree.branches_.at(iBranch));
      }
      return 0.0;

    } else {
      // we're at a leaf... start working

      // determine the error counts for each sentence under the
      // current weights at this node
      int iMatchDir;
      double step;
      SolveForDirectionAndStep(dtree.weights_, origin, dirs, &iMatchDir, &step);

      vector<ScoreP> parent_stats_by_sent(src_sents.size());
      parent_stats_by_sent.resize(src_sents.size());
      UpdateStats(iMatchDir, step, sent_surfs, active_sents, &parent_stats_by_sent);

      const float before_score = ScoreStats(parent_stats_by_sent);
      cerr << "Projected node score before splitting: " << before_score << endl;

      // optimize this node on its own to make sure gains
      // are due to splitting, not a larger view of the decoder's search space
      float n_best_score;
      size_t n_best_dir_id;
      double n_best_dir_update;
      size_t n_dir_err_verts, n_err_verts;
      OptimizeNode(dirs, active_sents, sent_surfs, parent_stats_by_sent,
		   &n_best_score, &n_best_dir_id, &n_best_dir_update, &n_dir_err_verts, &n_err_verts);
      cerr << "Projected score after optimizing pre-split node: " << n_best_score
	   << " (" << n_dir_err_verts << " error vertices, " << n_err_verts << ")" << endl;

      float best_score = 0.0;
      int best_qid = -1;
      vector<size_t> best_dir_ids;
      vector<double> best_dir_updates;
      for(size_t qid = 0; qid < questions_.size(); ++qid) {
	const Question& q = *questions_.at(qid);

	// partition the active sentences for this node into sets for
	// child nodes based on this question

	vector<unsigned> counts_by_branch;
	vector<vector<bool> > active_sents_by_branch; 
	bool valid = Partition(q, src_sents, active_sents, &counts_by_branch, &active_sents_by_branch);
	const size_t num_branches = counts_by_branch.size();
	
	cerr << "Question "
	     << setw(4) << qid
	     << setw(0) << ": "
	     << setw(25) << q
	     << setw(0) << " ::";
	for(unsigned iBranch=0; iBranch<num_branches; ++iBranch) {
	  cerr << setw(0) << " branch " << iBranch << " = " << setw(4) << counts_by_branch.at(iBranch);
	}
	cerr << setw(0) << ": ";
	if(!valid) {
	  // too few sentences in one of the sets
	  cerr << "Skipping since it fragments the data too much" << endl;
	} else {
	  // now optimize each node

	  vector<ScoreP> prev_stats_by_sent = parent_stats_by_sent;
	  float q_best_score;
	  vector<size_t> q_best_dir_ids(num_branches);
	  vector<double> q_best_dir_updates(num_branches);
	  q_best_dir_ids.resize(num_branches);
	  q_best_dir_updates.resize(num_branches);

	  for(unsigned iBranch=0; iBranch<num_branches; ++iBranch) {
	    size_t dir_err_verts, err_verts;
	    OptimizeNode(dirs, active_sents_by_branch.at(iBranch), sent_surfs, prev_stats_by_sent,
			 &q_best_score, &q_best_dir_ids.at(iBranch), &q_best_dir_updates.at(iBranch), &dir_err_verts, &err_verts);
	    cerr << "(branch: " << iBranch << " " << q_best_score << "; " << dir_err_verts << " err vertices in best direction, " << err_verts << " total) " << endl;

	    // grab sufficient stats for sentences we just optimized
	    // so that the optimization of the no branch is slightly
	    // more accurate than the previous branch
	    UpdateStats(q_best_dir_ids.at(iBranch), q_best_dir_updates.at(iBranch),
			sent_surfs, active_sents_by_branch.at(iBranch), &prev_stats_by_sent);
	  }
	  const float score_gain = q_best_score - n_best_score;
	  cerr << " (gain = " << score_gain << ")" << endl;

	  // TODO: Generalize to best()
	  // TODO: Check for minimum improvement as part of regularization
	  // TODO: pointer copy instead of vector copy
	  if(q_best_score > best_score) {
	    best_qid = qid;
	    best_score = q_best_score;
	    best_dir_ids = q_best_dir_ids;
	    best_dir_updates = q_best_dir_updates;
	  }
	}
      }

      assert(best_qid != -1);

      // TODO: Generalize for TER?
      const float score_gain = best_score - n_best_score;
      const shared_ptr<Question>& best_q = questions_.at(best_qid);
      cerr << "Best question: " << *best_q << " with score " << best_score << " (gain = " << score_gain << ")" << endl;

      // create new branches for our decision tree
      dtree.question_ = best_q;
      const size_t num_branches = best_dir_ids.size();
      for(unsigned iBranch=0; iBranch < num_branches; ++iBranch) {
	SparseVector<double> branch_weights = dtree.weights_; // copy this node's weights
	branch_weights += dirs.at(best_dir_ids.at(iBranch)) * best_dir_updates.at(iBranch);
	dtree.branches_.push_back(DTNode(branch_weights));
      }

      return best_score;
    }
  }

 private:
  const vector<shared_ptr<Question> > questions_;
  const float dt_epsilon_;
};

#endif
