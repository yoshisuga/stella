//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2018 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include "AudioQueue.hxx"

using std::mutex;
using std::lock_guard;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioQueue::AudioQueue(uInt32 fragmentSize, uInt8 capacity, bool isStereo, uInt16 sampleRate)
  : myFragmentSize(fragmentSize),
    myIsStereo(isStereo),
    myFragmentQueue(capacity),
    myAllFragments(capacity + 2),
    mySize(0),
    myNextFragment(0)
{
  const uInt8 sampleSize = myIsStereo ? 2 : 1;

  myFragmentBuffer = new Int16[myFragmentSize * sampleSize * (capacity + 2)];

  for (uInt8 i = 0; i < capacity; i++)
    myFragmentQueue[i] = myAllFragments[i] = myFragmentBuffer + i * sampleSize * myFragmentSize;

  myAllFragments[capacity] = myFirstFragmentForEnqueue =
    myFragmentBuffer + capacity * sampleSize * myFragmentSize;

  myAllFragments[capacity + 1] = myFirstFragmentForDequeue =
    myFragmentBuffer + (capacity + 1) * sampleSize * myFragmentSize;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioQueue::~AudioQueue()
{
  delete[]myFragmentBuffer;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 AudioQueue::capacity() const
{
  return myFragmentQueue.size();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 AudioQueue::size()
{
  lock_guard<mutex> guard(myMutex);

  return mySize;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AudioQueue::isStereo() const
{
  return myIsStereo;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 AudioQueue::fragmentSize() const
{
  return myFragmentSize;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt16 AudioQueue::sampleRate() const
{
  return mySampleRate;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int16* AudioQueue::enqueue(Int16* fragment)
{
  lock_guard<mutex> guard(myMutex);

  Int16* newFragment;

  if (!fragment) {
    if (!myFirstFragmentForEnqueue) throw runtime_error("enqueue called empty");

    newFragment = myFirstFragmentForEnqueue;
    myFirstFragmentForEnqueue = 0;

    return newFragment;
  }

  const uInt8 capacity = myFragmentQueue.size();
  const uInt8 fragmentIndex = (myNextFragment + mySize) % capacity;

  newFragment = myFragmentQueue.at(fragmentIndex);
  myFragmentQueue.at(fragmentIndex) = fragment;

  if (mySize < capacity) mySize++;
  else myNextFragment = (myNextFragment + 1) % capacity;

  return newFragment;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int16* AudioQueue::dequeue(Int16* fragment)
{
  lock_guard<mutex> guard(myMutex);

  if (mySize == 0) return 0;

  if (!fragment) {
    if (!myFirstFragmentForDequeue) throw runtime_error("dequeue called empty");

    fragment = myFirstFragmentForDequeue;
    myFirstFragmentForDequeue = 0;
  }

  Int16* nextFragment = myFragmentQueue.at(myNextFragment);
  myFragmentQueue.at(myNextFragment) = fragment;

  mySize--;
  myNextFragment = (myNextFragment + 1) % myFragmentQueue.size();

  return nextFragment;
}