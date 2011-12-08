#ifndef _VECTOR_UTIL_H_
#define _VECTOR_UTIL_H_

#include <iostream>
#include <vector>

template <typename T>
std::ostream& operator<<(std::ostream& out, const std::vector<T>& v) {
  out << "[";
  for(typename std::vector<T>::const_iterator it = v.begin(); it != v.end(); ++it) {
    out << *it;
    if(it != v.end()-1) out << ", ";
  }
  out << "]";
  return out;
}

#endif

