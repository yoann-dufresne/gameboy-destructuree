/**
 * Les identifiants, dans include/secrets.h, ignoré par git.
 *
 *     cp include/secrets.h.example include/secrets.h
 */
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h manquant : cp include/secrets.h.example include/secrets.h, puis le remplir"
#endif

#ifndef BANC_MOT_DE_PASSE
#error "secrets.h doit definir BANC_MOT_DE_PASSE (8 caracteres au moins)"
#endif
