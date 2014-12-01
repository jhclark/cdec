#include "ff_ngrams.h"

#include <cstring>
#include <iostream>
#include <set>

#include <boost/scoped_ptr.hpp>

#include "filelib.h"
#include "stringlib.h"
#include "hg.h"
#include "tdict.h"
#include "sentence_metadata.h"

using namespace std;

static const unsigned char HAS_FULL_CONTEXT = 1;
static const unsigned char HAS_EOS_ON_RIGHT = 2;
static const unsigned char MASK             = 7;

static const bool DEBUG_FF_NGRAMS = false;

namespace {
// represents either the left or right side of the chart LM state
// and never includes either the number of unscored words nor "has full context"
template <unsigned MAX_ORDER = 5>
struct State {
  explicit State() {
    memset(state, 0, sizeof(state));
  }
  explicit State(int order) {
    assert(order <= MAX_ORDER);
    memset(state, 0, (order-1) * sizeof(WordID));
  }
  State<MAX_ORDER>(char order, const WordID* mem) {
    assert(order <= MAX_ORDER);
    memcpy(state, mem, (order-1) * sizeof(WordID));
  }
  State(const State<MAX_ORDER>& other) {
    memcpy(state, other.state, sizeof(state));
  }
  const State& operator=(const State<MAX_ORDER>& other) {
    memcpy(state, other.state, sizeof(state));
  }
  explicit State(const State<MAX_ORDER>& other, unsigned order, WordID extend) {
    assert(order <= MAX_ORDER);
    size_t om1 = order - 1;
    assert(om1 > 0);
    for (size_t i = 1; i < order; ++i)
      state[i - 1] = other.state[i];
    state[order - 1] = extend;
  }
  const WordID& operator[](size_t i) const { return state[i]; }
  WordID& operator[](size_t i) { return state[i]; }

  // all data members:
  WordID state[MAX_ORDER - 1];
};
}

namespace {
  string Escape(const string& x) {
    string y = x;
    for (int i = 0; i < y.size(); ++i) {
      if (y[i] == '=') y[i]='_';
      if (y[i] == ';') y[i]='_';
    }
    return y;
  }
}

// -x : Grammar uses explicit SOS and BOS markers?
// -o : What order ngrams should be emitted?
// -F : ngram file, with list of ngrams that should be included
// NOTE : observable features must be annotated on source sentence using SGML will be conjoined
static bool ParseArgs(string const& in, bool* explicit_markers, unsigned* order, string* ngram_file) {
  vector<string> const& argv=SplitOnWhitespace(in);
  *explicit_markers = false;
  *order = 3;
  *ngram_file = "";
#define LMSPEC_NEXTARG if (i==argv.end()) {            \
    cerr << "Missing argument for "<<*last<<". "; goto usage; \
    } else { ++i; }

  for (vector<string>::const_iterator last,i=argv.begin(),e=argv.end();i!=e;++i) {
    string const& s=*i;
    if (s[0]=='-') {
      if (s.size()>2) goto fail;
      switch (s[1]) {
      case 'x':
        *explicit_markers = true;
        break;
      case 'o':
        LMSPEC_NEXTARG; *order=atoi((*i).c_str());
        break;
      case 'F':
        LMSPEC_NEXTARG; *ngram_file=*i;
        break;
#undef LMSPEC_NEXTARG
      default:
      fail:
        cerr<<"Unknown option on NgramFeatures "<<s<<" ; ";
        goto usage;
      }
    }
  }
  return true;
usage:
  cerr << "NgramFeatures is incorrect!\n";
  return false;
}

class NgramDetectorImpl {

  // returns the number of unscored words at the left edge of a span
  inline int UnscoredSize(const void* state) const {
    return *(static_cast<const char*>(state) + unscored_size_offset_);
  }

  inline void SetUnscoredSize(int size, void* state) const {
    *(static_cast<char*>(state) + unscored_size_offset_) = size;
  }

  // get the left size of the chart state
  inline State<5> RemnantLMState(const void* cstate) const {
    return State<5>(order_, static_cast<const WordID*>(cstate));
  }

  inline const State<5> BeginSentenceState() const {
    State<5> state(order_);
    state.state[order_-1] = kSOS_;
    return state;
  }

  // sets the *left* side of the antecedent state
  inline void SetRemnantLMState(const State<5>& lmstate, void* state) const {
    // if we were clever, we could use the memory pointed to by state to do all
    // the work, avoiding this copy
    memcpy(state, lmstate.state, (order_-1) * sizeof(WordID));
  }

  WordID IthUnscoredWord(int i, const void* state) const {
    const WordID* const mem = reinterpret_cast<const WordID*>(static_cast<const char*>(state) + unscored_words_offset_);
    return mem[i];
  }

  void SetIthUnscoredWord(int i, const WordID index, void *state) const {
    WordID* mem = reinterpret_cast<WordID*>(static_cast<char*>(state) + unscored_words_offset_);
    mem[i] = index;
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

  inline int GetFeatureID(WordID* buf, int n) const {
    const char* code="_UBT456789"; // prefix code (unigram, bigram, etc.)
    ostringstream os;
    os << code[n] << ':';
    for (int i = n-1; i >= 0; --i) {
      os << (i != n-1 ? "_" : "");
      const string& tok = TD::Convert(buf[i]);
      if (tok.find('=') == string::npos)
        os << tok;
      else
        os << Escape(tok);
    }
    string feat_name = os.str();
    if (DEBUG_FF_NGRAMS) std::cerr << "Generating feature ID for: " << feat_name << std::endl;
    int fid = FD::Convert(feat_name);
    return fid;
  }

  void PrintState(const State<5>& state) const {
    if (DEBUG_FF_NGRAMS) {
      std::cerr << "State =";
      for (int i = 0; i < 4; i++) {
        if (state[i] >= 0 && state[i] <= TD::NumWords())
          std::cerr << " " << TD::Convert(state[i]);
        else
          std::cerr << " " << state[i] << "/" << TD::NumWords();
      }
      std::cerr << std::endl;
    }
  }

  void FireFeatures(const State<5>& state, WordID cur, SparseVector<double>* feats) {
    FidTree* ft = &fidroot_;
    int n = 0;
    WordID buf[10];
    // read n-gram in reverse order, starting at position order_-1
    // (note: this gets decremented before use)
    int ci = order_;
    WordID curword = cur;
    if (DEBUG_FF_NGRAMS) std::cerr << "Firing features: "; PrintState(state);

    // first iteration: query "cur" as "curword" for a unigram
    // following iterations: go right to left, expanding to older n-gram contexts
    //   to produce additional n-grams, using elements of "state"
    // ft is a trie that caches stringified features names. from the root, we store n-grams in right-to-left order
    //   (with the most recent word nearest the root of the trie)
    // the "buf" array gets populated from left to right, with the n-gram in reverse order
    //   buf only gets used if we haven't cached a stringified feature name for this n-gram yet
    while(curword) {
      buf[n] = curword;
      if (DEBUG_FF_NGRAMS) std::cerr << "Firing features. Curword = " << TD::Convert(curword) << std::endl;
      int& fid = ft->fids[curword];
      ++n;
      if (!fid) {
        fid = GetFeatureID(buf, n);
      }
      ft = &ft->levels[curword];
      --ci;
      if (ci < 0) break;
      curword = state[ci];

      if(allowed_feats_.empty() || allowed_feats_.find(fid) != allowed_feats_.end()) {
        feats->set_value(fid, 1);
        for(int i=0; i<obs_feats_.size(); ++i) {
          int obs_fid = obs_feats_.at(i);
          int& conj_fid = conj_cache_[pair<int,int>(fid, obs_fid)];
          if(!conj_fid) {
            string conj_feat = FD::Convert(obs_fid) + "_" + FD::Convert(fid);
            conj_fid = FD::Convert(conj_feat);
          }
          feats->set_value(conj_fid, 1);
        }
      }
    }
  }

 public:
  void PrepareForInput(const SentenceMetadata& smeta) {
    obs_feats_.clear();

    // get the space-delimited list of features    
    string value = smeta.GetSGMLValue("features");
    vector<string> feats;
    Tokenize(value, ' ', &feats);
    cerr << "Found " << feats.size() << " SGML observable features: " << value << endl;
    for(int i=0; i<feats.size(); ++i) {
      obs_feats_.push_back(FD::Convert(feats.at(i)));
    }
  }

  void LookupWords(const TRule& rule, const vector<const void*>& ant_states, SparseVector<double>* feats, SparseVector<double>* est_feats, void* remnant) {
    double sum = 0.0;
    double est_sum = 0.0;
    int num_scored = 0;
    int num_estimated = 0;
    bool saw_eos = false;
    bool has_some_history = false;

    // used to hold the current n-gram to be fired
    State<5> state;
    const vector<WordID>& e = rule.e();
    bool context_complete = false;
    if (DEBUG_FF_NGRAMS) std::cerr << "---Lookup words---" << std::endl;
    for (int j = 0; j < e.size(); ++j) {

      if (DEBUG_FF_NGRAMS) std::cerr << "State at top of loop: "; PrintState(state);

      if (e[j] < 1) {   // handle non-terminal substitution
        if (DEBUG_FF_NGRAMS) std::cerr << "j=" << j << "; NONT #" << -e[j] << std::endl;

        // holds the full antecedent state (including left/right words plus flags)
        const void* astate = (ant_states[-e[j]]);
        int unscored_ant_len = UnscoredSize(astate);
        for (int k = 0; k < unscored_ant_len; ++k) {
          // get the ith unscored (on the right side of the antecedent) word
          const WordID cur_word = IthUnscoredWord(k, astate);
          const bool is_oov = (cur_word == 0);
          SparseVector<double> p;
          if (DEBUG_FF_NGRAMS) std::cerr << "k=" << k << "; cur_word = " << TD::Convert(cur_word) << std::endl;
          if (cur_word == kSOS_) {
            if (DEBUG_FF_NGRAMS) std::cerr << "NONTERM ANTECEDENT Saw SOS" << std::endl;
            state = BeginSentenceState();
            if (DEBUG_FF_NGRAMS) std::cerr << "Set state to: "; PrintState(state);
            if (has_some_history) {  // this is immediately fully scored, and bad
              p.set_value(FD::Convert("Malformed"), 1.0);
              context_complete = true;
            } else {  // this might be a real <s>
              num_scored = max(0, order_ - 2);
            }
          } else {
            if (DEBUG_FF_NGRAMS) std::cerr << "NONTERM ANTECEDENT Not SOS" << std::endl;
            FireFeatures(state, cur_word, &p);
            const State<5> scopy = State<5>(state, order_, cur_word);
            if (DEBUG_FF_NGRAMS) std::cerr << "State before copy: "; PrintState(state);
            state = scopy;
            if (DEBUG_FF_NGRAMS) std::cerr << "State after copy: "; PrintState(state);
            //std::cerr << "NONTERM ANTECEDENT Not SOS -- fire feats again TODO REMOVE ME" << std::endl;
            //FireFeatures(state, cur_word, &p);
            if (saw_eos) { p.set_value(FD::Convert("Malformed"), 1.0); }
            saw_eos = (cur_word == kEOS_);
          }
          has_some_history = true;
          ++num_scored;
          if (!context_complete) {
            if (num_scored >= order_) context_complete = true;
          }
          if (context_complete) {
            (*feats) += p;
          } else {
            if (remnant)
              SetIthUnscoredWord(num_estimated, cur_word, remnant);
            ++num_estimated;
            if (est_feats)
              (*est_feats) += p;
          }
        }
        saw_eos = GetFlag(astate, HAS_EOS_ON_RIGHT);
        if (HasFullContext(astate)) { // this is equivalent to the "star" in Chiang 2007
          // grab the left side of the antecedent state
          state = RemnantLMState(astate);
          if (DEBUG_FF_NGRAMS) std::cerr << "Set state to antecedant remnant: "; PrintState(state);
          context_complete = true;
        }
      } else {   // handle terminal
        if (DEBUG_FF_NGRAMS) std::cerr << "j=" << j << "; TERM" << std::endl;
        const WordID cur_word = e[j];
        SparseVector<double> p;
        if (cur_word == kSOS_) {
          if (DEBUG_FF_NGRAMS) std::cerr << "Saw SOS" << std::endl;
          state = BeginSentenceState();
          if (has_some_history) {  // this is immediately fully scored, and bad
            p.set_value(FD::Convert("Malformed"), -100);
            context_complete = true;
          } else {  // this might be a real <s>
            num_scored = max(0, order_ - 2);
          }
          // TODO: Remove me?
          //FireFeatures(state, cur_word, &p);
        } else {
          if (DEBUG_FF_NGRAMS) std::cerr << "Not SOS" << std::endl;
          FireFeatures(state, cur_word, &p);
          const State<5> scopy = State<5>(state, order_, cur_word);
          if (DEBUG_FF_NGRAMS) std::cerr << "State before copy: "; PrintState(state);
          state = scopy;
          if (DEBUG_FF_NGRAMS) std::cerr << "State after copy: "; PrintState(state);
          if (saw_eos) { p.set_value(FD::Convert("Malformed"), 1.0); }
          saw_eos = (cur_word == kEOS_);
        }
        has_some_history = true;
        ++num_scored;
        if (!context_complete) {
          if (num_scored >= order_)
            context_complete = true;
        }
        if (context_complete) {
          (*feats) += p;
        } else {
          if (remnant)
            SetIthUnscoredWord(num_estimated, cur_word, remnant);
          ++num_estimated;
          if (est_feats)
            (*est_feats) += p;
        }
      }
    }
    if (remnant) {
      SetFlag(saw_eos, HAS_EOS_ON_RIGHT, remnant);
      SetRemnantLMState(state, remnant);
      SetUnscoredSize(num_estimated, remnant);
      SetHasFullContext(context_complete || (num_scored >= order_), remnant);
    }
  }

  // this assumes no target words on final unary -> goal rule.  is that ok?
  // for <s> (n-1 left words) and (n-1 right words) </s>
  void FinalTraversal(const void* state, SparseVector<double>* feats) {
    if (DEBUG_FF_NGRAMS) std::cerr << "===FINAL TRAVERSAL===" << std::endl;
    if (add_sos_eos_) {  // rules do not produce <s> </s>, so do it here
      if (DEBUG_FF_NGRAMS) std::cerr << "FINAL TRAVERSAL ADD SOS/BOS" << std::endl;
      //SetRemnantLMState(BeginSentenceState(), dummy_state_);
      SetIthUnscoredWord(0, kSOS_, dummy_state_);
      SetUnscoredSize(1, dummy_state_);
      SetHasFullContext(false, dummy_state_);
      dummy_ants_[1] = state;
      LookupWords(*dummy_rule_, dummy_ants_, feats, NULL, NULL);
    } else {  // rules DO produce <s> ... </s>
#if 0
      double p = 0;
      if (!GetFlag(state, HAS_EOS_ON_RIGHT)) { p -= 100; }
      if (UnscoredSize(state) > 0) {  // are there unscored words
        if (kSOS_ != IthUnscoredWord(0, state)) {
          p -= 100 * UnscoredSize(state);
        }
      }
      return p;
#endif
    }
  }

 public:
  explicit NgramDetectorImpl(bool explicit_markers, unsigned order, const string& ngram_file) :
      kCDEC_UNK(TD::Convert("<unk>")) ,
      add_sos_eos_(!explicit_markers) {
    order_ = order;
    state_size_ = (order_ - 1) * sizeof(WordID) + 2 + (order_ - 1) * sizeof(WordID);
    unscored_size_offset_ = (order_ - 1) * sizeof(WordID);
    is_complete_offset_ = unscored_size_offset_ + 1;
    unscored_words_offset_ = is_complete_offset_ + 1;

    // special handling of beginning / ending sentence markers
    dummy_state_ = new char[state_size_];
    memset(dummy_state_, 0, state_size_);
    dummy_ants_.push_back(dummy_state_);
    dummy_ants_.push_back(NULL);
    dummy_rule_.reset(new TRule("[DUMMY] ||| [BOS] [DUMMY] ||| [1] [2] </s> ||| X=0"));
    kSOS_ = TD::Convert("<s>");
    kEOS_ = TD::Convert("</s>");
    if (DEBUG_FF_NGRAMS) std::cerr << "SOS = " << kSOS_ << std::endl;
    if (DEBUG_FF_NGRAMS) std::cerr << "EOS = " << kEOS_ << std::endl;

    if(!ngram_file.empty()) {
      ReadFile in_read(ngram_file);
      istream & in = *in_read.stream();
      string line;
      while (getline(in, line)) {
        if (!line.empty()) {
          vector<string> toks;
          Tokenize(line, ' ', &toks);
          int buf[10];
          for(int i=0; i<toks.size(); ++i) {
            buf[toks.size()-i-1] = TD::Convert(toks.at(i));
          }
          int fid = GetFeatureID(buf, toks.size());
          if (DEBUG_FF_NGRAMS) cerr << "Allowed " << FD::Convert(fid) << endl;
          allowed_feats_.insert(fid);
        }
      }
      cerr << "NgramDetector found " << allowed_feats_.size() << " allowable n-gram features" << endl;
    }
  }

  ~NgramDetectorImpl() {
    delete[] dummy_state_;
  }

  int ReserveStateSize() const { return state_size_; }

 private:
  const WordID kCDEC_UNK;
  WordID kSOS_;  // <s> - requires special handling.
  WordID kEOS_;  // </s>
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
  char* dummy_state_;
  vector<const void*> dummy_ants_;
  TRulePtr dummy_rule_;
  struct FidTree {
    map<WordID, int> fids;
    map<WordID, FidTree> levels;
  };
  mutable FidTree fidroot_;

  vector<int> obs_feats_;
  set<int> allowed_feats_; // in case we want to take the top N ngrams, etc.
  map<pair<int, int> , int> conj_cache_;
};

NgramDetector::NgramDetector(const string& param) {
  string filename;
  bool explicit_markers = false;
  unsigned order = 3;
  string ngram_file;
  ParseArgs(param, &explicit_markers, &order, &ngram_file);
  pimpl_ = new NgramDetectorImpl(explicit_markers, order, ngram_file);
  SetStateSize(pimpl_->ReserveStateSize());
}

NgramDetector::~NgramDetector() {
  delete pimpl_;
}

void NgramDetector::TraversalFeaturesImpl(const SentenceMetadata& /* smeta */,
                                          const Hypergraph::Edge& edge,
                                          const vector<const void*>& ant_states,
                                          SparseVector<double>* features,
                                          SparseVector<double>* estimated_features,
                                          void* state) const {
  pimpl_->LookupWords(*edge.rule_, ant_states, features, estimated_features, state);
}

void NgramDetector::FinalTraversalFeatures(const void* ant_state,
                                           SparseVector<double>* features) const {
  pimpl_->FinalTraversal(ant_state, features);
}

void NgramDetector::PrepareForInput(const SentenceMetadata& smeta) {
  pimpl_->PrepareForInput(smeta);
}
