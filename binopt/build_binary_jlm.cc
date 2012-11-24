// building:
// g++ -I$HOME/prefix/include/ -Iklm -c dumb.cc
// g++ -lz klm/util/file.o klm/util/mmap.o klm/util/exception.o dumb.o -o dumb

#include <fcntl.h>
#include <stdint.h>
#include <iostream>
#include <map>
#include <vector>
using namespace std;

#include <boost/functional/hash.hpp>

#include "ff_jlm_hash.h"

// cdec util:
#include "filelib.h"

// KenLM util:
#include "util/probing_hash_table.hh"
#include "util/scoped.hh"
#include "util/file.hh"
#include "util/mmap.hh"

int GetID(const string& name, std::map<string,int>& m) {
  std::map<string,int>::const_iterator match = m.find(name);
  if(match != m.end()) {
    return match->second;
  } else {
    int fid = m.size();
    m.insert(pair<string,int>(name, fid));
    return fid;
  }
}

int main(int argc, char** argv) {

  if(argc != 4) {
    cerr << "Usage: build_binary_jlm jlm_out num_entries load_factor < featlm > vocab_feats_order" << endl;
    exit(1);
  }

  char* jlm_filename = argv[1];
  int num_elements = atoi(argv[2]);
  float load_factor = atof(argv[3]);

  size_t bytes = jlm::Table::Size(num_elements, load_factor);
  size_t mb = bytes / 1000 / 1000;
  cerr << "Building binary file of " << mb << "MB" << endl;

  // make sure we can open this file before doing a lot of work
  int fd = util::CreateOrThrow(jlm_filename);
  util::scoped_fd file(fd);
  //util::scoped_mmap fmem(MapZeroedWrite(jlm_filename, bytes, file), bytes); // too slow for random writes

  // write to RAM first
  util::scoped_memory mem;
  util::MapAnonymous(bytes, mem);
  jlm::Table table(mem.get(), mem.size());  

  std::map<string, int> all_feats;
  std::map<string, int> all_words;
  
  string line;
  int order = 0;
  int entries = 0;
  cerr << "Reading from STDIN" << endl;
  while (cin) {
    getline(cin, line);
    if (!cin)
      continue;
    
    ++entries;
    
    // XXX: I'm a terrible person for using const_cast
    char* ngram = strtok(const_cast<char*>(line.c_str()), "\t");
    char* feats = strtok(NULL, "\t");
    char* backoff_feats = strtok(NULL, "\t");
    
    //cerr << "Got ngram: " << ngram << endl;
    
    char* tok = strtok(ngram, " ");
    vector<int> toks;
    while (tok != NULL) {
      int wid = GetID(string(tok), all_words); // ugh, copy
      //cerr << "Token: " << tok << " -> " << wid << endl;
      toks.push_back(wid);
      tok = strtok(NULL, " ");
    }

    jlm::Entry entry;
    entry.key = jlm::Hash(toks);
    order = max(order, (int) toks.size());
        
    char* feat_and_value = strtok(feats, " ");
    if(strtok(NULL, " ") != NULL) {
      cerr << "ERROR: JLM: Expected only one feature: " << feats << endl;
      abort();
    }

#ifdef JLM_REAL_VALUES
    char* feat = strtok(feat_and_value, "=");
    char* match_value = strtok(NULL, "=");
    if(match_value == NULL) {
      cerr << "ERROR: JLM: Expected =value to follow feature: " << feat << endl;
      abort();
    }
    if(strtok(NULL, " ") != NULL) {
      cerr << "ERROR: JLM: Expected only feature and value delimited by =" << feat_and_value << endl;
      abort();
    }
#else
    char* feat = feat_and_value;
    if(strtok(NULL, " ") != NULL) {
      cerr << "ERROR: JLM: Expected only feature (not =value)" << endl;
      abort();
    }
#endif

    entry.match_feat = GetID(string(feat), all_feats); // ugh, copy

#ifdef JLM_REAL_VALUES
    entry.match_value = atof(match_value);
#endif
    
    // Now match backoff features
    if(backoff_feats != NULL) {
      feat_and_value = strtok(backoff_feats, " ");
      if(strtok(NULL, " ") != NULL) {
        cerr << "ERROR: JLM: Expected only one feature" << endl;
        abort();
      }

#ifdef JLM_REAL_VALUES
    char* feat = strtok(feat_and_value, "=");
    char* miss_value = strtok(NULL, "=");
    if(miss_value == NULL) {
      cerr << "ERROR: JLM: Expected =value to follow backoff feature" << endl;
      abort();
    }
    if(strtok(NULL, " ") != NULL) {
      cerr << "ERROR: JLM: Expected only backoff feature and value delimited by =" << endl;
      abort();
    }
#else
    char* feat = feat_and_value;
    if(strtok(NULL, " ") != NULL) {
      cerr << "ERROR: JLM: Expected only feature (not =value)" << endl;
      abort();
    }
#endif

      entry.miss_feat = GetID(string(feat), all_feats); // ugh, copy

#ifdef JLM_REAL_VALUES
      entry.miss_value = atof(miss_value);
#endif
    } else {
      entry.miss_feat = -1;
    }

    table.Insert(entry);
  }

  cerr << "JLM: Loaded " << entries << " entries with " << all_feats.size() << " features; Max order is " << order << endl;
  if(entries != num_elements) {
    cerr << "ERROR: Actually read " << entries << " entries, but we were told to expect " << num_elements << " entries." << endl;
    abort();
  }
  
  cout << "ORDER: " << order << endl;
  for(std::map<string,int>::const_iterator it = all_feats.begin(); it != all_feats.end(); ++it) {
    cout << "FEAT: " << it->first << " " << it->second << endl;
  }
  for(std::map<string,int>::const_iterator it = all_words.begin(); it != all_words.end(); ++it) {
    cout << "WORD: " << it->first << " " << it->second << endl;
  }

  // now write out the model after we're done with random writes
  cerr << "Dumping " << bytes << " bytes to disk" << endl;
  util::WriteOrThrow(file.get(), mem.get(), bytes);
  //close(fd);

  /*		}
  {
    cerr << "Opening " << filename << endl;

    util::scoped_fd file(util::OpenReadOrThrow(filename));
    uint64_t size = util::SizeFile(file.get());
    util::scoped_memory mem;
    MapRead(util::POPULATE_OR_READ, file.get(), 0, size, mem);
    Table table(mem.get(), mem.size());

    const Entry *i = NULL;
    table.Find(3, i);
    cerr << "Foudn: " <<  (int) i->key << " " << i->value << endl;
  }
  */
  
  return 0;
}
