#include <iostream>
#include <vector>
#include <map>

#include <boost/shared_ptr.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>

#include "sparse_vector.h"
#include "error_surface.h"
#include "line_optimizer.h"
#include "b64tools.h"
#include "tdict.h"
#include "filelib.h"
#include "stringlib.h"

#include "question.h"

using namespace std;
using namespace boost;
namespace po = boost::program_options;

typedef vector<DTSent>::const_iter SentIter;

struct DTNode {
  Question* question_;
  DTNode* yes_branch_;
  DTNode* no_branch_;
  SparseVector<double>* weights_;

  DTNode(Question* q)
    : question_(q),
      yesBranch_(NULL),
      noBranch_(NULL),
      weights_(NULL)
  {}

  bool IsLeaf() { return (question_ == NULL); }
};

class DTreeOptimizer {

 public:
 DTreeOptimizer(Line::Optimizer opt_type,
		const double line_epsilon,
		const double dt_epsilon,
		const int min_sents_per_node) 
   : opt_type_(opt_type),
     line_epsilon_(line_epsilon),
     dt_epsilon_(dt_epsilon),
     min_sents_per_node_(min_sents_per_node)
    {
    questions_.push_back(shared_ptr<Question>(new QuestionQuestion()));
    questions_.push_back(shared_ptr<Question>(new LengthQuestion(3)));
    questions_.push_back(shared_ptr<Question>(new LengthQuestion(5)));
    questions_.push_back(shared_ptr<Question>(new LengthQuestion(7)));
    
    // TODO: Question factory
    // TODO: LDA topic question
  }

  // returns whether or not this is a valid partition
  bool Partition(const Question& q,
		 const vector<bool>& active_sents,
		 vector<bool>* yes_sents,
		 vector<bool>* no_sents) {

    size_t yes = 0;
    size_t no = 0;
    for(size_t sid = 0; sid < src_sents.size(); ++sid) {
      (*yes_sents)[sid] = false;
      (*no_sents)[sid] = false;
      if(active_sents.at(sid)) {
	if(q.ask(src_sents.at(sid))) {
	  (*yes_sents)[sid] = true;
	  ++yes;
	} else {
	  (*no_sents)[sid] = true;
	  ++no;
	}
      }
    }

    // TODO: Say how many nodes belong to each set according to this question
    return (yes >= min_sents_per_node_) && (no >= min_sents_per_node_);
  }

  // note: all surfaces must be relative to the same origin
  // returns the score of the new tree
  float GrowTree(const vector<SparseVector<double> > dirs,
		 const vector<DTSent>& src_sents,
		 const vector<ErrorSurface>& surfaces_by_dir_by_sent,
		 const vector<bool> active_sents,
		 DTNode& dtree) {
    
    // game plan:
    // 1) implement dt algorithm here
    // 2) modify surrounding binaries and configs to pass in required data
    // 3) test
    // 4) modify dist_vest.pl to support this

    // first, traverse the current dtree to its ends
    if(!dtree.IsLeaf()) {
      // need to keep recursing
      
      assert(dtree->yes_branch_ != NULL);
      assert(dtree->no_branch_ != NULL);

      // grow the left side, then the right
      GrowTree(dirs, src_sents, surfaces_by_dir_by_sent, dtree->yes_branch_);
      float best_score = GrowTree(dirs, src_sents, surfaces_by_dir_by_sent, dtree->no_branch_);
      return best_score;

    } else {
      // we're at a leaf... start working

      // this will have size zero for the root node
      vector<ScoreP> parent_stats_by_sent;
      UpdateStats(active_sents, &parent_stats_by_sent);
      // TODO: Even an empty decision tree requires params at the root
      
      float best_score;
      size_t best_q;
      size_t best_dir_id;
      double best_dir_update;
      for(size_t qid = 0; qid < questions.size(); ++qid) {
	const Question q = questions_.at(qid);

	// partition the active sentences for this node into sets for
	// child nodes based on this question
	vector<bool> yes_sents;
	vector<bool> no_sents;
	bool valid = Partition(q, active_sents, &yes_sents, &no_sents);
	if(!valid) {
	  // too few sentences in one of the sets
	} else {
	  // now optimize each node

	  // TODO: Obtain parent_stats_by_sent
	  float q_best_score;
	  size_t q_best_dir_id;
	  double q_best_dir_update;
	  OptimizeNode(dirs, yes_sents, surfaces_by_dir_by_sent, parent_stats_by_sent,
		       &q_best_score, &q_best_dir_id, &q_best_dir_update);
	  // TODO: Generalize to best()
	  if(q_best_score > best_score) {
	    best_q = qid;
	    best_score = q_best_score;
	    best_dir_id = q_best_dir_id;
	    best_dir_update = q_best_dir_update;
	  }
	}
      }
      
      // TODO: For the best question, go back and find the error segment
      //       that will be active under the optimized weights
      vector<bool> yes_sents;
      vector<bool> no_sents;
      Partition(best_q, active_sents, &yes_sents, &no_sents);

      return best_score;
    }
  }

 private:
  // parent_stats_by_sent: the sufficient statistics for each currently selected
  //                       hypothesis under the parent model.
  // 
  void OptimizeNode(const vector<SparseVector<double> > dirs,
		    const vector<bool>& sent_ids,
		    const vector<ErrorSurface>& surfaces_by_dir_by_sent,
		    const vector<ScoreP>& parent_stats_by_sent,
		    float* best_score,
		    size_t* best_dir_id,
		    double* best_dir_update) {

    assert(sent_ids.size() > 0);

    // accumulate metric stats for sentences outside this DTNode
    ScoreP outside_stats = surfaces_by_dir_by_sent.front()->front()->delta->GetZero();
    const size_t sent_count = sent_ids.size();
    size_t active_count = 0;
    for(size_t i =0; i<sent_count; ++i) {
      if(sent_ids.at(i)) {
	++active_count;
      } else {
 	outside_stats->PlusEquals(parent_stats_by_sent.at(i));
      }
    }

    // TODO: best direction, best change
    for(size_t dir_id = 0; i < dirs.size(); ++dir_id) {
      
      // accumulate the error surface for this direction
      // for the sentences inside this DTNode
      vector<ErrorSurface> esv;
      for(size_t i =0; i<sent_count; ++i) {
	if(sent_ids.at(i)) {
	  outside_stats->PlusEquals(parent_stats_by_dir_by_sent.at(dir_id).at(i));
	}
      }


      float score;
      Score* stats_result; //unused
      double x = LineOptimizer::LineOptimize(esv, opt_type, &stats_result, &score,
					     line_epsilon_, outside_stats);

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
};
