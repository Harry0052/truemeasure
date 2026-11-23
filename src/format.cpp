#include "truemeasure/types.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace truemeasure {

const char *status_name(Status status) {
  switch (status) {
  case Status::Measured:
    return "measured";
  case Status::Withheld:
    return "withheld";
  case Status::Rejected:
    return "rejected";
  case Status::Internal:
    return "internal";
  }
  return "internal";
}

int exit_code_for(Status status) {
  switch (status) {
  case Status::Measured:
    return 0;
  case Status::Withheld:
    return 2;
  case Status::Rejected:
    return 3;
  case Status::Internal:
    return 1;
  }
  return 1;
}

std::string format_fixed(double value, int decimals) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out << std::setprecision(decimals) << value;
  return out.str();
}

} // namespace truemeasure
