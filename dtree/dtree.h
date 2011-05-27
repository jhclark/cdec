#include <iostream>
#include <vector>
#include <map>
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
  Question* question_;
  DTNode* yes_branch_;
  DTNode* no_branch_;
  SparseVector<double> weights_;

  DTNode(Question* q)
    : question_(q),
      yes_branch_(NULL),
      no_branch_(NULL)
  {}

  bool IsLeaf() { return (question_ == NULL); }
};

class DTreeOptimizer {

 public:
 DTreeOptimizer(LineOptimizer::ScoreType opt_type,
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
		 const vector<DTSent>& src_sents,
		 const vector<bool>& active_sents,
		 vector<bool>* yes_sents,
		 vector<bool>* no_sents) {

    size_t yes = 0;
    size_t no = 0;
    for(size_t sid = 0; sid < active_sents.size(); ++sid) {
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

  // determine the step size needed to get from origin to goal moving in direction dir
  // returns -inf if no result is found
  double SolveStep(const SparseVector<double> origin,
		   const SparseVector<double> dir,
		   const SparseVector<double> goal) {

    assert(origin.size() == dir.size());
    assert(origin.size() == goal.size());

    cerr << origin << goal << endl;

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

  // TODO: Accept multiple origins so that we can do multiple
  //       iterations of MERT
  // this method sorts the error surfaces if they are not already sorted
  void UpdateStats(const SparseVector<double>& weights,
		   const SparseVector<double>& origin,
		   const vector<SparseVector<double> >& dirs,
		   vector<vector<ErrorSurface> >& surfaces_by_dir_by_sent,
		   const vector<bool>& active_sents,
		   vector<ScoreP>* parent_stats_by_sent) {

    const double MINF = -numeric_limits<double>::infinity();
    double step = MINF;
    int iMatchDir = -1;
    // determine which direction contains our weights
    // if we're at the origin, any direction will match
    for(unsigned i=0; i < dirs.size() && iMatchDir == -1; ++i) {
      step = SolveStep(origin, dirs.at(i), weights);
      if(step != MINF) {
	iMatchDir = i;
      }
    }
    if(iMatchDir == -1) {
      cerr << "No matching direction found. I have no visibility of the requested region of the error surface." << endl;
      cerr << "Weights: " << weights << endl;
      cerr << "Origin: " << origin << endl;
      abort();
    }

    // now collect the sufficient statistics at this weight point for each sentence
    for(size_t iSent = 0; iSent < active_sents.size(); ++iSent) {
      if(active_sents.at(iSent)) {
	ErrorSurface& sent_surface = surfaces_by_dir_by_sent.at(iMatchDir).at(iSent);
	
	// sort by point on (weight) line where each ErrorSegment induces a change in the error rate
	sort(sent_surface.begin(), sent_surface.end(), ErrorSegmentComp());
	
	ScoreP accp = sent_surface.front().delta->GetZero();
	for(ErrorIter it = sent_surface.begin(); it != sent_surface.end(); ++it) {
	  if(it->x < step) {
	    // we haven't yet stepped onto the line segment on this surface
	    // containing the error count of interest
	    accp->PlusEquals(*it->delta);
	  } else {
	    break;
	  }
	}
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

      vector<bool> yes_sents;
      vector<bool> no_sents;
      Partition(*dtree.question_, src_sents, active_sents, &yes_sents, &no_sents);

      // grow the left side, then the right
      GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, yes_sents, *dtree.yes_branch_);
      float best_score = GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, no_sents, *dtree.no_branch_);
      return best_score;

    } else {
      // we're at a leaf... start working

      // determine the error counts for each sentence under the
      // current weights at this node
      vector<ScoreP> parent_stats_by_sent;
      UpdateStats(dtree.weights_, origin, dirs, surfaces_by_dir_by_sent, active_sents, &parent_stats_by_sent);
      
      float best_score;
      size_t best_qid;
      size_t best_dir_id;
      double best_dir_update;
      for(size_t qid = 0; qid < questions_.size(); ++qid) {
	const Question& q = *questions_.at(qid);

	// partition the active sentences for this node into sets for
	// child nodes based on this question
	vector<bool> yes_sents;
	vector<bool> no_sents;
	bool valid = Partition(q, src_sents, active_sents, &yes_sents, &no_sents);
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
	    best_qid = qid;
	    best_score = q_best_score;
	    best_dir_id = q_best_dir_id;
	    best_dir_update = q_best_dir_update;
	  }
	}
      }
      
      // TODO: For the best question, go back and find the error segment
      //       that will be active under the optimized weights
      const Question& best_q = *questions_.at(best_qid);
      vector<bool> yes_sents;
      vector<bool> no_sents;
      Partition(best_q, src_sents, active_sents, &yes_sents, &no_sents);

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
};
