#ifndef QUESTION_H_
#define QUESTION_H_

#include <vector>
#include <map>
#include <iostream>
#include <string>
#include <sstream>
#include <exception>
using namespace std;

#include "cluster.h"

struct DTSent {
  unsigned id;
  vector<WordID> src;
  map<string, string> sgml;
  bool operator==(const DTSent& other) const {
    return src == other.src;
  }
};

class Question {
public:
  // answer is which branch (class/child) the given sentence belongs in, according to this question
  virtual unsigned Ask(const DTSent& sent) const =0;

  // what is the maximum number of branches that this question can result in?
  // in most decision tree estimation strategies (those without smoothing, and most with),
  // it's important to have at least 1 data point per branch so that we can estimate
  // some parameters for each branch
  virtual unsigned Size() const =0;
  virtual string ToString() const =0;
  virtual void Serialize(ostream& out) const =0;
};

inline ostream& operator<<(ostream& out, const Question& q) {
  out << q.ToString();
  return out;
}

class QuestionQuestion : public Question {
public:
  QuestionQuestion() {
    qmark_ = TD::Convert("?");
  }

  unsigned Ask(const DTSent& sent) const {
    WordID lastTok = sent.src.back();
    return (lastTok == qmark_) ? 1 : 0;
  }

  unsigned Size() const {
    return 2;
  }

  string ToString() const {
    return "Is last token '?'";
  }

  void Serialize(ostream& out) const {
    out << "QQ";
  }
private:
  WordID qmark_;
};

class LengthQuestion : public Question {
public:
  LengthQuestion(const int len) : len_(len) {}

  unsigned Ask(const DTSent& sent) const {
    return (sent.src.size() >= len_) ? 1 : 0;
  }

  unsigned Size() const {
    return 2;
  }

  string ToString() const {
    stringstream s;
    s << "Has length >= " << len_;
    return s.str();
  }

  void Serialize(ostream& out) const {
    out << "L" << len_;
  }
private:
  int len_;
};

class OovQuestion : public Question {
public:
 OovQuestion(const set<WordID>& vocab,
	     const int num)
   : vocab_(vocab),
    num_(num) {}

  unsigned Ask(const DTSent& sent) const {
    int n = 0;
    for(size_t i=0; i<sent.src.size(); ++i) {
      const WordID wid = sent.src.at(i);
      if(vocab_.find(wid) == vocab_.end()) {
	++n;
      }
    }
    return (n >= num_) ? 1 : 0;
  }

  unsigned Size() const {
    return 2;
  }

  string ToString() const {
    stringstream s;
    s << "OOV count >= " << num_;
    return s.str();
  }

  void Serialize(ostream& out) const {
    out << "OOV" << num_;
  }
private:
  const set<WordID>& vocab_;
  const int num_;
};

class SrcSentQuestion : public Question {
public:
 SrcSentQuestion(const size_t src_sents) : src_sents_(src_sents) {}

  unsigned Ask(const DTSent& sent) const {
    return sent.id;
  }

  unsigned Size() const {
    return src_sents_;
  }

  string ToString() const {
    return "What is the tuning set ID of this source sentence?";
  }

  void Serialize(ostream& out) const {
    out << "SrcSent";
  }

 private:
  const size_t src_sents_;
};

class SrcClusterQuestion : public Question {
public:
 SrcClusterQuestion(const Clustering& clust)
   : clust_(clust) {}

  unsigned Ask(const DTSent& sent) const {
    for(unsigned i=0; i<clust_.Size(); ++i) {
      const vector<bool>& cluster = clust_.active_sents_by_branch_.at(i);
      if(cluster.at(sent.id)) {
	return i;
      }
    }
    cerr << "ERROR: No matching cluster found" << endl;
    abort();
  }

  unsigned Size() const {
    return clust_.Size();
  }

  string ToString() const {
    return "What is the tuning set cluster ID of this source sentence?";
  }

  void Serialize(ostream& out) const {
    out << "SrcSentClust";
  }

 private:
  const Clustering clust_;
};

#endif
