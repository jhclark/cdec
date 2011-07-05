#ifndef DTREE_H_
#define DTREE_H_

#include <iostream>
#include <vector>
#include <map>
#include <iomanip>
#include <queue>
using namespace std;

#include <boost/algorithm/string.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>
using namespace boost;
namespace po = boost::program_options;

#include "sparse_vector.h"
#include "error_surface.h"
#include "line_optimizer.h"
#include "b64tools.h"
#include "tdict.h"
#include "filelib.h"
#include "stringlib.h"
#include "weights.h"

#include "question.h"
#include "cluster.h"
#include "beam.h"

typedef vector<DTSent>::const_iterator SentIter;
typedef vector<SparseVector<double> >::const_iterator DirIter;

struct DTNode {
  shared_ptr<const Question> question_;

  // there will be 2 branches for a binary tree
  vector<DTNode> branches_;
  SparseVector<double> weights_;

  // internal node
  DTNode(const shared_ptr<Question> q, const vector<DTNode>& branches)
    : question_(q),
      branches_(branches)
  {}

  // leaf node
  DTNode(SparseVector<double>& weights)
  : weights_(weights)
  {}

  bool IsLeaf() const {
    return (question_ == NULL);
  }

  void ToString(ostream& out, unsigned indent=0) const {
    out << string(indent, ' ') << "(";
    if(IsLeaf()) {
      out << weights_;
    } else {
      out << '"';
      question_->Serialize(out);
      out << "\"\n";
      for(size_t i=0; i<branches_.size(); ++i) {
	out << string(indent, ' ');
	branches_.at(i).ToString(out, indent+2);
      }
    }
    out << string(indent, ' ') << ")\n";
  }

  void Load(const string& file,
	    const size_t src_sent_count) {
    ReadFile in_read(file);
    istream &in = *in_read.stream();
    while(in) {
      string line;
      getline(in, line);
      if (line.empty()) continue;
      trim(line);
      if(starts_with(line, "(\"")) {
	// non-leaf, parse question name
	trim_if(line, is_any_of("(\")"));
	if(line == "SrcSent") {
	  question_.reset(new SrcSentQuestion(src_sent_count));
	} else {
	  cerr << "Unsupported question name: " << line << endl;
	  abort();
	}
      } else {
	// leaf, parse weights
	trim_if(line, is_any_of("()"));
	Weights::ReadSparseVectorString(line, &weights_);
      }
    }
  }
};

inline ostream& operator<<(ostream& out, const DTNode& dtree) {
  dtree.ToString(out);
  return out;
}

class DTreeOptBase {

 public:
  DTreeOptBase(const vector<SparseVector<double> >& dirs,
	       const LineOptimizer::ScoreType opt_type,
	       const double line_epsilon,
	       const unsigned min_sents_per_node)
    : dirs_(dirs),
      opt_type_(opt_type),
      line_epsilon_(line_epsilon),
      min_sents_per_node_(min_sents_per_node),
      DEBUG(false)
    {}

  // returns whether or not this is a valid partition
  bool Partition(const Question& q,
		 const vector<DTSent>& src_sents,
		 const vector<bool>& active_sents,
		 vector<unsigned>* branch_counts,
		 vector<vector<bool> >* active_sents_by_branch) {

    branch_counts->clear();
    active_sents_by_branch->clear();

    for(size_t sid = 0; sid < active_sents.size(); ++sid) {
      if(active_sents.at(sid)) {
	unsigned k = q.Ask(src_sents.at(sid));
	size_t num_branches = branch_counts->size();
	if(k >= num_branches) {
	  branch_counts->resize(k+1, 0);
	  active_sents_by_branch->resize(k+1);
	  // initialize the newly created entries
	  for(unsigned j=k; j<k+1; ++j) {
	    active_sents_by_branch->at(j).resize(active_sents.size(), false);
	  }
	  num_branches = k+1;
	}
	branch_counts->at(k)++;
	active_sents_by_branch->at(k).at(sid) = true;
      }
    }

    // check if this is a valid parition
    for(unsigned i=0; i<branch_counts->size(); ++i) {
      if(branch_counts->at(i) < min_sents_per_node_) {
	return false;
      }
    }
    return true;
  }


  // determine the step size needed to get from origin to goal moving in direction dir
  // returns -inf if no result is found
  double SolveStep(const SparseVector<double> origin,
		   const SparseVector<double> dir,
		   const SparseVector<double> goal) {

    assert(origin.size() == dir.size());
    assert(origin.size() == goal.size());

    if(origin == goal) {
      cerr << origin << goal << endl;
      return 0.0;
    } else {
      const double MINF = -numeric_limits<double>::infinity();
      double step = MINF;
      for(size_t i=0; i<origin.size(); ++i) {
	if(dir.at(0) == 0.0)
	  continue;
	double distance = goal.at(i) - origin.at(i);
	if(distance == 0.0)
	  continue;
	double iStep = distance / dir.at(i);
	if(step == MINF) {
	  step = iStep; // first time
	} else {
	  if(step != iStep) {
	    // no consistent solution found
	    return MINF;
	  }
	}
      }
      return step;
    }
  }

  void SolveForDirectionAndStep(const SparseVector<double>& weights,
				const SparseVector<double>& origin,
				int* iMatchDir,
				double* step) {

    const double MINF = -numeric_limits<double>::infinity();
    // determine which direction contains our weights
    // if we're at the origin, any direction will match
    for(unsigned i=0; i < dirs_.size(); ++i) {
      *step = SolveStep(origin, dirs_.at(i), weights);
      if(*step != MINF) {
	*iMatchDir = i;
	return;
      }
    }
    if(*iMatchDir == -1) {
      cerr << "No matching direction found. I have no visibility of the requested region of the error surface." << endl;
      cerr << "Weights: " << weights << endl;
      cerr << "Origin: " << origin << endl;
      abort();
    }
  }

  // TODO: Accept multiple origins so that we can do multiple
  //       iterations of MERT
  // * this method sorts the error surfaces
  // * sent_surfs, active_sents, and parent_stats_by_sent must/will be parallel
  //   but they might actually correspond to clusters rather than sentences when doing agglomerative clustering
  void UpdateStats(const size_t iMatchDir,
		   const double step,
		   vector<DirErrorSurface>& sent_surfs,
		   const vector<bool>& active_sents,
		   vector<ScoreP>* parent_stats_by_sent) {

    assert(sent_surfs.size() == active_sents.size());

    // now collect the sufficient statistics at this weight point for each sentence
    float neg_inf = -numeric_limits<float>::infinity();
    float pos = neg_inf;
    for(size_t iSent = 0; iSent < active_sents.size(); ++iSent) {
      if(active_sents.at(iSent)) {
	assert(sent_surfs.front().size() > iMatchDir);
	assert(sent_surfs.size() > iSent);
	ErrorSurface& sent_surface = sent_surfs.at(iSent).AtDir(iMatchDir);
	
	// sort by point on (weight) line where each ErrorSegment induces a change in the error rate
	sort(sent_surface.begin(), sent_surface.end(), ErrorSegmentComp());
	
	if(DEBUG) cerr << "UpdateStats: Accumulating sufficient statistics for sentence " << iSent << " along direction " << iMatchDir << " by " << step << endl;
	ScoreP accp = sent_surface.front().delta->GetZero();
	for(ErrorIter it = sent_surface.begin(); it != sent_surface.end(); ++it) {
	  if(DEBUG) cerr << "UpdateStats: stepping: " << *it << endl;
	  if(it->x <= step) {
	    // we haven't yet stepped onto the line segment on this surface
	    // containing the error count of interest
	    accp->PlusEquals(*it->delta);
	    pos = it->x;
	    if(DEBUG) cerr << "UpdateStats: added: " << *accp << endl;
	  } else {
	    if(DEBUG)
	      cerr << "UpdateStats: skipped: " << *accp << endl;
	    else
	      break;
	  }
	}
	if(DEBUG) cerr << "UpdateStats: Found Stats along direction " << iMatchDir << " at position " << pos << ": " << *accp << endl;
	parent_stats_by_sent->at(iSent) = accp;
      }
    }
  }

  // vector of stats (each element in the vector is likely a sentence or cluster in the corpus)
  float ScoreStats(const vector<ScoreP>& stats) {
    ScoreP node_score = stats.front()->GetZero();
    for(size_t i=0; i<stats.size(); ++i) {
      node_score->PlusEquals(*stats.at(i));
      if(DEBUG) cerr << "ScoreStats: Accumulating node score: " << *node_score << endl;
    }
    return node_score->ComputeScore() * 100;
  }

  // parent_stats_by_sent: the sufficient statistics for each currently selected
  // hypothesis under the parent model.
  // * sent_surfs may actually be clusters rather than sentences during agglomerative clustering
  //
  // returns true if a better solution than the prev_best_score is found
  bool OptimizeNode(const vector<bool>& sent_ids,
		    const vector<DirErrorSurface>& sent_surfs,
		    const vector<ScoreP>& parent_stats_by_sent,
		    const float prev_best_score,
		    const size_t prev_best_dir,
		    const float prev_best_pos,
		    float* best_score,
		    size_t* best_dir_id,
		    double* best_dir_update,
		    size_t* best_dir_err_verts,
		    size_t* err_verts) {

    assert(sent_ids.size() > 0);
    assert(parent_stats_by_sent.size() == sent_ids.size());
    assert(dirs_.size() == sent_surfs.front().size());

    // accumulate metric stats for sentences outside this DTNode
    ScoreP outside_stats = parent_stats_by_sent.front()->GetZero();
    const size_t sent_count = sent_ids.size();
    size_t active_count = 0;
    for(size_t i =0; i<sent_count; ++i) {
      if(sent_ids.at(i)) {
	// accumuate metric stats inside this node later within loop over dirs
	++active_count;
      } else {
	const ScoreP& sent_stats = parent_stats_by_sent.at(i);
 	outside_stats->PlusEquals(*sent_stats);
      }
    }

    if(DEBUG) {
      ScoreP all_stats = parent_stats_by_sent.front()->GetZero();
      for(size_t i =0; i<sent_count; ++i) {
	const ScoreP& sent_stats = parent_stats_by_sent.at(i);
 	all_stats->PlusEquals(*sent_stats);
      }
      cerr << "OptimizeNode: ALL STATS BEFORE: " << *all_stats << endl;
      cerr << "OptimizeNode: OUTSIDE STATS BEFORE (for " << (sent_count - active_count) << " inactive sentences): " << *outside_stats << endl;
    }

    bool found_better = false;
    *best_score = prev_best_score;
    *best_dir_id = prev_best_dir;
    *best_dir_update = prev_best_pos;
    *err_verts = 0;

    for(size_t dir_id = 0; dir_id < dirs_.size(); ++dir_id) {
      
      // accumulate the error surface for this direction
      // for the sentences inside this DTNode
      vector<ErrorSurface> esv;
      size_t points = 0;
      for(size_t i =0; i<sent_count; ++i) {
	if(sent_ids.at(i)) {
	  const ErrorSurface& sent_surface = sent_surfs.at(i).AtDir(dir_id);
	  esv.push_back(sent_surface);
	  points += sent_surface.size();
	}
      }

      if(DEBUG) cerr << "OptimizeNode: Running line optimization on " << points << " error vertices in direction " << dir_id << "... " << endl;
      float score;
      ScoreP stats_result = outside_stats->GetZero(); // unused
      double x = LineOptimizer::LineOptimize(esv, opt_type_, stats_result, &score,
					     line_epsilon_, outside_stats);
      score *= 100;

      // TODO: Print information about how well we did with this direction...
      // TODO: Generalize to best() operator for TER
      if(score > *best_score) {
	*best_score = score;
	*best_dir_id = dir_id;
	*best_dir_update = x;
	*best_dir_err_verts = points;
	found_better = true;

	if(DEBUG) cerr << "OptimizeNode: NEW BEST: " << score << " " << dir_id << " " << x << " (gain over prev 'branch' = " << (*best_score - prev_best_score) << ") :: STATS = " << *stats_result << endl;
      }
      *err_verts += points;
    }
    return found_better;
  }

  // TODO: Refactor this method signature to induce less bleeding from the eyes
  void OptimizeQuestion(const Question& q,
			const vector<DTSent>& src_sents,
			const vector<bool>& active_sents,
			const vector<ScoreP>& parent_stats_by_sent,
			vector<DirErrorSurface>& sent_surfs,
			const float prev_best_score,
			const size_t prev_best_dir,
			const float prev_best_pos,
			// the following are updated only if we find a better solution:
			float* best_score,
			vector<size_t>* best_dir_ids,
			vector<double>* best_dir_updates,
			vector<ScoreP>* best_opt_stats) {

    // partition the active sentences for this node into sets for
    // child nodes based on this question

    vector<unsigned> counts_by_branch;
    vector<vector<bool> > active_sents_by_branch; 
    bool valid = Partition(q, src_sents, active_sents, &counts_by_branch, &active_sents_by_branch);
    const size_t num_branches = counts_by_branch.size();
	
    if(DEBUG) cerr << "OptimizeQuestion: Optmizing question " << q << " with " << num_branches << " branches. prev_best_score = " << prev_best_score << endl;

    if(num_branches < 10) {
      for(unsigned iBranch=0; iBranch<num_branches; ++iBranch) {
	cerr << setw(0) << " branch " << iBranch << " = " << setw(4) << counts_by_branch.at(iBranch);
      }
      cerr << setw(0) << ": ";
    }

    vector<ScoreP> opt_stats = parent_stats_by_sent;
    if(!valid) {
      // too few sentences in one of the sets
      cerr << "Skipping question since it fragments the data too much" << endl;
    } else {
      // now optimize each node

      float q_best_score;
      vector<size_t> q_best_dir_ids(num_branches);
      vector<double> q_best_dir_updates(num_branches);
      q_best_dir_ids.resize(num_branches);
      q_best_dir_updates.resize(num_branches);
      
      for(unsigned iBranch=0; iBranch<num_branches; ++iBranch) {
	size_t dir_err_verts, err_verts;

	bool found_better = OptimizeNode(active_sents_by_branch.at(iBranch), sent_surfs, opt_stats,
					 q_best_score, prev_best_dir, prev_best_pos,
					 &q_best_score, &q_best_dir_ids.at(iBranch),
					 &q_best_dir_updates.at(iBranch), &dir_err_verts, &err_verts);

	const float branch_gain = q_best_score - prev_best_score;
	cerr << "branch: " << iBranch << " " << q_best_score << "; " << dir_err_verts << " err vertices in best direction, " << err_verts << " total (gain=" << branch_gain << ")" << endl;

	if(DEBUG) cerr << "OptimizeQuestion: Best direction was " << q_best_dir_ids.at(iBranch) << " at position " << q_best_dir_updates.at(iBranch) << endl;

	// grab sufficient stats for sentences we just optimized
	// so that the optimization of the next branch is slightly
	// more accurate than the previous branch
	UpdateStats(q_best_dir_ids.at(iBranch), q_best_dir_updates.at(iBranch),
		    sent_surfs, active_sents_by_branch.at(iBranch), &opt_stats);
      }
      const float score_gain = q_best_score - prev_best_score;
      cerr << "gain for this question = " << score_gain << endl;

      // TODO: Generalize to best()
      // TODO: Check for minimum improvement as part of regularization
      // TODO: pointer copy instead of vector copy
      if(q_best_score > *best_score) {
	*best_score = q_best_score;
	*best_dir_ids = q_best_dir_ids;
	*best_dir_updates = q_best_dir_updates;
	*best_opt_stats = opt_stats;
      }
    }
  }

 protected:
  const vector<SparseVector<double> >& dirs_;
  const LineOptimizer::ScoreType opt_type_;
  const float line_epsilon_;
  const unsigned min_sents_per_node_;
  const bool DEBUG;
};

#endif
