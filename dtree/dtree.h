#include <iostream>
#include <vector>
#include <map>
#include <iomanip>
using namespace std;

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

#include "question.h"

typedef vector<DTSent>::const_iterator SentIter;
typedef vector<SparseVector<double> >::const_iterator DirIter;

struct DTNode {
  const Question* question_;
  DTNode* yes_branch_;
  DTNode* no_branch_;
  SparseVector<double> weights_;

  // internal node
  DTNode(Question* q, DTNode* yes_branch, DTNode* no_branch)
    : question_(q),
      yes_branch_(yes_branch),
      no_branch_(no_branch)
  {}

  // leaf node
  DTNode(SparseVector<double>& weights)
  : question_(NULL),
    yes_branch_(NULL),
    no_branch_(NULL),
    weights_(weights)
  {}

  bool IsLeaf() const {
    return (question_ == NULL);
  }

  void ToString(ostream& out) const {
    out << "(";
    if(IsLeaf()) {
      out << weights_;
    } else {
      question_->Serialize(out);
      yes_branch_->ToString(out);
      no_branch_->ToString(out);
    }
    out << ")";
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
		const int min_sents_per_node) 
   : opt_type_(opt_type),
     line_epsilon_(line_epsilon),
     dt_epsilon_(dt_epsilon),
     min_sents_per_node_(min_sents_per_node),
     DEBUG(false)
    {
      questions_.push_back(shared_ptr<Question>(new QuestionQuestion()));
      for(int i=2; i<25; ++i) {
	questions_.push_back(shared_ptr<Question>(new LengthQuestion(i)));
      }
    
      // TODO: Question factory
      // TODO: LDA topic question
  }

  // returns whether or not this is a valid partition
  bool Partition(const Question& q,
		 const vector<DTSent>& src_sents,
		 const vector<bool>& active_sents,
		 int* yes,
		 int* no,
		 vector<bool>* yes_sents,
		 vector<bool>* no_sents) {

    *yes = 0;
    *no = 0;
    assert(yes_sents->size() == active_sents.size());
    assert(no_sents->size() == active_sents.size());
    for(size_t sid = 0; sid < active_sents.size(); ++sid) {
      (*yes_sents)[sid] = false;
      (*no_sents)[sid] = false;
      if(active_sents.at(sid)) {
	if(q.Ask(src_sents.at(sid))) {
	  (*yes_sents)[sid] = true;
	  ++*yes;
	} else {
	  (*no_sents)[sid] = true;
	  ++*no;
	}
      }
    }

    // TODO: Say how many nodes belong to each set according to this question
    return (*yes >= min_sents_per_node_) && (*no >= min_sents_per_node_);
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
  void UpdateStats(const int iMatchDir,
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
	(*parent_stats_by_sent)[iSent] = accp;
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
      
      assert(dtree.yes_branch_ != NULL);
      assert(dtree.no_branch_ != NULL);

      vector<bool> yes_sents(src_sents.size());
      vector<bool> no_sents(src_sents.size());
      yes_sents.resize(src_sents.size());
      no_sents.resize(src_sents.size());
      int yes = 0;
      int no = 0;
      Partition(*dtree.question_, src_sents, active_sents, &yes, &no, &yes_sents, &no_sents);

      // grow the left side, then the right
      GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, yes_sents, *dtree.yes_branch_);
      GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, no_sents, *dtree.no_branch_);
      // TODO: Combine scores for trees with height > 1
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
      int best_yes_dir_id = -1;
      int best_no_dir_id = -1;
      double best_yes_dir_update = 0.0;
      double best_no_dir_update = 0.0;
      for(size_t qid = 0; qid < questions_.size(); ++qid) {
	const Question& q = *questions_.at(qid);

	// partition the active sentences for this node into sets for
	// child nodes based on this question
	vector<bool> yes_sents(src_sents.size());
	vector<bool> no_sents(src_sents.size());
	yes_sents.resize(src_sents.size());
	no_sents.resize(src_sents.size());
	int yes = 0;
	int no = 0;
	bool valid = Partition(q, src_sents, active_sents, &yes, &no, &yes_sents, &no_sents);
	cerr << "Question "
	     << setw(4) << qid
	     << setw(0) << ": "
	     << setw(25) << q
	     << setw(0) << " ::"
	     << setw(0) << " yes = " << setw(4) << yes
	     << setw(0) << " no = " << setw(4) << no
	     << setw(0) << ": ";
	if(!valid) {
	  // too few sentences in one of the sets
	  cerr << "Skipping since it fragments the data too much" << endl;
	} else {
	  // now optimize each node

	  float q_best_score;
	  size_t q_best_yes_dir_id;
	  double q_best_yes_dir_update;
	  OptimizeNode(dirs, yes_sents, surfaces_by_dir_by_sent, parent_stats_by_sent,
		       &q_best_score, &q_best_yes_dir_id, &q_best_yes_dir_update);
	  cerr << "(Y branch: " << q_best_score << ") ";

	  // grab sufficient stats for sentences we just optimized
	  // so that the optimization of the no branch is slightly
	  // more accurate than the yes branch
	  // NOTE: somewhat expensive vector copy
	  vector<ScoreP> parent_and_yes_stats_by_sent = parent_stats_by_sent;
	  UpdateStats(q_best_yes_dir_id, q_best_yes_dir_update, surfaces_by_dir_by_sent, yes_sents, &parent_and_yes_stats_by_sent);

	  size_t q_best_no_dir_id;
	  double q_best_no_dir_update;
	  OptimizeNode(dirs, no_sents, surfaces_by_dir_by_sent, parent_and_yes_stats_by_sent,
		       &q_best_score, &q_best_no_dir_id, &q_best_no_dir_update);
	  const float score_gain = q_best_score - n_best_score;
	  cerr << "Y&N branch: " << q_best_score << " (gain = " << score_gain << ")" << endl;

	  // TODO: Generalize to best()
	  // TODO: Check for minimum improvement as part of regularization
	  if(q_best_score > best_score) {
	    best_qid = qid;
	    best_score = q_best_score;
	    best_yes_dir_id = q_best_yes_dir_id;
	    best_no_dir_id = q_best_no_dir_id;
	    best_yes_dir_update = q_best_yes_dir_update;
	    best_no_dir_update = q_best_no_dir_update;
	  }
	}
      }

      assert(best_qid != -1);
      assert(best_yes_dir_id != -1);
      assert(best_no_dir_id != -1);

      // TODO: Generalize for TER?
      const float score_gain = best_score - n_best_score;
      const Question& best_q = *questions_.at(best_qid);
      cerr << "Best question: " << best_q << " with score " << best_score << " (gain = " << score_gain << ")" << endl;

      SparseVector<double> yes_weights = dtree.weights_;
      yes_weights += dirs.at(best_yes_dir_id) * best_yes_dir_update;
      SparseVector<double> no_weights = dtree.weights_;
      no_weights += dirs.at(best_no_dir_id) * best_no_dir_update;

      // create new branches for our decision tree
      dtree.question_ = &best_q;
      dtree.yes_branch_ = new DTNode(yes_weights);
      dtree.no_branch_ = new DTNode(no_weights);

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

  vector<shared_ptr<Question> > questions_;
  const LineOptimizer::ScoreType opt_type_;
  const float line_epsilon_;
  const float dt_epsilon_;
  const int min_sents_per_node_;
  const bool DEBUG;
};
