//This file is developed by Privacore ApS and covered by the GNU Affero General Public License. See LICENSE for details.
#ifndef FX_EXTRA_KEYWORDS_H_
#define FX_EXTRA_KEYWORDS_H_
#include <string>

namespace ExplicitKeywords {

bool initialize();
void finalize();
std::string lookupExplicitKeywords(const std::string &url);

}

#endif
