#include "cado.h"
/* 
 * Copyright (C) 2009, 2011 William Hart
 * 
 * This file is part of FLINT.
 * 
 * FLINT is free software: you can redistribute it and/or modify it under the 
 * terms of the GNU Lesser General Public License (LGPL) as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.  See <http://www.gnu.org/licenses/>. */

#include "gmp.h"
#include "flint.h"
#include "fft.h"

/*
 * Convert the Fermat number ``(i, limbs)`` modulo ``B^limbs + 1`` to
 * an ``mpz_t m``. Assumes ``m`` has been initialised. This function 
 * is used only in test code.
 * 
 * 
 */
void fermat_to_mpz(mpz_t m, mp_limb_t * i, mp_size_t limbs)
{
    mp_limb_signed_t hi;

    mpz_realloc(m, limbs + 1);
    flint_mpn_copyi(m->_mp_d, i, limbs + 1);
    hi = i[limbs];
    if (hi < WORD(0)) {
	mpn_neg_n(m->_mp_d, m->_mp_d, limbs + 1);
	m->_mp_size = limbs + 1;
	while ((m->_mp_size) && (!m->_mp_d[m->_mp_size - 1]))
	    m->_mp_size--;
	m->_mp_size = -m->_mp_size;
    } else {
	m->_mp_size = limbs + 1;
	while ((m->_mp_size) && (!m->_mp_d[m->_mp_size - 1]))
	    m->_mp_size--;
    }
}
