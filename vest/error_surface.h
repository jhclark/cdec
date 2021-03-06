#ifndef _ERROR_SURFACE_H_
#define _ERROR_SURFACE_H_

#include <vector>
#include <string>

#include "scorer.h"

class Score;

struct ErrorSegment {
  double x; // left boundary of this segment for the current feature's weight
  ScoreP delta; // change in suffucient stats / metric store at that boundary
  ErrorSegment() : x(0), delta(NULL) {}
};

inline std::ostream& operator<<(std::ostream& out, const ErrorSegment& score) {
  out << "((" << score.x << ": " << *(score.delta) << "))";
  return out;
}

class ErrorSurface : public std::vector<ErrorSegment> {
 public:
  ~ErrorSurface();
  void Serialize(std::string* out) const;
  void Deserialize(ScoreType type, const std::string& in);
};

typedef ErrorSurface::const_iterator ErrorIter;

#endif
