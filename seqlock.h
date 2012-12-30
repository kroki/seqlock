/*
  Copyright (C) 2012 Tomash Brechko.  All rights reserved.

  This header is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This header is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this header.  If not, see <http://www.gnu.org/licenses/>.


  DESCRIPTION:

  Implement sequential read/write lock.

  When there are multiple writer threads a pair of calls
  seqlock_write_lock_spin()/seqlock_write_unlock() must be used.  As
  the name suggests, seqlock_write_lock_spin() may spin waiting for
  other writer thread to complete the writing.  When there's only one
  writer thread (or several threads perform the writing in a
  synchronized critical section) a more efficient pair of calls
  seqlock_write_lock_spin()/seqlock_write_unlock() may be used.

  Reading is done with seqlock_read_lock()/seqlock_read_unlock() and
  will execute the code between the these calls repeatedly until a
  clean read is achieved (i.e. a consistent read that wasn't disturbed
  by writers).

  All three pairs of calls expand to beginning and end of a code block
  and so they must reside within the same function and at the same
  lexical nesting level.

  seqlock_t is an arithmetic type and should be initialized with zero
  before use.

  Defining KROKI_SEQLOCK_NOPOLLUTE will result in omitting alias
  definitions, but functionality will still be available with the
  namespace prefix 'kroki_'.

  Implementation requires GCC 4.7.3+.

  On IA-64 (i.e. Itanium CPUs, this is _not_ x86-64) writing and
  reading of user data must be wrapped in __atomic_store_n() and
  __atomic_load_n() respectively (__ATOMIC_RELAXED memory model will
  suffice).  On other architectures plain store/load may be used.
*/

#ifndef KROKI_SEQLOCK_NOPOLLUTE

#define seqlock_t  kroki_seqlock_t
#define seqlock_write_lock(plock)  kroki_seqlock_write_lock(plock)
#define seqlock_write_lock_spin(plock)  kroki_seqlock_write_lock_spin(plock)
#define seqlock_write_unlock(plock)  kroki_seqlock_write_unlock(plock)
#define seqlock_read_lock(plock)  kroki_seqlock_read_lock(plock)
#define seqlock_read_unlock(plock)  kroki_seqlock_read_unlock(plock)

#endif  /* ! KROKI_SEQLOCK_NOPOLLUTE */


#ifndef KROKI_SEQLOCK_H
#define KROKI_SEQLOCK_H 1


typedef unsigned int kroki_seqlock_t;


/*
  In seqlock_write_lock() the first __atomic_store_n(__ATOMIC_RELAXED)
  increments the lock, and second __atomic_store_n(__ATOMIC_RELEASE)
  emits store-store memory barrier before the store (except for IA-64
  where __atomic_store_n(__ATOMIC_RELAXED) must be used for user data
  stores to emit st.rel instructions).

  __atomic_signal_fence(__ATOMIC_ACQ_REL) is used as a two-way
  compiler barrier to prevent user stores from mixing with lock
  update.
*/
#define kroki_seqlock_write_lock(plock)                 \
  do                                                    \
    {                                                   \
      kroki_seqlock_t _kroki_seqlock_old =              \
        __atomic_load_n(plock, __ATOMIC_RELAXED);       \
      __atomic_store_n(plock, _kroki_seqlock_old + 1,   \
                       __ATOMIC_RELAXED);               \
      __atomic_store_n(plock, _kroki_seqlock_old + 1,   \
                       __ATOMIC_RELEASE);               \
      __atomic_signal_fence(__ATOMIC_ACQ_REL)

/*
  In seqlock_write_lock_spin() in __atomic_compare_exchange_n() we
  expect the old value of the lock if we read it as even
  (i.e. unlocked), or the next even value if we read it as odd
  (i.e. locked).
*/
#define kroki_seqlock_write_lock_spin(plock)                            \
  do                                                                    \
    {                                                                   \
      kroki_seqlock_t _kroki_seqlock_old =                              \
        (__atomic_load_n(plock, __ATOMIC_RELAXED) + 1) & ~1;            \
      while (__builtin_expect(                                          \
               ! __atomic_compare_exchange_n(plock,                     \
                                             &_kroki_seqlock_old,       \
                                             _kroki_seqlock_old + 1,    \
                                             1,                         \
                                             __ATOMIC_ACQUIRE,          \
                                             __ATOMIC_RELAXED),         \
               0))                                                      \
        {                                                               \
          _kroki_seqlock_pause();                                       \
          _kroki_seqlock_old =                                          \
            (__atomic_load_n(plock, __ATOMIC_RELAXED) + 1) & ~1;        \
        }                                                               \
      do {} while (0)

#define kroki_seqlock_write_unlock(plock)               \
      __atomic_store_n(plock, _kroki_seqlock_old + 2,   \
                       __ATOMIC_RELEASE);               \
    }                                                   \
  while (0)

#define kroki_seqlock_read_lock(plock)                          \
  do                                                            \
    {                                                           \
      kroki_seqlock_t _kroki_seqlock_old;                       \
      kroki_seqlock_t _kroki_seqlock_new =                      \
        __atomic_load_n(plock, __ATOMIC_ACQUIRE);               \
      do                                                        \
        {                                                       \
          do                                                    \
            _kroki_seqlock_old = _kroki_seqlock_new & ~1;       \
          while (0)

/*
  In seqlock_read_unlock() first __atomic_load_n(__ATOMIC_ACQUIRE)
  emits load-load memory barrier after the load, and second
  __atomic_load_n(__ATOMIC_ACQUIRE) loads the actual value and also
  emits the barrier before possible next iteration of the loop (again,
  not for IA-64, where __atomic_load_n(__ATOMIC_RELAXED) must be used
  for user data loads to emit ld.acq instructions).

  __atomic_signal_fence(__ATOMIC_ACQ_REL) is used as a two-way
  compiler barrier to prevent user loads from mixing with lock query.
*/
#define kroki_seqlock_read_unlock(plock)                                \
          __atomic_signal_fence(__ATOMIC_ACQ_REL);                      \
          __atomic_load_n(plock, __ATOMIC_ACQUIRE);                     \
          _kroki_seqlock_new =                                          \
            __atomic_load_n(plock, __ATOMIC_ACQUIRE);                   \
        }                                                               \
      while (__builtin_expect(_kroki_seqlock_old != _kroki_seqlock_new, \
                              0));                                      \
    }                                                                   \
  while (0)

#if defined(__i686__) || defined(__x86_64__)
#define _kroki_seqlock_pause()  __builtin_ia32_pause()
#else
#define _kroki_seqlock_pause()
#endif


#endif  /* ! KROKI_SEQLOCK_H */
