#include "CirBuf.h"


int main(int argc, char **argv)
{ return 0; }


// [thread 1] WorkingFunc:
// - calls CirBuf::InsertBuff
//   - asserts m_CondVar2

// [thread 2] InsertBufThd:
// - waits on m_CondVar2
// - asserts m_CondVar1

// [thread 0] CirBuf::ReadBuff / CirBuf::IsBuffHeadCorrect:
// - waits on m_CondVar1

// [thread 0] CirBuf::StopInstBufThr:
// - asserts m_CondVar2 (purely to get the thread to wake up and realize it should exit)


// m_CondVar1: intermediate processing by InsertBufThd done, safe to dequeue now
// m_CondVar2: new data from WorkingFunc is ready for InsertBufThd to process

// m_CondVar1 ==> m_InsThdFinishedWork_CondVar
// m_CondVar2 ==> m_InsThdStartWorking_CondVar






// OLD AND PROBABLY WRONG ======================================================
// presumably:
// + let CV "A" indicate that it is safe to insert data to the cirbuf
//   - this is m_CondVar1
// + let CV "B" indicate that data has just been inserted to the cirbuf and needs to be processed
//   - this is m_CondVar2
// - WorkingFunc gets a bunch of incoming data
// - WorkingFunc waits to make sure that CV "A" is asserted
// - WorkingFunc inserts that data into the circular buffer
//   - this asserts CV "B"
// - InsertBufThd waits for CV "B" to be asserted (waiting for work to do)
// - When CV "B" is asserted by WorkingFunc, it starts de-queueing data
// - Once the de-queueing is complete, InsertBufThd asserts CV "A"
// - Loop back to the start.
