#include "outils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pico/stdlib.h"

namespace outils {

bool ligne(char *buf, size_t n) {
    static char tampon[64];
    static size_t lg = 0;
    while (true) {
        const int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT)
            return false;
        if (c == '\r' || c == '\n') {
            if (lg == 0)
                continue;
            tampon[lg] = 0;
            std::snprintf(buf, n, "%s", tampon);
            lg = 0;
            return true;
        }
        if (lg < sizeof(tampon) - 1)
            tampon[lg++] = (char)c;
    }
}

size_t ecrire_histo(char *ligne, size_t place, const char *cle,
                    const uint32_t n[BANC_SEAUX]) {
    size_t lg = (size_t)std::snprintf(ligne, place, " %s=", cle);
    bool premier = true;
    for (int i = 0; i < BANC_SEAUX && lg < place; ++i) {
        if (!n[i])
            continue;
        lg += (size_t)std::snprintf(ligne + lg, place - lg, "%s%d:%lu",
                                    premier ? "" : ",", i, (unsigned long)n[i]);
        premier = false;
    }
    if (premier && lg < place)
        lg += (size_t)std::snprintf(ligne + lg, place - lg, "-");
    return lg < place ? lg : place - 1;
}

bool commande(const char *l, const char *mot, int &v) {
    const size_t m = std::strlen(mot);
    if (std::strncmp(l, mot, m) != 0 || l[m] != ' ')
        return false;
    char *fin = nullptr;
    const long x = std::strtol(l + m + 1, &fin, 10);
    if (fin == l + m + 1)
        return false;
    v = (int)x;
    return true;
}

} // namespace outils
