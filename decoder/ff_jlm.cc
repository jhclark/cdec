#include "ff_jlm.h"

#include <cstring>
#include <iostream>

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_map.hpp>

#include "filelib.h"
#include "stringlib.h"
#include "hg.h"
#include "tdict.h"
#include "lm/model.hh"
#include "lm/enumerate_vocab.hh"

using namespace std;

static const unsigned char HAS_FULL_CONTEXT = 1;
static const unsigned char HAS_EOS_ON_RIGHT = 2;
static const unsigned char MASK             = 7;

typedef unsigned short conj_fid;
static const conj_fid END_OF_CONJ_VEC = 0;

void load_file_into_vec(const string& filename, vector<string>* conjoin_with_feats) {
    ReadFile in_read(filename);
    istream & in = *in_read.stream();
    string line;
    while(getline(in, line)){
        if(!line.empty())
            conjoin_with_feats->push_back(line);
    }
}

// -x : rules include <s> and </s>
// -n NAME : feature id is NAME
// -C conjoin_with_name : conjoined features must be *rule* features
// -C@file : specify file with list of names to conjoin with
// -D : disable single feature (in favor of using only conjoined features)
// -L : Conjoin with match length (note: this is itself a non-local feature)
// -J : Discriminative LM mode
bool ParseLMArgs(string const& in, string* filename, string* mapfile, bool* explicit_markers, string* featname,
		 vector<int>* conjoin_with_fids, vector<int>* conjoined_fids, bool* conjoin_with_length,
		 bool* enable_main_feat, bool* disc_mode, string* count_bin_file, string* context_bin_file) {
  vector<string> const& argv=SplitOnWhitespace(in);
  *explicit_markers = false;
  *featname="JLanguageModel";
  *mapfile = "";
  *enable_main_feat = true;
  *conjoin_with_length = false;
  *disc_mode = false;
  *count_bin_file = "";
  *context_bin_file = "";
#define LMSPEC_NEXTARG if (i==argv.end()) {            \
    cerr << "Missing argument for "<<*last<<". "; goto usage; \
    } else { ++i; }

  vector<string> conjoin_with_feats;
  for (vector<string>::const_iterator last,i=argv.begin(),e=argv.end();i!=e;++i) {
    string const& s=*i;
    if (s[0]=='-') {
      if (s.size()>2) goto fail;
      switch (s[1]) {
      case 'x':
        *explicit_markers = true;
        break;
      case 'm':
        LMSPEC_NEXTARG; *mapfile=*i;
        break;
      case 'n':
        LMSPEC_NEXTARG; *featname=*i;
        break;
      case 'D':
	    *enable_main_feat = false;
	    break;
      case 'J':
	    *disc_mode = true;
        LMSPEC_NEXTARG; *count_bin_file=*i;
        LMSPEC_NEXTARG; *context_bin_file=*i;
	    break;
      case 'L':
    	*conjoin_with_length = true;
    	break;
      case 'C':
        LMSPEC_NEXTARG;
	if( (*i)[0] == '@' ) { // read feature names from file?
	    const string filename = i->substr(1);
        load_file_into_vec(filename, &conjoin_with_feats);
	} else {
	  conjoin_with_feats.push_back(*i);
	}
        break;
#undef LMSPEC_NEXTARG
      default:
      fail:
        cerr<<"Unknown JLanguageModel option "<<s<<" ; ";
        goto usage;
      }
    } else {
      if (filename->empty())
        *filename=s;
      else {
        cerr<<"More than one filename provided. ";
        goto usage;
      }
    }
  }

  if(conjoin_with_feats.size() > 0) {
    cerr << "Using base feature in addition to conjoined" << endl;

    for(unsigned i=0; i<conjoin_with_feats.size(); ++i) {
      const string& conjoin_with_name = conjoin_with_feats.at(i);
      int conjoin_with_fid = FD::Convert(conjoin_with_name);
      conjoin_with_fids->push_back(conjoin_with_fid);

      const string conjoined_name = conjoin_with_name + "__X__" + *featname;
      int conjoined_fid = FD::Convert(conjoined_name);
      cerr << "Using Conjoined Feature Pattern: " << conjoined_name << " (conjoining with fid " << conjoin_with_fid << " to make fid " << conjoined_fid << ")" << endl;
      conjoined_fids->push_back(conjoined_fid);
    }
  } else {
    cerr << "Not using conjoined LM for any local features" << endl;
  }


  if (!filename->empty())
    return true;
usage:
  cerr << "JLanguageModel is incorrect!\n";
  return false;
}

template <class Model>
string JLanguageModel<Model>::usage(bool /*param*/,bool /*verbose*/) {
  return "JLanguageModel";
}

struct VMapper : public lm::EnumerateVocab {
  VMapper(vector<lm::WordIndex>* out) : out_(out), kLM_UNKNOWN_TOKEN(0) { out_->clear(); }
  void Add(lm::WordIndex index, const StringPiece &str) {
    const WordID cdec_id = TD::Convert(str.as_string());
    if (cdec_id >= out_->size())
      out_->resize(cdec_id + 1, kLM_UNKNOWN_TOKEN);
    (*out_)[cdec_id] = index;
  }
  vector<lm::WordIndex>* out_;
  const lm::WordIndex kLM_UNKNOWN_TOKEN;
};

template <class Model>
class JLanguageModelImpl {

  // returns the number of unscored words at the left edge of a span
  inline int UnscoredSize(const void* state) const {
    return *(static_cast<const char*>(state) + unscored_size_offset_);
  }

  inline void SetUnscoredSize(int size, void* state) const {
    *(static_cast<char*>(state) + unscored_size_offset_) = size;
  }

  static inline const lm::ngram::State& RemnantLMState(const void* state) {
    return *static_cast<const lm::ngram::State*>(state);
  }

  inline void SetRemnantLMState(const lm::ngram::State& lmstate, void* state) const {
    // if we were clever, we could use the memory pointed to by state to do all
    // the work, avoiding this copy
    memcpy(state, &lmstate, ngram_->StateSize());
  }

  lm::WordIndex IthUnscoredWord(int i, const void* state) const {
    const lm::WordIndex* const mem = reinterpret_cast<const lm::WordIndex*>(static_cast<const char*>(state) + unscored_words_offset_);
    return mem[i];
  }

  void SetIthUnscoredWord(int i, lm::WordIndex index, void *state) const {
    lm::WordIndex* mem = reinterpret_cast<lm::WordIndex*>(static_cast<char*>(state) + unscored_words_offset_);
    mem[i] = index;
  }

  // returns a pointer to the beginning of the vector
  // the last element in the vector is followed by 0 (END_OF_CONJ_VEC) unless it is of size conjoined_fids_.size()
  // this also means it's quite important that no actual fid stored in it be zero
  // (this should be guaranteed since we must query a base feature before we can conjoin with it)
  const conj_fid* IthConjVec(int i, const void* state) const {
    const conj_fid* begin = reinterpret_cast<const conj_fid*>(
			    static_cast<const char*>(state)
			    + conjoined_feats_offset_ + i * conj_vec_size_);
    return begin;
  }

  void SetIthConjVec(int i, const conj_fid* vec, void* state) const {
    void* dest = reinterpret_cast<void*>(static_cast<char*>(state) + conjoined_feats_offset_ + i * conj_vec_size_);
    memcpy(dest, vec, conj_vec_size_);
  }


  void FireConjFeats(SparseVector<double>* features, const conj_fid* feats_to_fire,
		     const int len_fid, double value, bool est) const {
    int i = 0;
    if(features != NULL) {
      for(const conj_fid* it = feats_to_fire; i != conjoined_fids_.size() && *it != END_OF_CONJ_VEC; ++it, ++i) {
	int fid = *it;
	///cerr << "Firing " << (est?"est":"feature") << " fid " << fid << ": " << value << endl;
	features->set_value(fid, value);
      }
      if(len_fid != 0) {
	///cerr << "Firing non-local " << (est?"est":"feature") << " fid " << len_fid << ": " << value << endl;
	features->set_value(len_fid, value);
      }
    } else {
      //cerr << "NOTE: NULL features" << endl;
    }
    //cerr << "Fired " << i << " conjoined feats" << endl;
  }

  inline bool GetFlag(const void *state, unsigned char flag) const {
    return (*(static_cast<const char*>(state) + is_complete_offset_) & flag);
  }

  inline void SetFlag(bool on, unsigned char flag, void *state) const {
    if (on) {
      *(static_cast<char*>(state) + is_complete_offset_) |= flag;
    } else {
      *(static_cast<char*>(state) + is_complete_offset_) &= (MASK ^ flag);
    }
  }

  inline bool HasFullContext(const void *state) const {
    return GetFlag(state, HAS_FULL_CONTEXT);
  }

  inline void SetHasFullContext(bool flag, void *state) const {
    SetFlag(flag, HAS_FULL_CONTEXT, state);
  }

 public:
  // we now directly modify features and est_features here
  // so that we can modify multiple features (e.g. conjoined features)
  void LookupWords(const TRule& rule, const vector<const void*>& ant_states,
		   SparseVector<double>* features, SparseVector<double>* est_features,
		   double* oovs, double* est_oovs, void* remnant) {

    //    cerr << "Lookup words for rule: " << TD::Convert(rule.lhs_) << " ||| " << TD::GetString(rule.f_) << " ||| " << TD::GetString(rule.e_) << " ||| " << rule.scores_ << endl;
    //    cerr << "Lookup words for rule: " << TD::Convert(rule.lhs_) << " ||| " << rule.f_ << " ||| " << rule.e_ << " ||| " << rule.scores_ << endl;

    double sum = 0.0;
    double est_sum = 0.0;
    int num_scored = 0;
    int num_estimated = 0;
    if (oovs) *oovs = 0;
    if (est_oovs) *est_oovs = 0;
    bool saw_eos = false;
    bool has_some_history = false;
    lm::ngram::State state = ngram_->NullContextState();
    const vector<WordID>& e = rule.e();
    bool context_complete = false;
    for (int j = 0; j < e.size(); ++j) {
      ///cerr << "Target terminal number " << j << endl;
      if (e[j] < 1) {   // handle non-terminal substitution
        const void* astate = (ant_states[-e[j]]);
	
	// score unscored items (always on the left, since they
	// might not have seen their full context yet)
        int unscored_ant_len = UnscoredSize(astate);
        for (int k = 0; k < unscored_ant_len; ++k) {
          const lm::WordIndex cur_word = IthUnscoredWord(k, astate);
	  ///cerr << "Antecedent " << k << "(E nonterm index is " << -e[j] << "): Scoring LM Word ID " << cur_word << endl;
	  const conj_fid* conj_fid_vec = IthConjVec(k, astate);
	  int len_fid = 0; // for nonlocal fid

          const bool is_oov = (cur_word == 0);
          double p = 0;
          if (cur_word == kSOS_) {
	    //cerr << "NT SOS!" << endl;
            state = ngram_->BeginSentenceState();
            if (has_some_history) {  // this is immediately fully scored, and bad
              p = -100;
              context_complete = true;
            } else {  // this might be a real <s>
              num_scored = max(0, order_ - 2);
            }
          } else {
            const lm::ngram::State scopy(state);
	    lm::FullScoreReturn score = ngram_->FullScore(scopy, cur_word, state);
	    int ngram_len = is_oov ? 0 : score.ngram_length;
	    if(conjoined_nonlocal_fids_.size() > 0) {
	      len_fid = conjoined_nonlocal_fids_.at(ngram_len);
	    }
	    p = score.prob;
	    ///cerr << "LM SCORE: " << (int)ngram_len << " " << p << "; first history word is " << state.history_[0] << "; astate is " << "" /*astate->history_[0]*/ << "; cur_word is " << cur_word << endl;
            if (saw_eos) { p = -100; }
            saw_eos = (cur_word == kEOS_);
	    //if(saw_eos) 	    cerr << "NT EOS!" << endl;
          }
          has_some_history = true;
          ++num_scored;
          if (!context_complete) {
            if (num_scored >= order_) context_complete = true;
          }
          if (context_complete) {
            sum += p;
	    //cerr << "Complete (in nonterm)" << endl;
	    FireConjFeats(features, conj_fid_vec, len_fid, p, false);
            if (oovs && is_oov) (*oovs)++;
          } else {
            if (remnant) {
	      ///cerr << "Storing (nonterm) Word "<<cur_word<<"in remnant at " << num_estimated << endl;
              SetIthUnscoredWord(num_estimated, cur_word, remnant);
	      //cerr << "Storing (nonterm) IthConjVec in remnant at " << num_estimated << endl;
	      SetIthConjVec(num_estimated, conj_fid_vec, remnant);
	    }
            ++num_estimated;
            est_sum += p;
	    //cerr << "Est (in nonterm)" << endl;
	    FireConjFeats(est_features, conj_fid_vec, len_fid, p, true);
            if (est_oovs && is_oov) (*est_oovs)++;
          }
        }
        saw_eos = GetFlag(astate, HAS_EOS_ON_RIGHT);
        if (HasFullContext(astate)) { // this is equivalent to the "star" in Chiang 2007
          state = RemnantLMState(astate);
          context_complete = true;
        }
      } else {   // handle terminal
        const WordID cdec_word_or_class = ClassifyWordIfNecessary(e[j]);  // in future,
                                                                          // maybe handle emission
        const lm::WordIndex cur_word = MapWord(cdec_word_or_class); // map to LM's id

	// determine which conjoined features these lexical items will contribute to
	const SparseVector<double>& rule_feats = rule.GetFeatureValues();
	conj_fid conj_fid_vec[conjoined_fids_.size()];
	std::fill(conj_fid_vec, conj_fid_vec + conjoined_fids_.size(), 0);
	int vec_i = 0;
	for(unsigned i = 0; i<conjoin_with_fids_.size(); ++i) {
	  int conjoin_with_fid = conjoin_with_fids_.at(i);
	  double conjoin_with_value = rule_feats.value(conjoin_with_fid);
	  if(conjoin_with_value != 0.0) {
	    //cerr << "Recognized conjoined fid " << conjoin_with_fid << endl;
	    int conjoined_fid = conjoined_fids_.at(i);
	    conj_fid_vec[vec_i] = conjoined_fid;
	    ++vec_i;
	  }
	}
	int len_fid = 0;

        double p = 0;
        const bool is_oov = (cur_word == 0);
        if (cur_word == kSOS_) {
          state = ngram_->BeginSentenceState();
          if (has_some_history) {  // this is immediately fully scored, and bad
            p = -100;
            context_complete = true;
          } else {  // this might be a real <s>
            num_scored = max(0, order_ - 2);
	    //cerr << "T SOS!" << endl;
          }
        } else {
          const lm::ngram::State scopy(state);
	  lm::FullScoreReturn score = ngram_->FullScore(scopy, cur_word, state);
	  int ngram_len = is_oov ? 0 : score.ngram_length;
	  if(conjoined_nonlocal_fids_.size() > 0) {
	    len_fid = conjoined_nonlocal_fids_.at(ngram_len);
	  }
	  p = score.prob;
	  ///cerr << "LM SCORE (terminal): " << (int)ngram_len << " " << p << endl;
          if (saw_eos) { p = -100; }
          saw_eos = (cur_word == kEOS_);
	  //if(saw_eos) 	    cerr << "T EOS!" << endl;
        }
        has_some_history = true;
        ++num_scored;
        if (!context_complete) {
          if (num_scored >= order_) context_complete = true;
        }
        if (context_complete) {
          sum += p;
	  //cerr << "Complete (in term)" << endl;
	  FireConjFeats(features, conj_fid_vec, len_fid, p, false);
          if (oovs && is_oov) (*oovs)++;
        } else {
          if (remnant) {
            SetIthUnscoredWord(num_estimated, cur_word, remnant);
	    //cerr << "Storing (term) IthConjVec in remnant at " << num_estimated << endl;
	    SetIthConjVec(num_estimated, conj_fid_vec, remnant);
	  }
          ++num_estimated;
          est_sum += p;
	  //cerr << "Est (in nonterm)" << endl;
	  FireConjFeats(est_features, conj_fid_vec, len_fid, p, true);
          if (est_oovs && is_oov) (*est_oovs)++;
        }
      }
    }
    if (remnant) {
      state.ZeroRemaining();
      SetFlag(saw_eos, HAS_EOS_ON_RIGHT, remnant);
      SetRemnantLMState(state, remnant);
      SetUnscoredSize(num_estimated, remnant);
      SetHasFullContext(context_complete || (num_scored >= order_), remnant);
    }

    if(enable_main_feat_) {
      // are we still using the single, main LM feat?
      // we might want to disable this when using conjoined features
      features->set_value(fid_, sum);
      if(est_features) {
	est_features->set_value(fid_, est_sum);
      }
    }
  }

  // this assumes no target words on final unary -> goal rule.  is that ok?
  // for <s> (n-1 left words) and (n-1 right words) </s>
  void FinalTraversalCost(const void* state, SparseVector<double>* features,
			  double* oovs) {

    // Since this function no longer returns the lm cost,
    // we must directly add it to the affected conjoined features
    // TODO: Even if we have enable_main_feat_ == false, we still add final
    // transition cost there, since we don't know where else to add it.

    if (add_sos_eos_) {  // rules do not produce <s> </s>, so do it here

      // dummy_state_ introduces <s> as a LM state
      SetRemnantLMState(ngram_->BeginSentenceState(), dummy_state_);
      SetHasFullContext(1, dummy_state_);
      SetUnscoredSize(0, dummy_state_);

      // actual sentence replaces middle element of dummy rule via LM state
      dummy_ants_[1] = state; // dummy ants represents the rule "<s> state </s>"
      *oovs = 0;

      // dummy_rule_ adds </s> as a real terminal
      ///cerr << "Looking up words for final transition" << endl;
      ///cerr << "History is " << RemnantLMState(state).history_[0] << endl;


      /*
      cerr << "FINAL TRAVERSAL" << endl;
      const vector<WordID>& e = dummy_rule_->e();
      for (int j = 0; j < e.size(); ++j) {
	cerr << "TARGET POS " << j << endl;
	if (e[j] < 1) {   // handle non-terminal substitution
	  const void* astate = (dummy_ants_[-e[j]]);
	  int unscored_ant_len = UnscoredSize(astate);
	  cerr << "HAZ " << unscored_ant_len << " UNSCORED" << endl;
	  for (int k = 0; k < unscored_ant_len; ++k) {
	    const lm::WordIndex cur_word = IthUnscoredWord(k, astate);
	    cerr << "CUR_WORD " << cur_word << endl;
	  }
	}
      }
      */




      LookupWords(*dummy_rule_, dummy_ants_, features, NULL, oovs, NULL, NULL);

    } else {  // rules DO produce <s> ... </s>
      double p = 0;
      if (!GetFlag(state, HAS_EOS_ON_RIGHT)) { p -= 100; } // TODO: Tune another feature for this?
      if (UnscoredSize(state) > 0) {  // are there unscored words
        if (kSOS_ != IthUnscoredWord(0, state)) {
          p -= 100 * UnscoredSize(state);
        }
      }
      features->set_value(fid_, p);
    }
  }

  // if this is not a class-based LM, returns w untransformed,
  // otherwise returns a word class mapping of w,
  // returns TD::Convert("<unk>") if there is no mapping for w
  WordID ClassifyWordIfNecessary(WordID w) const {
    if (word2class_map_.empty()) return w;
    if (w >= word2class_map_.size())
      return kCDEC_UNK;
    else
      return word2class_map_[w];
  }

  // converts to cdec word id's to KenLM's id space, OOVs and <unk> end up at 0
  lm::WordIndex MapWord(WordID w) const {
    if (w >= cdec2klm_map_.size()) {
      return 0;
    } else {
      lm::WordIndex wid = cdec2klm_map_[w];
      ///cerr << "Word " << TD::Convert(w) << " has LM ID " << wid << endl;
      return wid;
    }
  }

 public:
  JLanguageModelImpl(const string& filename,
                     const string& mapfile,
                     bool explicit_markers,
		     int fid,
		     const string& featname,
		     const vector<int>& conjoin_with_fids,
		     const vector<int>& conjoined_fids,
		     bool conjoin_with_length,
		     bool enable_main_feat,
		     bool disc_mode,
		     string count_bin_file,
		     string context_bin_file) :
      kCDEC_UNK(TD::Convert("<unk>")) ,
      add_sos_eos_(!explicit_markers),
      fid_(fid),
      enable_main_feat_(enable_main_feat),
      disc_mode_(disc_mode) {
    {
      VMapper vm(&cdec2klm_map_);
      lm::ngram::Config conf;
      conf.enumerate_vocab = &vm;
      ngram_ = new Model(filename.c_str(), conf);
    }
    order_ = ngram_->Order();
    cerr << "Loaded " << order_ << "-gram KLM from " << filename << " (MapSize=" << cdec2klm_map_.size() << ")\n";
    
    if(disc_mode) {
    	// precompute features for discriminative language model
    	vector<string> count_bin_names;
    	vector<string> context_bin_names;
    	load_file_into_vec(count_bin_file, &count_bin_names);
    	load_file_into_vec(context_bin_file, &context_bin_names);

    	// TODO: Move to inst variable
    	disc_feats_.reserve(order_+1);
    	disc_feats_.resize(order_+1);
    	for(int match_len=0; match_len<=order_; ++match_len) {
    		vector<vector<vector<int> > >& match_len_vec = disc_feats_.at(match_len);

    		if(match_len == 0) {
    			// there is no count bin -- we've never seen this token
    			const string first_name = featname + "_UNK";
    			match_len_vec.resize(1);
    			vector<vector<int> >& count_bin_zero = match_len_vec.at(0); // use 0th count bin
    			count_bin_zero.resize(1);
    			spawn_disc_feats(first_name, context_bin_names, &count_bin_zero);
    		} else {
        		match_len_vec.resize(count_bin_names.size());
				for(int iCountBin=0; iCountBin < count_bin_names.size(); ++iCountBin) {
					const string& count_bin_name = count_bin_names.at(iCountBin);
					const string first_name = featname + "_Len" + boost::lexical_cast<std::string>(match_len) + "_Count" + count_bin_name;

	    			vector<vector<int> >& count_bin_i = match_len_vec.at(iCountBin); // use ith count bin
	    			count_bin_i.resize(order_);

					// if we matched the entire LM window, there is no context in scope
	    			// so skip adding a context length and count
		    		if(match_len != order_) {
		    			spawn_disc_feats(first_name, context_bin_names, &count_bin_i);
		    		} else {
		    			vector<int>& context_len_zero = count_bin_i.at(0); // pretend context len is zero
		    			context_len_zero.push_back(FD::Convert(first_name)); // 0th bin of zero len context is just this feature
		    		}
				}
    		}
    	}
    }

    if(conjoin_with_length) {
      for(unsigned i=0; i<=order_; ++i) {
	const string conjoined_name = featname + "__X__LMNgramLen" + boost::lexical_cast<std::string>(i);
	int conjoined_fid = FD::Convert(conjoined_name);
	cerr << "Using Nonlocal_X_Nonlocal Conjoined Feature Pattern: " << conjoined_name << " (conjoining with LM match length to make fid " << conjoined_fid << ")" << endl;
	conjoined_nonlocal_fids_.push_back(conjoined_fid);
      }
    }

    // TODO: Replace with a vector<bool>, dynamic_bitset<bool>, or use on-the-fly shifts
    conjoin_with_fids_ = conjoin_with_fids;
    conjoined_fids_ = conjoined_fids;
    conj_vec_size_ = conjoined_fids.size() * sizeof(conj_fid);
    state_size_ = ngram_->StateSize() // KenLM state
      + 2 // unscored_size and is_complete
      + (order_ - 1) * sizeof(lm::WordIndex) // unscored_words: words that might participate in context in the future
      + (order_ - 1) * conj_vec_size_; // vectors (one per unscored word) of conjoined fids that receive this LM score
    unscored_size_offset_ = ngram_->StateSize();
    is_complete_offset_ = unscored_size_offset_ + 1;
    unscored_words_offset_ = is_complete_offset_ + 1;
    conjoined_feats_offset_ = unscored_words_offset_ + (order_ - 1) * sizeof(lm::WordIndex);
    assert(conjoined_feats_offset_ + (order_ - 1) * conj_vec_size_ == state_size_);

    cerr << "LM state size is " << state_size_ << " bytes" << endl;

    // special handling of beginning / ending sentence markers
    dummy_state_ = new char[state_size_];
    memset(dummy_state_, 0, state_size_);
    // this will get written with the BOS LM state
    dummy_ants_.push_back(dummy_state_);
    // this will get overwritten for each true antecedent
    dummy_ants_.push_back(NULL);
    dummy_rule_.reset(new TRule("[DUMMY] ||| [BOS] [DUMMY] ||| [1] [2] </s> ||| X=0"));
    kSOS_ = MapWord(TD::Convert("<s>"));
    assert(kSOS_ > 0);
    kEOS_ = MapWord(TD::Convert("</s>"));
    assert(kEOS_ > 0);
    assert(MapWord(kCDEC_UNK) == 0); // KenLM invariant

    // handle class-based LMs (unambiguous word->class mapping reqd.)
    if (mapfile.size())
      LoadWordClasses(mapfile);
  }

  void spawn_disc_feats(const string& first_name, const vector<string>& context_bin_names, vector<vector<int> >* context_disc_feats) {

		// TODO: context_len=0 is special case -- we will have no count bin
		// we will never have a context of size order_ since the match will have used >= 1 token
		for(int context_len=0; context_len<order_; ++context_len) {
		  vector<int>& context_vec = context_disc_feats->at(context_len);

		  if(context_len == 0) {
			// there is no count bin -- we've never seen this context token
			const string disc_feat_name = first_name + "_ConUNK";
			context_vec.push_back(FD::Convert(disc_feat_name));
		  } else {
			context_vec.reserve(context_bin_names.size());
			for(int iContextBin=1; iContextBin < context_bin_names.size(); ++iContextBin) {
				const string& context_bin_name = context_bin_names.at(iContextBin);
				const string disc_feat_name = first_name
						+ "_PrevLen" + boost::lexical_cast<std::string>(context_len) + "_PrevConCount" + context_bin_name;
				context_vec.push_back(FD::Convert(disc_feat_name));
			}
		  }
		}
  }

  void LoadWordClasses(const string& file) {
    ReadFile rf(file);
    istream& in = *rf.stream();
    string line;
    vector<WordID> dummy;
    int lc = 0;
    cerr << "  Loading word classes from " << file << " ...\n";
    AddWordToClassMapping_(TD::Convert("<s>"), TD::Convert("<s>"));
    AddWordToClassMapping_(TD::Convert("</s>"), TD::Convert("</s>"));
    while(in) {
      getline(in, line);
      if (!in) continue;
      dummy.clear();
      TD::ConvertSentence(line, &dummy);
      ++lc;
      if (dummy.size() != 2) {
        cerr << "    Format error in " << file << ", line " << lc << ": " << line << endl;
        abort();
      }
      AddWordToClassMapping_(dummy[0], dummy[1]);
    }
  }

  void AddWordToClassMapping_(WordID word, WordID cls) {
    if (word2class_map_.size() <= word) {
      word2class_map_.resize((word + 10) * 1.1, kCDEC_UNK);
      assert(word2class_map_.size() > word);
    }
    if(word2class_map_[word] != kCDEC_UNK) {
      cerr << "Multiple classes for symbol " << TD::Convert(word) << endl;
      abort();
    }
    word2class_map_[word] = cls;
  }

  ~JLanguageModelImpl() {
    delete ngram_;
    delete[] dummy_state_;
  }

  int ReserveStateSize() const { return state_size_; }

 private:
  const WordID kCDEC_UNK;
  lm::WordIndex kSOS_;  // <s> - requires special handling.
  lm::WordIndex kEOS_;  // </s>
  Model* ngram_;
  boost::unordered_map<uint64_t, vector<int> > disc_feats_;
  boost::unordered_map<uint64_t, vector<int> > disc_backoff_feats_;
  const bool add_sos_eos_; // flag indicating whether the hypergraph produces <s> and </s>
                     // if this is true, FinalTransitionFeatures will "add" <s> and </s>
                     // if false, FinalTransitionFeatures will score anything with the
                     // markers in the right place (i.e., the beginning and end of
                     // the sentence) with 0, and anything else with -100

  int order_;
  int state_size_;
  int unscored_size_offset_;
  int is_complete_offset_;
  int unscored_words_offset_;
  int conjoined_feats_offset_;
  char* dummy_state_;
  vector<const void*> dummy_ants_;
  vector<lm::WordIndex> cdec2klm_map_;
  vector<WordID> word2class_map_;        // if this is a class-based LM, this is the word->class mapping
  TRulePtr dummy_rule_;
  int fid_;
  
  // are we putting all probability in the main feature? (possibly in addition to conjoined features)
  // we might want to disable this and only put it in the conjunctive feature bins to decorrelate features
  bool enable_main_feat_;

  vector<int> conjoin_with_fids_;
  vector<int> conjoined_fids_;
  vector<int> conjoined_nonlocal_fids_;
  int conj_vec_size_;

  // for the discriminative LM
  bool disc_mode_;
  vector< vector< vector< vector<int> > > > disc_feats_;
};

template <class Model>
JLanguageModel<Model>::JLanguageModel(const string& param) {
  string filename, mapfile, featname;
  bool explicit_markers;
  vector<int> conjoin_with_fids;
  vector<int> conjoined_fids;
  bool conjoin_with_length;
  bool enable_main_feat;
  bool disc_mode;
  string count_bin_file;
  string context_bin_file;
  if (!ParseLMArgs(param, &filename, &mapfile, &explicit_markers, &featname, &conjoin_with_fids, &conjoined_fids, &conjoin_with_length,
		  	  &enable_main_feat, &disc_mode, &count_bin_file, &context_bin_file)) {
    abort();
  }

  fid_ = FD::Convert(featname);

  try {
    pimpl_ = new JLanguageModelImpl<Model>(filename, mapfile, explicit_markers, fid_, featname, conjoin_with_fids, conjoined_fids, conjoin_with_length,
    		enable_main_feat, disc_mode, count_bin_file, context_bin_file);
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    abort();
  }

  SetStateSize(pimpl_->ReserveStateSize());
}

template <class Model>
Features JLanguageModel<Model>::features() const {
  /*
  Features vec;
  vec.push_back(fid_);
  vec.push_back(oov_fid_);
  vec.insert(vec.end(), conjoined_fids_.begin(), conjoined_fids_.end());
  return vec;
  */
  return single_feature(fid_);
}

template <class Model>
JLanguageModel<Model>::~JLanguageModel() {
  delete pimpl_;
}

template <class Model>
void JLanguageModel<Model>::TraversalFeaturesImpl(const SentenceMetadata& smeta,
                                          const Hypergraph::Edge& edge,
                                          const vector<const void*>& ant_states,
                                          SparseVector<double>* features,
                                          SparseVector<double>* estimated_features,
                                          void* state) const {
  double oovs = 0;
  double est_oovs = 0;
  pimpl_->LookupWords(*edge.rule_, ant_states,
		      features, estimated_features,
		      &oovs, &est_oovs, state);

  // don't conjoin the OOVs... for now
//  if (oov_fid_) {
//    if (oovs) features->set_value(oov_fid_, oovs);
//    if (est_oovs) estimated_features->set_value(oov_fid_, est_oovs);
//  }
}

template <class Model>
void JLanguageModel<Model>::FinalTraversalFeatures(const void* ant_state,
                                           SparseVector<double>* features) const {
  double oovs = 0;
  pimpl_->FinalTraversalCost(ant_state, features, &oovs);

//  if (oov_fid_ && oovs)
//    features->set_value(oov_fid_, oovs);
}

template <class Model> boost::shared_ptr<FeatureFunction> CreateModel(const std::string &param) {
  JLanguageModel<Model> *ret = new JLanguageModel<Model>(param);
  ret->Init();
  return boost::shared_ptr<FeatureFunction>(ret);
}

boost::shared_ptr<FeatureFunction> JLanguageModelFactory::Create(std::string param) const {
  using namespace lm::ngram;
  std::string filename, ignored_map;
  bool ignored_markers;
  std::string ignored_featname;
  vector<int> conjoin_with_fids; // unused
  vector<int> conjoined_fids; // unused
  bool conjoin_with_length; // unused
  bool enable_main_feat; // unused
  bool disc_mode; // unused
  string count_bin_file; // unused
  string context_bin_file; // unused
  ParseLMArgs(param, &filename, &ignored_map, &ignored_markers, &ignored_featname, &conjoin_with_fids, &conjoined_fids, &conjoin_with_length,
		  &enable_main_feat, &disc_mode, &count_bin_file, &context_bin_file);
  ModelType m;
  if (!RecognizeBinary(filename.c_str(), m)) m = HASH_PROBING;

  switch (m) {
    case HASH_PROBING:
      return CreateModel<ProbingModel>(param);
    case TRIE_SORTED:
      return CreateModel<TrieModel>(param);
    case ARRAY_TRIE_SORTED:
      return CreateModel<ArrayTrieModel>(param);
    case QUANT_TRIE_SORTED:
      return CreateModel<QuantTrieModel>(param);
    case QUANT_ARRAY_TRIE_SORTED:
      return CreateModel<QuantArrayTrieModel>(param);
    default:
      UTIL_THROW(util::Exception, "Unrecognized kenlm binary file type " << (unsigned)m);
  }
}

std::string  JLanguageModelFactory::usage(bool params,bool verbose) const {
  return JLanguageModel<lm::ngram::Model>::usage(params, verbose);
}
