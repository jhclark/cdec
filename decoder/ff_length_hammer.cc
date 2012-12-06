#include "ff_length_hammer.h"

#include "lattice.h"
#include "sentence_metadata.h"
#include "filelib.h"
#include "stringlib.h"
#include "hg.h"
#include "tdict.h"
using namespace std;

string LengthHammer::usage(bool /*param*/, bool /*verbose*/) {
  return "LengthHammer";
}

LengthHammer::LengthHammer(const string& param) {
  fid_ = FD::Convert("LengthHammer");
  SetStateSize(sizeof(short int));
}

Features LengthHammer::features() const {
  return single_feature(fid_);
}

LengthHammer::~LengthHammer() {}

// how many words are we off by?
// (this could be fractional for partial hypotheses)
// state can be NULL
float LengthHammer::CalcLengthHammerPenalty(short int start, short int end, short int tgt_words) const {

  // how many words have we covered so far versus how many are in the whole source?
  short int covered_words = end - start;
  float pct_covered = (float) covered_words / (float) src_len_;
  float desired_partial_len = desired_len_ * pct_covered;

  float actual_partial_len = tgt_words;

  // how many words are we off by?
  float len_diff = fabs(desired_partial_len - actual_partial_len);
  return len_diff;
}

void LengthHammer::TraversalFeaturesImpl(const SentenceMetadata& smeta,
		const Hypergraph::Edge& edge,
                const vector<const void*>& ant_states,
	        SparseVector<double>* /*features*/,
		SparseVector<double>* estimated_features,
                void* state) const {

  short int tgt_words = 0;
  for(int i=0; i < ant_states.size(); ++i) {
    tgt_words += *static_cast<const short int*>( ant_states.at(i) );
  }
  tgt_words += edge.rule_->EWords();
  if (state != NULL) {
    short int* rstate = static_cast<short int*>(state);
    *rstate = tgt_words;
  }

  short int start = edge.i_;
  short int end = edge.j_;
  float value = CalcLengthHammerPenalty(start, end, tgt_words);

  // we only ever estimate here
  estimated_features->set_value(fid_, value);
}

void LengthHammer::FinalTraversalFeatures(const void* ant_state, SparseVector<double>* features) const {
  const short int tgt_words = *static_cast<const short int*>(ant_state);
  float value = CalcLengthHammerPenalty(0, src_len_, tgt_words);
  features->set_value(fid_, value);
}

/*
boost::shared_ptr<FeatureFunction> CreateModel(const std::string &param) {
  LengthHammer *ret = new LengthHammer(param);
  ret->Init();
  return boost::shared_ptr<FeatureFunction>(ret);
}

boost::shared_ptr<FeatureFunction> LengthHammerFactory::Create(std::string param) const {
  return CreateModel(param);
}

std::string LengthHammerFactory::usage(bool params, bool verbose) const {
  return LengthHammer::usage(params, verbose);
}
*/

void LengthHammer::PrepareForInput(const SentenceMetadata& smeta) {
  string value = smeta.GetSGMLValue("desired_len");
  if (value.empty()) {
    cerr << "No desired_len found in input SGML" << endl;
    abort();
  }
  cerr << "Found desired target hypothesis length: " << value << endl;
  desired_len_ = atoi(value.c_str());

  // determine total src len
  // XXX: Does not work for lattice translation!
  src_len_ = smeta.src_lattice_.size();
}
