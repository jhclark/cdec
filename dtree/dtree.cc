#include "dtree.h"
#include "weights.h"

#include <boost/tokenizer.hpp>
typedef boost::tokenizer<boost::char_separator<char> > Tokenizer;

void ParseOpts(int argc, char** argv, po::variables_map* conf) {
  po::options_description opts("Configuration options");
  opts.add_options()
    ("loss_function,l",po::value<string>(), "Loss function being optimized")
    ("src_sents,s",po::value<string>(), "Source sentences (that we will be asking questions about)")
    ("err_surface,e",po::value<string>(), "Directory containing error surfaces for each sentence")
    ("min_sents,m",po::value<int>(), "Minimum sentences per decision tree node")
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
  while(in) {
    string line;
    getline(in, line);
    if (line.empty() && !in) break;
    
    sents->resize(sents->size()+1);
    DTSent& sent = sents->back();
    
    ProcessAndStripSGML(&line, &sent.sgml);
    TD::ConvertSentence(line, &sent.src);
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
		     vector<vector<ErrorSurface> >* surfaces_by_dir_by_sent) {

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
    
    // insert this direction into the vector 
    size_t dir_id = IndexOf(*dirs, dir);
    if(dir_id == -1) {
      dir_id = dirs->size();
      dirs->push_back(dir);
      surfaces_by_dir_by_sent->resize(dir_id + 1);
    }

    vector<ErrorSurface>& surfaces_by_sent = surfaces_by_dir_by_sent->at(dir_id);
    if(surfaces_by_sent.size() <= sid) {
      surfaces_by_sent.resize(sid + 1);
    }
    ErrorSurface& es = surfaces_by_sent.at(sid);
    if(es.size() != 0) {
      cerr << "ERROR: Unexpectedly received error surface for the same (origin, direction, sentence) from multiple map keys" << endl;
      abort();
    }
    es.Deserialize(type, encoded);
  }
}

void CheckSanity(const size_t num_srcs,
		 const vector<SparseVector<double> >& dirs,
		 const vector<vector<ErrorSurface> >& surfaces_by_dir_by_sent) {

  // verify that everything is parallel
  for(size_t iDir=0; iDir<dirs.size(); ++iDir) {
    const vector<ErrorSurface>& surfaces_by_sent = surfaces_by_dir_by_sent.at(iDir);
    if(surfaces_by_sent.size() != num_srcs) {
      cerr << "ERROR: Not enough error surfaces for all sentences sentences ("<<surfaces_by_sent.size()<<") for direction "<< dirs.at(iDir) << endl;
      abort();
    }
    for(size_t iSent=0; iSent<num_srcs; ++iSent) {
      const ErrorSurface& surf = surfaces_by_sent.at(iSent);
      if(surf.size() == 0) {
	cerr << "ERROR: No error segments for sentence " << iSent << " for direction " << dirs.at(iDir) << endl;
	abort();
      }
    }
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

  int min_sents_per_node = conf["min_sents"].as<int>();

  vector<DTSent> src_sents;
  string inFile = conf["src_sents"].as<string>();
  string errFile = conf["err_surface"].as<string>();
  LoadSents(inFile, &src_sents);
  cerr << "Loaded " << src_sents.size() << " source sentences" << endl;

  SparseVector<double> origin;
  vector<vector<ErrorSurface> > surfaces_by_dir_by_sent;
  vector<SparseVector<double> > dirs;
  cerr << "Loading error surfaces..." << endl;
  LoadErrSurfaces(errFile, type, &origin, &dirs, &surfaces_by_dir_by_sent);

  cerr << "Loaded 1 origin weight vector" << endl;
  cerr << "Loaded " << dirs.size() << " line search directions" << endl;
  for(size_t i=0; i<dirs.size(); ++i) {
    cerr << "For direction " << i << ": Loaded " << surfaces_by_dir_by_sent.at(i).size() << " sentence-level error surfaces" << endl;
  }
  CheckSanity(src_sents.size(), dirs, surfaces_by_dir_by_sent);

  const float DEFAULT_LINE_EPSILON = 1.0/65536.0;
  float dt_epsilon = 1.0/65536.0;

  // TODO: verbosity?
  DTreeOptimizer opt(opt_type, DEFAULT_LINE_EPSILON, dt_epsilon, min_sents_per_node);

  // TODO: Load existing decision tree
  // for now, we just set the weights equal to the origin
  DTNode dtree(NULL);
  dtree.weights_ = origin;

  vector<bool> active_sents(src_sents.size());
  active_sents.resize(src_sents.size());
  for(size_t i=0; i<src_sents.size(); ++i) {
    active_sents[i] = true;
  }

  float best_score = opt.GrowTree(origin, dirs, src_sents, surfaces_by_dir_by_sent, active_sents, dtree);

  // TODO: Save decision tree for decoder use
  // Serialize(outFile, dtree);
}
