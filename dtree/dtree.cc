#include "dtree_split.h"
#include "dtree_merge.h"
#include "weights.h"

#include <boost/tokenizer.hpp>
typedef boost::tokenizer<boost::char_separator<char> > Tokenizer;

void ParseOpts(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
    ("loss_function,l",po::value<string>(), "Loss function being optimized")
    ("src_sents,s",po::value<string>(), "Source sentences (that we will be asking questions about)")
    ("src_vocab,v",po::value<string>(), "Source vocabulary")
    ("err_surface,e",po::value<string>(), "Directory containing error surfaces for each sentence")
    ("mode,M",po::value<string>(), "Optimization mode. One of: split, merge")
    ("min_sents,m",po::value<unsigned>(), "Minimum sentences per decision tree node (for split optimizer)")
    ("beam_size,b",po::value<unsigned>(), "How many best clusterings should we keep around? (for merge optimizer)")
    ("clusters,c",po::value<unsigned>(), "What is the desired number of output clusters? (for merge optimizer)")
    ("help,h", "Help");

  po::options_description dcmdline_options;
  dcmdline_options.add(opts);
  po::store(parse_command_line(argc, argv, dcmdline_options), *conf);
  bool flag = conf->count("loss_function") == 0;
  if (flag || conf->count("help")) {
    cerr << dcmdline_options << endl;
    exit(1);
  }
}


void LoadSents(const string& file,
	       vector<DTSent>* sents) {

  ReadFile rf(file);
  istream& in = *rf.stream();
  unsigned id = 0;
  while(in) {
    string line;
    getline(in, line);
    if (line.empty() && !in) break;
    
    sents->resize(sents->size()+1);
    DTSent& sent = sents->back();
    sent.id = id;
    ProcessAndStripSGML(&line, &sent.sgml);
    TD::ConvertSentence(line, &sent.src);
    
    ++id;
  }
}

// TODO: Factor out
// O(n) -- use only on small vectors
// returns -1 if not found
template <typename T>
size_t IndexOf(const vector<T>& vec, const T& elem) {
  for(size_t i=0; i<vec.size(); ++i)
    if(vec.at(i) == elem)
      return i;
  return -1;
}

void LoadErrSurfaces(const string& file,
		     const ScoreType& type,
		     SparseVector<double>* origin,
		     vector<SparseVector<double> >* dirs,
		     vector<DirErrorSurface>* sent_surfs) {

  ReadFile rf(file);
  istream& in = *rf.stream();
  while(in) {
    string line;
    getline(in, line);
    if (line.empty()) continue;

    boost::char_separator<char> kvsep("\t");
    Tokenizer kvtok(line, kvsep);
    Tokenizer::iterator kvit=kvtok.begin();
    const string key = *kvit++;
    assert(kvit != kvtok.end());
    const string val = *kvit++;
    assert(kvit == kvtok.end());

    // value has "sent_id b64_str"
    boost::char_separator<char> valsep(" ");
    Tokenizer valtok(val, valsep);
    Tokenizer::iterator valit=valtok.begin();
    const string sent_id = *valit++;
    const int sid = atoi(sent_id.c_str());
    assert(valit != valtok.end());
    const string b64val = *valit++;
    assert(valit == valtok.end());

    if (b64val.size() % 4 != 0) {
      cerr << "B64 encoding error 1! Skipping.\n";
      abort();
    }
    string encoded(b64val.size() / 4 * 3, '\0');
    if (!B64::b64decode(reinterpret_cast<const unsigned char*>(&b64val[0]), b64val.size(), &encoded[0], encoded.size())) {
      cerr << "B64 encoding error 2! Skipping.\n";
      abort();
    }

    boost::char_separator<char> keysep(" ");
    Tokenizer keytok(key, keysep);
    Tokenizer::iterator keyit=keytok.begin();
    const string dummyM = *keyit++;
    assert(keyit != keytok.end());
    const string s_origin = *keyit++;
    assert(keyit != keytok.end());
    const string s_axis = *keyit++;
    assert(keyit == keytok.end());
    
    // TODO: we read origin many times, but we only need it once
    assert(Weights::ReadSparseVectorString(s_origin, origin));

    SparseVector<double> dir;
    Weights w_dir;
    assert(Weights::ReadSparseVectorString(s_axis, &dir));

    if(sent_surfs->size() <= sid) {
      sent_surfs->resize(sid + 1);
    }

    // populate the error surface with the sentence we just received
    DirErrorSurface& dir_surfs = sent_surfs->at(sid);

    // insert this direction into the vector 
    size_t dir_id = IndexOf(*dirs, dir);
    if(dir_id == -1) {
      dir_id = dirs->size();
      dirs->push_back(dir);
    }

    // add this direction to the current sentence's error surface
    if(dir_surfs.size() <= dir_id) {
      dir_surfs.resize(dir_id + 1);
    }
    ErrorSurface& es = dir_surfs.AtDir(dir_id);
    if(es.size() != 0) {
      cerr << "ERROR: Unexpectedly received error surface for the same (origin, direction, sentence) from multiple map keys" << endl;
      abort();
    }
    es.Deserialize(type, encoded);
  }
}

void CheckSanity(const size_t num_srcs,
		 const vector<SparseVector<double> >& dirs,
		 const vector<DirErrorSurface>& sent_surfs) {

  bool error = false;

  if(sent_surfs.size() != num_srcs) {
    cerr << "ERROR: Expected " << num_srcs << " sentence-level directional error surfaces, but found " << sent_surfs.size() << endl;
    error = true;
  }

  // verify that everything is parallel
  for(size_t iSent=0; iSent<num_srcs; ++iSent) {
    const DirErrorSurface& dsurf = sent_surfs.at(iSent);
    if(dsurf.size() != dirs.size()) {
      cerr << "ERROR: Expected " << dirs.size() << " directional error surfaces for sentence " << iSent << " but found " << dsurf.size() << endl;
      error = true;
    }

    for(size_t iDir=0; iDir<dirs.size(); ++iDir) {
      const ErrorSurface& surf = dsurf.AtDir(iDir);
      if(surf.size() == 0) {
	cerr << "ERROR: No error segments for sentence " << iSent << " for direction " << iDir << ": " << dirs.at(iDir) << endl;
	error = true;
      }
    }
  }
  if(error) {
    abort();
  }
}

void LoadVocab(const string& file,
	       set<WordID>* vocab) {

  ReadFile rf(file);
  istream& in = *rf.stream();
  while(in) {
    string line;
    getline(in, line);
    if (line.empty() && !in) break;

    const WordID wid = TD::Convert(line);
    vocab->insert(wid);
  }
}

// TODO: This needs to more closely mirror the mr_vest_reducer
// Could we just write a special DT line optimizer instead
// of having a LinearLineOptimizer?
int main(int argc, char** argv) {
  po::variables_map conf;
  ParseOpts(argc, argv, &conf);

  const string loss_function = conf["loss_function"].as<string>();
  ScoreType type = ScoreTypeFromString(loss_function);
  LineOptimizer::ScoreType opt_type = LineOptimizer::GetOptType(type);

  const string src_vocab_file = conf["src_vocab"].as<string>();
  set<WordID> src_vocab;
  LoadVocab(src_vocab_file, &src_vocab);

  vector<DTSent> src_sents;
  string inFile = conf["src_sents"].as<string>();
  string errFile = conf["err_surface"].as<string>();
  LoadSents(inFile, &src_sents);
  cerr << "Loaded " << src_sents.size() << " source sentences" << endl;

  SparseVector<double> origin;
  vector<DirErrorSurface> sent_surfs;
  vector<SparseVector<double> > dirs;
  cerr << "Loading error surfaces..." << endl;
  LoadErrSurfaces(errFile, type, &origin, &dirs, &sent_surfs);

  cerr << "Loaded 1 origin weight vector" << endl;
  cerr << "Loaded " << dirs.size() << " line search directions" << endl;
  /*
  for(size_t i=0; i<dirs.size(); ++i) {
    cerr << "For direction " << i << ": Loaded " << sent_surfs.at(i).size() << " sentence-level error surfaces" << endl;
  }
  */
  CheckSanity(src_sents.size(), dirs, sent_surfs);

  const float DEFAULT_LINE_EPSILON = 1.0/65536.0;
  float dt_epsilon = 1.0/65536.0;

  vector<shared_ptr<Question> > questions;
  questions.push_back(shared_ptr<Question>(new SrcSentQuestion(src_sents.size())));
#if 0
  questions.push_back(shared_ptr<Question>(new QuestionQuestion));
  for(int i=1; i<4; ++i) {
    questions.push_back(shared_ptr<Question>(new OovQuestion(src_vocab, i)));
  }
  for(int i=2; i<25; ++i) {
    questions.push_back(shared_ptr<Question>(new LengthQuestion(i)));
  }
  // TODO: Question factory
  // TODO: LDA topic question
#endif

  // TODO: Load existing decision tree
  // for now, we just set the weights equal to the origin
  DTNode dtree(origin);
  
  vector<bool> active_sents(src_sents.size());
  active_sents.resize(src_sents.size());
  for(size_t i=0; i<src_sents.size(); ++i) {
    active_sents.at(i) = true;
  }

  string mode = conf["mode"].as<string>();
  if(mode == "split") {
    // TODO: verbosity?
    unsigned min_sents_per_node = conf["min_sents"].as<unsigned>();
    DTreeSplitOptimizer opt(opt_type, DEFAULT_LINE_EPSILON, dt_epsilon, min_sents_per_node, questions);

    float best_score = opt.GrowTree(origin, dirs, src_sents, sent_surfs, active_sents, dtree);

    // TODO: Add option for opt.Oracle
    
    // print new decision tree to stdout
    cout << dtree << endl;
    // TODO: Save decision tree for decoder use
    // Serialize(outFile, dtree);
  } else if(mode == "merge") {
    unsigned beam_size = conf["beam_size"].as<unsigned>();
    unsigned clusters = conf["clusters"].as<unsigned>();
    DTreeMergeOptimizer opt(opt_type, DEFAULT_LINE_EPSILON, dirs, beam_size);

    dtree.question_ = questions.front(); // split by all tuning sentences
    opt.MergeNode(origin, src_sents, active_sents, sent_surfs, clusters, dtree);

  } else {
    cerr << "ERROR: Unrecognized mode: " << mode << endl;
    abort();
  }
}
