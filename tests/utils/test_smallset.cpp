#include "cado_config.h"
#include <cstdio>
#include "smallset.h"

#ifdef HAVE_SSE2

template <int SIZE, typename ELEMENTTYPE>
void
test_smallset()
{
  const size_t nr_items = smallset<SIZE, ELEMENTTYPE>::nr_items;
  ELEMENTTYPE items[nr_items];
  for (size_t i = 0; i < nr_items; i++)
    items[i] = i;

  if (SIZE == 0) {
    smallset<SIZE, ELEMENTTYPE> set(items, 0);
    assert(!set.contains(0));
  }

  for (size_t i = 1; i <= nr_items; i++) {
    smallset<SIZE, ELEMENTTYPE> set(items, i);
    assert(set.contains(0));
    assert(set.contains(i-1));
    assert(!set.contains(i));
  }
}

template <typename ELEMENTTYPE>
void
test_one_type()
{
  test_smallset<0, ELEMENTTYPE>();
  test_smallset<1, ELEMENTTYPE>();
  test_smallset<2, ELEMENTTYPE>();
  test_smallset<3, ELEMENTTYPE>();
  test_smallset<4, ELEMENTTYPE>();
  test_smallset<5, ELEMENTTYPE>();
}

int main()
{
  test_one_type<unsigned char>();
  test_one_type<unsigned short>();
  test_one_type<unsigned int>();
  return(0);
}

#else

int main()
{
  return(0);
}

#endif