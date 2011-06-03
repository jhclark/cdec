#include <iostream>
#include <vector>
#include <map>
#include <iomanip>
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

  void Load(const string& file) {
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
	  question_.reset(new SrcSentQuestion);
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

class DTreeOptimizer {

 public:
 DTreeOptimizer(LineOptimizer::ScoreType opt_type,
		const double line_epsilon,
		const double dt_epsilon,
		const int min_sents_per_node,
		const vector<shared_ptr<Question> >& questions) 
   : opt_type_(opt_type),
     line_epsilon_(line_epsilon),
     dt_epsilon_(dt_epsilon),
     min_sents_per_node_(min_sents_per_node),
     questions_(questions),
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
				const vector<SparseVector<double> >& dirs,
				int* iMatchDir,
				double* step) {

    const double MINF = -numeric_limits<double>::infinity();
    // determine which direction contains our weights
    // if we're at the origin, any direction will match
    for(unsigned i=0; i < dirs.size(); ++i) {
      *step = SolveStep(origin, dirs.at(i), weights);
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
  // this method sorts the error surfaces if they are not already sorted
  void UpdateStats(const size_t iMatchDir,
		   const double step,
		   vector<vector<ErrorSurface> >& surfaces_by_dir_by_sent,
		   const vector<bool>& active_sents,
		   vector<ScoreP>* parent_stats_by_sent) {

    // now collect the sufficient statistics at this weight point for each sentence
    for(size_t iSent = 0; iSent < active_sents.size(); ++iSent) {
      if(active_sents.at(iSent)) {
	assert(surfaces_by_dir_by_sent.size() > iMatchDir);
	assert(surfaces_by_dir_by_sent.at(iMatchDir).size() > iSent);
	ErrorSurface& sent_surface = surfaces_by_dir_by_sent.at(iMatchDir).at(iSent);
	
	// sort by point on (weight) line where each ErrorSegment induces a change in the error rate
	sort(sent_surface.begin(), sent_surface.end(), ErrorSegmentComp());
	
	if(DEBUG) cerr << "Accumulating sufficient statistics for sentence " << iSent << " along direction " << iMatchDir << " by " << step << endl;
	ScoreP accp = sent_surface.front().delta->GetZero();
	for(ErrorIter it = sent_surface.begin(); it != sent_surface.end(); ++it) {
	  if(DEBUG) cerr << "stepping: " << *it << endl;
	  if(it->x <= step) {
	    // we haven't yet stepped onto the line segment on this surface
	    // containing the error count of interest
	    accp->PlusEquals(*it->delta);
	    if(DEBUG) cerr << "added: " << *accp << endl;
	  } else {
	    if(DEBUG)
	      cerr << "skipped: " << *accp << endl;
	    else
	      break;
	  }
	}
	if(DEBUG) cerr << "Stats: " << *accp << endl;
	parent_stats_by_sent->at(iSent) = accp;
      }
    }
  }

  // note: all surfaces must be relative to the same origin
  // surfaces may be sorted in place
  // returns the score of the new tree
  float GrowTree(const SparseVector<double>& origin,
		 const vector<SparseVector<double> >& dirs,
		 const vector<DTSent>& src_sents,
		 vector<vector<ErrorSurface> >& surfaces_by_dir_by_sent,
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
	GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, active_sents_by_branch.at(iBranch), dtree.branches_.at(iBranch));
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
      UpdateStats(iMatchDir, step, surfaces_by_dir_by_sent, active_sents, &parent_stats_by_sent);

      ScoreP node_score = surfaces_by_dir_by_sent.front().front().front().delta->GetZero();
      for(size_t i=0; i<parent_stats_by_sent.size(); ++i) {
	node_score->PlusEquals(*parent_stats_by_sent.at(i));
	if(DEBUG) cerr << "Accumulating node score: " << *node_score << endl;
      }
      const float before_score = node_score->ComputeScore()*100;
      cerr << "Projected node score before splitting: " << before_score << endl;

      // optimize this node on its own to make sure gains
      // are due to splitting, not a larger view of the decoder's search space
      float n_best_score;
      size_t n_best_dir_id;
      double n_best_dir_update;
      OptimizeNode(dirs, active_sents, surfaces_by_dir_by_sent, parent_stats_by_sent,
		   &n_best_score, &n_best_dir_id, &n_best_dir_update);
      cerr << "Projected score after optimizing pre-split node: " << n_best_score << endl;

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
	    OptimizeNode(dirs, active_sents_by_branch.at(iBranch), surfaces_by_dir_by_sent, prev_stats_by_sent,
			 &q_best_score, &q_best_dir_ids.at(iBranch), &q_best_dir_updates.at(iBranch));
	    cerr << "(branch: " << iBranch << " " << q_best_score << ") ";

	    // grab sufficient stats for sentences we just optimized
	    // so that the optimization of the no branch is slightly
	    // more accurate than the previous branch
	    UpdateStats(q_best_dir_ids.at(iBranch), q_best_dir_updates.at(iBranch),
			surfaces_by_dir_by_sent, active_sents_by_branch.at(iBranch), &prev_stats_by_sent);
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
  // parent_stats_by_sent: the sufficient statistics for each currently selected
  //                       hypothesis under the parent model.
  // 
  void OptimizeNode(const vector<SparseVector<double> > dirs,
		    const vector<bool>& sent_ids,
		    const vector<vector<ErrorSurface> >& surfaces_by_dir_by_sent,
		    const vector<ScoreP>& parent_stats_by_sent,
		    float* best_score,
		    size_t* best_dir_id,
		    double* best_dir_update) {

    assert(sent_ids.size() > 0);
    assert(parent_stats_by_sent.size() == sent_ids.size());
    assert(dirs.size() == surfaces_by_dir_by_sent.size());

    // accumulate metric stats for sentences outside this DTNode
    ScoreP outside_stats = surfaces_by_dir_by_sent.front().front().front().delta->GetZero();
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

    for(size_t dir_id = 0; dir_id < dirs.size(); ++dir_id) {
      
      // accumulate the error surface for this direction
      // for the sentences inside this DTNode
      vector<ErrorSurface> esv;
      for(size_t i =0; i<sent_count; ++i) {
	if(sent_ids.at(i)) {
	  const ErrorSurface& sent_surface = surfaces_by_dir_by_sent.at(dir_id).at(i);
	  esv.push_back(sent_surface);
	}
      }


      float score;
      Score* stats_result; //unused
      double x = LineOptimizer::LineOptimize(esv, opt_type_, &stats_result, &score,
					     line_epsilon_, outside_stats);
      score *= 100;

      // TODO: Print information about how well we did with this direction...
      // TODO: Generalize to best() operator for TER
      if(score > *best_score) {
	*best_score = score;
	*best_dir_id = dir_id;
	*best_dir_update = x;
      }
    }
  }

  const vector<shared_ptr<Question> > questions_;
  const LineOptimizer::ScoreType opt_type_;
  const float line_epsilon_;
  const float dt_epsilon_;
  const int min_sents_per_node_;
  const bool DEBUG;
};
